#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>

#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "pulsebook/wire/ethernet_frame.hpp"

namespace {

inline constexpr const char* kDefaultInterface = "pb_peer";
inline constexpr int kOrderTimeoutMs = 5000;
inline constexpr std::size_t kWarmupOrders = 100'000;
inline constexpr std::size_t kMeasuredOrders = 1'000'000;

pulsebook::wire::EthernetEnvelope market_envelope() noexcept {
    pulsebook::wire::EthernetEnvelope envelope{};

    envelope.destination = {
        std::byte{0x02},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x20},
    };

    envelope.source = {
        std::byte{0x02},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x10},
    };

    return envelope;
}

pulsebook::wire::MarketDataMessage make_message(
    const std::uint32_t sequence_number,
    const pulsebook::wire::Side side,
    const std::uint16_t level,
    const std::int64_t price_ticks,
    const std::uint32_t quantity) noexcept {
    pulsebook::wire::MarketDataMessage message{};

    message.sequence_number = sequence_number;
    message.exchange_timestamp_ns =
        static_cast<std::uint64_t>(sequence_number) * 1000ULL;
    message.instrument_id = 77;
    message.price_ticks = price_ticks;
    message.quantity = quantity;
    message.side = side;
    message.action = pulsebook::wire::UpdateAction::modify;
    message.level = level;
    message.source_flags = 0;

    return message;
}

bool send_market_message(
    const int socket_fd,
    const sockaddr_ll& destination,
    const pulsebook::wire::MarketDataMessage& message) noexcept {
    pulsebook::wire::EthernetFrame frame{};

    if (!pulsebook::wire::encode_market_data_frame(
            market_envelope(),
            message,
            frame)) {
        return false;
    }

    const ssize_t sent = sendto(
        socket_fd,
        frame.data(),
        frame.size(),
        0,
        reinterpret_cast<const sockaddr*>(&destination),
        sizeof(destination));

    return sent == static_cast<ssize_t>(frame.size());
}

bool receive_valid_order(
    const int socket_fd,
    pulsebook::wire::OutboundOrderMessage& output) noexcept {
    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds(kOrderTimeoutMs);

    while (std::chrono::steady_clock::now() < deadline) {
        pollfd descriptor{};
        descriptor.fd = socket_fd;
        descriptor.events = POLLIN;

        const int poll_result = poll(&descriptor, 1, 250);

        if (poll_result < 0) {
            return false;
        }

        if (poll_result == 0) {
            continue;
        }

        std::array<std::byte, 4096> buffer{};

        const ssize_t received = recvfrom(
            socket_fd,
            buffer.data(),
            buffer.size(),
            0,
            nullptr,
            nullptr);

        if (received <= 0) {
            return false;
        }

        pulsebook::wire::EthernetEnvelope envelope{};

        if (pulsebook::wire::decode_order_frame(
                std::span<const std::byte>(
                    buffer.data(),
                    static_cast<std::size_t>(received)),
                envelope,
                output) ==
            pulsebook::wire::FrameDecodeError::none) {
            return true;
        }
    }

    return false;
}

bool validate_buy_order(
    const pulsebook::wire::OutboundOrderMessage& order) noexcept {
    return order.instrument_id == 77 &&
           order.price_ticks == 100100 &&
           order.quantity == 10 &&
           order.side == pulsebook::wire::Side::buy;
}

bool send_trigger_and_validate_order(
    const int socket_fd,
    const sockaddr_ll& destination,
    const std::uint32_t sequence_number,
    const std::size_t event_index) noexcept {
    const std::uint32_t trigger_quantity =
        1000U + static_cast<std::uint32_t>(event_index & 1U);

    if (!send_market_message(
            socket_fd,
            destination,
            make_message(
                sequence_number,
                pulsebook::wire::Side::buy,
                0,
                100000,
                trigger_quantity))) {
        return false;
    }

    pulsebook::wire::OutboundOrderMessage order{};

    return receive_valid_order(socket_fd, order) &&
           validate_buy_order(order);
}

}  // namespace

int main(int argc, char** argv) {
    const char* const interface_name =
        argc > 1 ? argv[1] : kDefaultInterface;

    const unsigned int interface_index =
        if_nametoindex(interface_name);

    if (interface_index == 0) {
        std::fprintf(
            stderr,
            "Benchmark generator failure: interface %s not found\n",
            interface_name);
        return EXIT_FAILURE;
    }

    const int socket_fd = socket(
        AF_PACKET,
        SOCK_RAW,
        htons(pulsebook::wire::kEtherType));

    if (socket_fd < 0) {
        std::fprintf(
            stderr,
            "Benchmark generator failure: socket creation failed: %s\n",
            std::strerror(errno));
        return EXIT_FAILURE;
    }

    sockaddr_ll bind_address{};
    bind_address.sll_family = AF_PACKET;
    bind_address.sll_protocol = htons(pulsebook::wire::kEtherType);
    bind_address.sll_ifindex = static_cast<int>(interface_index);

    if (bind(
            socket_fd,
            reinterpret_cast<const sockaddr*>(&bind_address),
            sizeof(bind_address)) < 0) {
        std::fprintf(
            stderr,
            "Benchmark generator failure: bind failed: %s\n",
            std::strerror(errno));
        close(socket_fd);
        return EXIT_FAILURE;
    }

    sockaddr_ll destination{};
    destination.sll_family = AF_PACKET;
    destination.sll_protocol = htons(pulsebook::wire::kEtherType);
    destination.sll_ifindex = static_cast<int>(interface_index);
    destination.sll_halen = 6;

    const auto envelope = market_envelope();

    for (std::size_t index = 0;
         index < envelope.destination.size();
         ++index) {
        destination.sll_addr[index] =
            std::to_integer<std::uint8_t>(
                envelope.destination[index]);
    }

    std::uint32_t sequence_number = 1;

    for (std::uint16_t level = 0; level < 5; ++level) {
        if (!send_market_message(
                socket_fd,
                destination,
                make_message(
                    sequence_number++,
                    pulsebook::wire::Side::sell,
                    level,
                    100100 + level,
                    10))) {
            std::fprintf(
                stderr,
                "Benchmark generator failure: ASK seed TX failed\n");
            close(socket_fd);
            return EXIT_FAILURE;
        }
    }

    for (std::uint16_t level = 0; level < 5; ++level) {
        if (!send_market_message(
                socket_fd,
                destination,
                make_message(
                    sequence_number++,
                    pulsebook::wire::Side::buy,
                    level,
                    100000 - level,
                    10))) {
            std::fprintf(
                stderr,
                "Benchmark generator failure: BID seed TX failed\n");
            close(socket_fd);
            return EXIT_FAILURE;
        }
    }

    std::printf("============================================================\n");
    std::printf("PulseBook Level 3 Phase 9C - AF_PACKET Generator\n");
    std::printf("============================================================\n");
    std::printf("Interface:                 %s\n", interface_name);
    std::printf("Seed market packets sent:  10\n");
    std::printf("Warmup orders to request:  %zu\n", kWarmupOrders);
    std::printf("Measured orders to request:%zu\n", kMeasuredOrders);
    std::fflush(stdout);

    for (std::size_t event = 0;
         event < kWarmupOrders;
         ++event) {
        if (!send_trigger_and_validate_order(
                socket_fd,
                destination,
                sequence_number++,
                event)) {
            std::fprintf(
                stderr,
                "Benchmark generator failure during warmup event %zu\n",
                event);
            close(socket_fd);
            return EXIT_FAILURE;
        }
    }

    std::printf("Warmup completed:           %zu orders validated\n",
                kWarmupOrders);
    std::fflush(stdout);

    for (std::size_t event = 0;
         event < kMeasuredOrders;
         ++event) {
        if (!send_trigger_and_validate_order(
                socket_fd,
                destination,
                sequence_number++,
                event)) {
            std::fprintf(
                stderr,
                "Benchmark generator failure during measured event %zu\n",
                event);
            close(socket_fd);
            return EXIT_FAILURE;
        }

        if ((event + 1) % 100'000 == 0) {
            std::printf("Validated measured orders: %zu / %zu\n",
                        event + 1,
                        kMeasuredOrders);
            std::fflush(stdout);
        }
    }

    std::printf("\nGenerator result:\n");
    std::printf("  Seed market packets:      10\n");
    std::printf("  Warmup orders validated:  %zu\n", kWarmupOrders);
    std::printf("  Measured orders validated:%zu\n", kMeasuredOrders);
    std::printf("  Validation failures:      0\n");
    std::printf("\nStatus:                    OK\n");

    close(socket_fd);
    return EXIT_SUCCESS;
}

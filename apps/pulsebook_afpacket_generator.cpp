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
inline constexpr int kReceiveTimeoutMs = 5000;

pulsebook::wire::EthernetEnvelope make_market_envelope() noexcept {
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

    return message;
}

bool transmit_market_message(
    const int socket_fd,
    const sockaddr_ll& destination,
    const pulsebook::wire::MarketDataMessage& message) noexcept {
    pulsebook::wire::EthernetFrame frame{};

    if (!pulsebook::wire::encode_market_data_frame(
            make_market_envelope(),
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

bool receive_generated_order(
    const int socket_fd,
    pulsebook::wire::OutboundOrderMessage& output) noexcept {
    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds(kReceiveTimeoutMs);

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

        std::array<std::byte, 2048> buffer{};

        const ssize_t bytes_received = recvfrom(
            socket_fd,
            buffer.data(),
            buffer.size(),
            0,
            nullptr,
            nullptr);

        if (bytes_received < 0) {
            return false;
        }

        pulsebook::wire::EthernetEnvelope envelope{};

        const auto result = pulsebook::wire::decode_order_frame(
            std::span<const std::byte>(
                buffer.data(),
                static_cast<std::size_t>(bytes_received)),
            envelope,
            output);

        if (result == pulsebook::wire::FrameDecodeError::none) {
            return true;
        }
    }

    return false;
}

}  // namespace

int main(int argc, char** argv) {
    const char* const interface_name =
        argc > 1 ? argv[1] : kDefaultInterface;

    const unsigned int interface_index =
        if_nametoindex(interface_name);

    if (interface_index == 0) {
        std::fprintf(stderr,
                     "Generator failure: interface %s not found\n",
                     interface_name);
        return EXIT_FAILURE;
    }

    const int socket_fd = socket(
        AF_PACKET,
        SOCK_RAW,
        htons(pulsebook::wire::kEtherType));

    if (socket_fd < 0) {
        std::fprintf(stderr,
                     "Generator failure: raw socket creation failed: %s\n",
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
        std::fprintf(stderr,
                     "Generator failure: socket bind failed: %s\n",
                     std::strerror(errno));
        close(socket_fd);
        return EXIT_FAILURE;
    }

    sockaddr_ll destination{};
    destination.sll_family = AF_PACKET;
    destination.sll_protocol = htons(pulsebook::wire::kEtherType);
    destination.sll_ifindex = static_cast<int>(interface_index);
    destination.sll_halen = 6;

    const auto envelope = make_market_envelope();

    for (std::size_t index = 0; index < envelope.destination.size(); ++index) {
        destination.sll_addr[index] =
            std::to_integer<std::uint8_t>(envelope.destination[index]);
    }

    std::uint32_t sequence_number = 1;

    for (std::uint16_t level = 0; level < 5; ++level) {
        if (!transmit_market_message(
                socket_fd,
                destination,
                make_message(
                    sequence_number++,
                    pulsebook::wire::Side::sell,
                    level,
                    100100 + level,
                    10))) {
            std::fprintf(stderr, "Generator failure: ASK frame TX failed\n");
            close(socket_fd);
            return EXIT_FAILURE;
        }
    }

    for (std::uint16_t level = 0; level < 5; ++level) {
        if (!transmit_market_message(
                socket_fd,
                destination,
                make_message(
                    sequence_number++,
                    pulsebook::wire::Side::buy,
                    level,
                    100000 - level,
                    10))) {
            std::fprintf(stderr, "Generator failure: BID frame TX failed\n");
            close(socket_fd);
            return EXIT_FAILURE;
        }
    }

    if (!transmit_market_message(
            socket_fd,
            destination,
            make_message(
                sequence_number++,
                pulsebook::wire::Side::buy,
                0,
                100000,
                1000))) {
        std::fprintf(stderr, "Generator failure: BUY trigger frame TX failed\n");
        close(socket_fd);
        return EXIT_FAILURE;
    }

    pulsebook::wire::OutboundOrderMessage received_order{};

    if (!receive_generated_order(socket_fd, received_order)) {
        std::fprintf(stderr,
                     "Generator failure: no valid outbound order was received\n");
        close(socket_fd);
        return EXIT_FAILURE;
    }

    const bool valid_order =
        received_order.instrument_id == 77 &&
        received_order.price_ticks == 100100 &&
        received_order.quantity == 10 &&
        received_order.side == pulsebook::wire::Side::buy;

    if (!valid_order) {
        std::fprintf(stderr,
                     "Generator failure: outbound order fields are incorrect\n");
        close(socket_fd);
        return EXIT_FAILURE;
    }

    std::printf("============================================================\n");
    std::printf("PulseBook Level 3 Phase 9B - Linux Raw Packet Generator\n");
    std::printf("============================================================\n");
    std::printf("Interface:                 %s\n", interface_name);
    std::printf("Market packets sent:       11\n");
    std::printf("Outbound orders received:  1\n");
    std::printf("Generated order side:      BUY\n");
    std::printf("Generated order price:     %lld\n",
                static_cast<long long>(received_order.price_ticks));
    std::printf("Generated order quantity:  %u\n",
                received_order.quantity);
    std::printf("Status:                    OK\n");

    close(socket_fd);
    return EXIT_SUCCESS;
}

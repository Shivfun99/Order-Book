#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>

#include <rte_eal.h>
#include <rte_errno.h>
#include <rte_eth_ring.h>
#include <rte_ethdev.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_pause.h>
#include <rte_ring.h>
#include <rte_version.h>

#include "pulsebook/dpdk/dpdk_config.hpp"
#include "pulsebook/wire/ethernet_frame.hpp"

namespace {

using pulsebook::dpdk::kBurstSize;
using pulsebook::dpdk::kDataRoomSize;
using pulsebook::dpdk::kEnginePortName;
using pulsebook::dpdk::kGeneratorPortName;
using pulsebook::dpdk::kMarketDataRingName;
using pulsebook::dpdk::kMempoolCacheSize;
using pulsebook::dpdk::kMempoolName;
using pulsebook::dpdk::kMempoolSize;
using pulsebook::dpdk::kOutboundOrderRingName;
using pulsebook::dpdk::kReceivePollAttempts;
using pulsebook::dpdk::kRingSize;
using pulsebook::dpdk::kRxDescriptors;
using pulsebook::dpdk::kRxQueues;
using pulsebook::dpdk::kTxDescriptors;
using pulsebook::dpdk::kTxQueues;

using pulsebook::wire::EthernetEnvelope;
using pulsebook::wire::EthernetFrame;
using pulsebook::wire::FrameDecodeError;
using pulsebook::wire::MarketDataMessage;
using pulsebook::wire::OrderType;
using pulsebook::wire::OutboundOrderMessage;
using pulsebook::wire::Side;
using pulsebook::wire::TimeInForce;
using pulsebook::wire::UpdateAction;
using pulsebook::wire::decode_market_data_frame;
using pulsebook::wire::decode_order_frame;
using pulsebook::wire::encode_market_data_frame;
using pulsebook::wire::encode_order_frame;
using pulsebook::wire::kEthernetFrameBytesWithoutFcs;

struct VirtualPorts {
    std::uint16_t generator{};
    std::uint16_t engine{};
};

void print_failure(const char* operation, const int error_code) noexcept {
    std::fprintf(stderr,
                 "PulseBook Phase 4 failure: %s failed, code=%d, reason=%s\n",
                 operation,
                 error_code,
                 rte_strerror(error_code < 0 ? -error_code : error_code));
}

bool configure_and_start_port(const std::uint16_t port_id,
                              rte_mempool* const mempool) noexcept {
    rte_eth_conf port_configuration{};

    int result = rte_eth_dev_configure(
        port_id,
        kRxQueues,
        kTxQueues,
        &port_configuration);

    if (result < 0) {
        print_failure("rte_eth_dev_configure", result);
        return false;
    }

    result = rte_eth_rx_queue_setup(
        port_id,
        0,
        kRxDescriptors,
        rte_socket_id(),
        nullptr,
        mempool);

    if (result < 0) {
        print_failure("rte_eth_rx_queue_setup", result);
        return false;
    }

    result = rte_eth_tx_queue_setup(
        port_id,
        0,
        kTxDescriptors,
        rte_socket_id(),
        nullptr);

    if (result < 0) {
        print_failure("rte_eth_tx_queue_setup", result);
        return false;
    }

    result = rte_eth_dev_start(port_id);

    if (result < 0) {
        print_failure("rte_eth_dev_start", result);
        return false;
    }

    return true;
}

bool create_virtual_ports(rte_mempool* const mempool,
                          VirtualPorts& ports) noexcept {
    const unsigned int socket_id = rte_socket_id();

    rte_ring* const market_data_ring = rte_ring_create(
        kMarketDataRingName,
        kRingSize,
        static_cast<int>(socket_id),
        RING_F_SP_ENQ | RING_F_SC_DEQ);

    if (market_data_ring == nullptr) {
        std::fprintf(stderr,
                     "PulseBook Phase 4 failure: cannot create market-data ring: %s\n",
                     rte_strerror(rte_errno));
        return false;
    }

    rte_ring* const outbound_order_ring = rte_ring_create(
        kOutboundOrderRingName,
        kRingSize,
        static_cast<int>(socket_id),
        RING_F_SP_ENQ | RING_F_SC_DEQ);

    if (outbound_order_ring == nullptr) {
        std::fprintf(stderr,
                     "PulseBook Phase 4 failure: cannot create order ring: %s\n",
                     rte_strerror(rte_errno));
        return false;
    }

    rte_ring* generator_rx_queues[] = {outbound_order_ring};
    rte_ring* generator_tx_queues[] = {market_data_ring};

    rte_ring* engine_rx_queues[] = {market_data_ring};
    rte_ring* engine_tx_queues[] = {outbound_order_ring};

    const int generator_port = rte_eth_from_rings(
        kGeneratorPortName,
        generator_rx_queues,
        1,
        generator_tx_queues,
        1,
        socket_id);

    if (generator_port < 0) {
        std::fprintf(stderr,
                     "PulseBook Phase 4 failure: cannot create generator port\n");
        return false;
    }

    const int engine_port = rte_eth_from_rings(
        kEnginePortName,
        engine_rx_queues,
        1,
        engine_tx_queues,
        1,
        socket_id);

    if (engine_port < 0) {
        std::fprintf(stderr,
                     "PulseBook Phase 4 failure: cannot create engine port\n");
        return false;
    }

    ports.generator = static_cast<std::uint16_t>(generator_port);
    ports.engine = static_cast<std::uint16_t>(engine_port);

    if (!configure_and_start_port(ports.generator, mempool)) {
        return false;
    }

    if (!configure_and_start_port(ports.engine, mempool)) {
        return false;
    }

    return true;
}

rte_mbuf* frame_to_mbuf(rte_mempool* const mempool,
                        const EthernetFrame& frame) noexcept {
    rte_mbuf* const mbuf = rte_pktmbuf_alloc(mempool);

    if (mbuf == nullptr) {
        return nullptr;
    }

    void* const destination = rte_pktmbuf_append(
        mbuf,
        static_cast<std::uint16_t>(frame.size()));

    if (destination == nullptr) {
        rte_pktmbuf_free(mbuf);
        return nullptr;
    }

    std::memcpy(destination, frame.data(), frame.size());
    return mbuf;
}

std::span<const std::byte> mbuf_frame_bytes(
    const rte_mbuf* const mbuf) noexcept {
    const auto* const data = rte_pktmbuf_mtod(mbuf, const std::byte*);
    return std::span<const std::byte>(
        data,
        static_cast<std::size_t>(rte_pktmbuf_data_len(mbuf)));
}

rte_mbuf* receive_single_packet(const std::uint16_t port_id) noexcept {
    std::array<rte_mbuf*, kBurstSize> packets{};

    for (std::uint32_t attempt = 0;
         attempt < kReceivePollAttempts;
         ++attempt) {
        const std::uint16_t received = rte_eth_rx_burst(
            port_id,
            0,
            packets.data(),
            kBurstSize);

        if (received == 0) {
            rte_pause();
            continue;
        }

        for (std::uint16_t index = 1; index < received; ++index) {
            rte_pktmbuf_free(packets[index]);
        }

        return packets[0];
    }

    return nullptr;
}

bool transmit_single_packet(const std::uint16_t port_id,
                            rte_mbuf* const packet) noexcept {
    rte_mbuf* packets[] = {packet};

    const std::uint16_t transmitted = rte_eth_tx_burst(
        port_id,
        0,
        packets,
        1);

    if (transmitted != 1) {
        rte_pktmbuf_free(packet);
        return false;
    }

    return true;
}

EthernetEnvelope make_market_envelope() noexcept {
    EthernetEnvelope envelope{};

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

EthernetEnvelope make_response_envelope(
    const EthernetEnvelope& received_envelope) noexcept {
    EthernetEnvelope response{};

    response.destination = received_envelope.source;
    response.source = received_envelope.destination;

    return response;
}

MarketDataMessage make_market_data_message() noexcept {
    MarketDataMessage message{};

    message.sequence_number = 1001;
    message.wire_flags = 0;
    message.exchange_timestamp_ns = 1234567890123ULL;
    message.instrument_id = 77;
    message.price_ticks = 100250;
    message.quantity = 600;
    message.side = Side::buy;
    message.action = UpdateAction::modify;
    message.level = 1;
    message.source_flags = 0;

    return message;
}

OutboundOrderMessage make_test_order_from_market_data(
    const MarketDataMessage& market_data) noexcept {
    OutboundOrderMessage order{};

    order.sequence_number = market_data.sequence_number + 1;
    order.wire_flags = 0;
    order.client_order_id = 9000001ULL;
    order.instrument_id = market_data.instrument_id;
    order.price_ticks = market_data.price_ticks;
    order.quantity = 10;
    order.side = Side::buy;
    order.order_type = OrderType::limit;
    order.time_in_force = TimeInForce::immediate_or_cancel;
    order.order_flags = 0;
    order.reserved = 0;

    return order;
}

void close_port(const std::uint16_t port_id) noexcept {
    const int stop_result = rte_eth_dev_stop(port_id);

    if (stop_result < 0) {
        print_failure("rte_eth_dev_stop", stop_result);
    }

    const int close_result = rte_eth_dev_close(port_id);

    if (close_result < 0) {
        print_failure("rte_eth_dev_close", close_result);
    }
}

}  // namespace

int main(int argc, char** argv) {
    const int eal_arguments = rte_eal_init(argc, argv);

    if (eal_arguments < 0) {
        std::fprintf(stderr, "PulseBook Phase 4 failure: EAL initialization failed\n");
        return EXIT_FAILURE;
    }

    std::printf("============================================================\n");
    std::printf("PulseBook Level 3 Phase 4 - Virtual DPDK RX/TX Path\n");
    std::printf("============================================================\n");
    std::printf("DPDK version:             %s\n", rte_version());
    std::printf("Main lcore:               %u\n", rte_lcore_id());
    std::printf("Available ports at start: %u\n", rte_eth_dev_count_avail());

    rte_mempool* const mempool = rte_pktmbuf_pool_create(
        kMempoolName,
        kMempoolSize,
        kMempoolCacheSize,
        0,
        kDataRoomSize,
        rte_socket_id());

    if (mempool == nullptr) {
        std::fprintf(stderr,
                     "PulseBook Phase 4 failure: cannot create mbuf pool: %s\n",
                     rte_strerror(rte_errno));
        rte_eal_cleanup();
        return EXIT_FAILURE;
    }

    VirtualPorts ports{};

    if (!create_virtual_ports(mempool, ports)) {
        rte_eal_cleanup();
        return EXIT_FAILURE;
    }

    std::printf("Generator virtual port:   %u\n", ports.generator);
    std::printf("Engine virtual port:      %u\n", ports.engine);
    std::printf("Available virtual ports:  %u\n", rte_eth_dev_count_avail());

    const EthernetEnvelope market_envelope = make_market_envelope();
    const MarketDataMessage market_message = make_market_data_message();

    EthernetFrame encoded_market_frame{};

    if (!encode_market_data_frame(
            market_envelope,
            market_message,
            encoded_market_frame)) {
        std::fprintf(stderr,
                     "PulseBook Phase 4 failure: market frame encoding failed\n");
        close_port(ports.engine);
        close_port(ports.generator);
        rte_eal_cleanup();
        return EXIT_FAILURE;
    }

    rte_mbuf* const market_packet =
        frame_to_mbuf(mempool, encoded_market_frame);

    if (market_packet == nullptr) {
        std::fprintf(stderr,
                     "PulseBook Phase 4 failure: market mbuf allocation failed\n");
        close_port(ports.engine);
        close_port(ports.generator);
        rte_eal_cleanup();
        return EXIT_FAILURE;
    }

    if (!transmit_single_packet(ports.generator, market_packet)) {
        std::fprintf(stderr,
                     "PulseBook Phase 4 failure: market packet TX failed\n");
        close_port(ports.engine);
        close_port(ports.generator);
        rte_eal_cleanup();
        return EXIT_FAILURE;
    }

    rte_mbuf* const engine_received_packet =
        receive_single_packet(ports.engine);

    if (engine_received_packet == nullptr) {
        std::fprintf(stderr,
                     "PulseBook Phase 4 failure: engine RX timed out\n");
        close_port(ports.engine);
        close_port(ports.generator);
        rte_eal_cleanup();
        return EXIT_FAILURE;
    }

    if (rte_pktmbuf_data_len(engine_received_packet) <
        kEthernetFrameBytesWithoutFcs) {
        std::fprintf(stderr,
                     "PulseBook Phase 4 failure: received short market frame\n");
        rte_pktmbuf_free(engine_received_packet);
        close_port(ports.engine);
        close_port(ports.generator);
        rte_eal_cleanup();
        return EXIT_FAILURE;
    }

    EthernetEnvelope decoded_market_envelope{};
    MarketDataMessage decoded_market_message{};

    const FrameDecodeError market_decode_result = decode_market_data_frame(
        mbuf_frame_bytes(engine_received_packet),
        decoded_market_envelope,
        decoded_market_message);

    rte_pktmbuf_free(engine_received_packet);

    if (market_decode_result != FrameDecodeError::none) {
        std::fprintf(stderr,
                     "PulseBook Phase 4 failure: received market frame invalid, code=%u\n",
                     static_cast<unsigned int>(market_decode_result));
        close_port(ports.engine);
        close_port(ports.generator);
        rte_eal_cleanup();
        return EXIT_FAILURE;
    }

    const OutboundOrderMessage outbound_order =
        make_test_order_from_market_data(decoded_market_message);

    const EthernetEnvelope order_envelope =
        make_response_envelope(decoded_market_envelope);

    EthernetFrame encoded_order_frame{};

    if (!encode_order_frame(
            order_envelope,
            outbound_order,
            encoded_order_frame)) {
        std::fprintf(stderr,
                     "PulseBook Phase 4 failure: order frame encoding failed\n");
        close_port(ports.engine);
        close_port(ports.generator);
        rte_eal_cleanup();
        return EXIT_FAILURE;
    }

    rte_mbuf* const order_packet =
        frame_to_mbuf(mempool, encoded_order_frame);

    if (order_packet == nullptr) {
        std::fprintf(stderr,
                     "PulseBook Phase 4 failure: order mbuf allocation failed\n");
        close_port(ports.engine);
        close_port(ports.generator);
        rte_eal_cleanup();
        return EXIT_FAILURE;
    }

    if (!transmit_single_packet(ports.engine, order_packet)) {
        std::fprintf(stderr,
                     "PulseBook Phase 4 failure: order packet TX failed\n");
        close_port(ports.engine);
        close_port(ports.generator);
        rte_eal_cleanup();
        return EXIT_FAILURE;
    }

    rte_mbuf* const generator_received_packet =
        receive_single_packet(ports.generator);

    if (generator_received_packet == nullptr) {
        std::fprintf(stderr,
                     "PulseBook Phase 4 failure: generator RX timed out\n");
        close_port(ports.engine);
        close_port(ports.generator);
        rte_eal_cleanup();
        return EXIT_FAILURE;
    }

    EthernetEnvelope decoded_order_envelope{};
    OutboundOrderMessage decoded_order{};

    const FrameDecodeError order_decode_result = decode_order_frame(
        mbuf_frame_bytes(generator_received_packet),
        decoded_order_envelope,
        decoded_order);

    rte_pktmbuf_free(generator_received_packet);

    if (order_decode_result != FrameDecodeError::none) {
        std::fprintf(stderr,
                     "PulseBook Phase 4 failure: received order frame invalid, code=%u\n",
                     static_cast<unsigned int>(order_decode_result));
        close_port(ports.engine);
        close_port(ports.generator);
        rte_eal_cleanup();
        return EXIT_FAILURE;
    }

    const bool packet_data_correct =
        decoded_market_message.sequence_number == market_message.sequence_number &&
        decoded_market_message.instrument_id == market_message.instrument_id &&
        decoded_market_message.price_ticks == market_message.price_ticks &&
        decoded_market_message.quantity == market_message.quantity &&
        decoded_order.instrument_id == market_message.instrument_id &&
        decoded_order.price_ticks == market_message.price_ticks &&
        decoded_order.quantity == 10 &&
        decoded_order.side == Side::buy;

    if (!packet_data_correct) {
        std::fprintf(stderr,
                     "PulseBook Phase 4 failure: packet field validation failed\n");
        close_port(ports.engine);
        close_port(ports.generator);
        rte_eal_cleanup();
        return EXIT_FAILURE;
    }

    rte_eth_stats generator_stats{};
    rte_eth_stats engine_stats{};

    rte_eth_stats_get(ports.generator, &generator_stats);
    rte_eth_stats_get(ports.engine, &engine_stats);

    std::printf("\nVirtual packet flow completed successfully.\n");
    std::printf("Market sequence received: %u\n",
                decoded_market_message.sequence_number);
    std::printf("Instrument ID received:   %u\n",
                decoded_market_message.instrument_id);
    std::printf("Market price ticks:        %lld\n",
                static_cast<long long>(decoded_market_message.price_ticks));
    std::printf("Order client ID:           %llu\n",
                static_cast<unsigned long long>(decoded_order.client_order_id));
    std::printf("Order quantity:            %u\n",
                decoded_order.quantity);
    std::printf("Generator TX packets:      %llu\n",
                static_cast<unsigned long long>(generator_stats.opackets));
    std::printf("Generator RX packets:      %llu\n",
                static_cast<unsigned long long>(generator_stats.ipackets));
    std::printf("Engine RX packets:         %llu\n",
                static_cast<unsigned long long>(engine_stats.ipackets));
    std::printf("Engine TX packets:         %llu\n",
                static_cast<unsigned long long>(engine_stats.opackets));
    std::printf("Frame bytes:               %zu\n",
                kEthernetFrameBytesWithoutFcs);
    std::printf("Status:                    OK\n");

    close_port(ports.engine);
    close_port(ports.generator);

    const int cleanup_result = rte_eal_cleanup();

    if (cleanup_result != 0) {
        std::fprintf(stderr,
                     "PulseBook Phase 4 failure: EAL cleanup failed\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

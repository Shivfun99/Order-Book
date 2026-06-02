# PulseBook Level 3 Phase 4: Virtual DPDK Packet Path

## Purpose

Validate PulseBook Ethernet frames inside DPDK packet buffers before using a
physical NIC or integrating the trading engine.

## Virtual Topology

Two software-backed DPDK Ethernet ports are created with the Ring PMD.

Generator Port TX -> market_data_ring -> Engine Port RX

Engine Port TX -> outbound_order_ring -> Generator Port RX

## Test Sequence

1. Encode one PulseBook market-data Ethernet frame.
2. Allocate one rte_mbuf from the DPDK mbuf pool.
3. Copy the frame into the mbuf.
4. Transmit the mbuf from the generator virtual port.
5. Receive the mbuf at the engine virtual port.
6. Decode and validate the market-data frame.
7. Construct one deterministic outbound-order Ethernet frame.
8. Transmit the order mbuf from the engine virtual port.
9. Receive and decode it at the generator virtual port.
10. Validate key order fields and display virtual-port statistics.

## Scope

This validates:

- DPDK EAL;
- DPDK mbuf pools;
- DPDK virtual Ethernet ports;
- DPDK RX and TX burst APIs;
- PulseBook fixed Ethernet packet format.

This does not validate:

- physical NIC RX or TX;
- vfio-pci binding;
- kernel bypass through a physical NIC;
- physical wire latency;
- trading strategy output;
- exchange acknowledgment round-trip latency.

## Later Replacement

In the engine integration phase, deterministic order construction is replaced by:

Decoded MarketDataMessage
  -> MarketUpdate adapter
  -> TradingEngine::on_market_update()
  -> emitted OrderRequest
  -> OutboundOrderMessage
  -> Ethernet order frame

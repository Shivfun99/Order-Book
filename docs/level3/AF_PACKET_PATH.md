# PulseBook Level 3 Phase 9B: DPDK AF_PACKET Path

## Hardware Decision

The development laptop does not expose a suitable wired PCIe Ethernet NIC for
a VFIO-backed physical DPDK benchmark.

Detected networking:

- PCIe Realtek RTL8821CE Wi-Fi adapter.
- USB RNDIS phone-tether interface.

Neither is used as a physical PulseBook DPDK benchmark device.

## Safe Functional Network Path

A private Linux veth pair is created:

pb_peer <-> pb_eng

The Linux raw packet generator transmits fixed PulseBook Ethernet frames from
pb_peer. The DPDK AF_PACKET PMD attaches to pb_eng and processes the frames
through the actual TradingEngine.

## Packet Path

pulsebook_afpacket_generator
  -> raw Ethernet market-data frame on pb_peer
  -> Linux veth pair
  -> DPDK AF_PACKET RX on pb_eng
  -> decode_market_data_frame()
  -> EngineWireAdapter
  -> TradingEngine
  -> encode_order_frame()
  -> DPDK AF_PACKET TX on pb_eng
  -> Linux veth pair
  -> generator validates outbound order

## Measurement Classification

This is a kernel-backed DPDK AF_PACKET functional network path.

It is not:

- a VFIO-bound physical NIC benchmark;
- physical Ethernet wire latency;
- exchange acknowledgement latency;
- network round-trip latency.

# PulseBook Level 3 Phase 9B: AF_PACKET Network Path

## Hardware Result

The current laptop does not expose a suitable wired PCIe Ethernet NIC for a
true hardware-bypass DPDK benchmark.

Detected interfaces:

- wlp1s0: PCIe Wi-Fi adapter, not used for PulseBook DPDK hardware testing.
- enxb632794aaaef: USB RNDIS phone-tether interface, not treated as a native
  DPDK hardware NIC.

## Safe Network-Facing Development Path

A private Linux veth pair is used:

pb_peer <-> pb_eng

The raw Linux packet generator transmits PulseBook Ethernet frames through
pb_peer. The DPDK AF_PACKET PMD attaches to pb_eng and processes the market
frames through the actual TradingEngine path.

## Data Path

pulsebook_afpacket_generator
  -> Linux AF_PACKET raw TX on pb_peer
  -> Linux veth pair
  -> DPDK AF_PACKET RX on pb_eng
  -> decode_market_data_frame()
  -> EngineWireAdapter
  -> TradingEngine
  -> encode_order_frame()
  -> DPDK AF_PACKET TX on pb_eng
  -> Linux veth pair
  -> generator validates outbound order

## Benchmark Classification

This path is kernel-backed. It validates real Linux-interface packet ingress
and egress through a DPDK PMD, but it is not:

- VFIO-bound physical NIC kernel bypass;
- physical wire ingress-to-egress latency;
- exchange network round-trip latency.

## Future Real-NIC Upgrade

A proper physical benchmark requires a supported wired NIC, an external packet
peer or second usable port, and an honest latency measurement method.

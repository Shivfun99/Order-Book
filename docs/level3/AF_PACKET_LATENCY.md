# PulseBook Level 3 Phase 9C: AF_PACKET Latency Benchmark

## Purpose

Measure application-side latency after moving from software Ring PMD transport
to a Linux-interface DPDK AF_PACKET transport over a private veth pair.

## Transport Topology

pulsebook_afpacket_benchmark_generator on pb_peer
  -> Linux veth
  -> pb_eng
  -> DPDK AF_PACKET PMD RX
  -> PulseBook TradingEngine
  -> DPDK AF_PACKET PMD TX
  -> Linux veth
  -> generator validates outbound order

## Timed Boundary

Timing begins immediately after rte_eth_rx_burst() returns a valid
market-data packet to the AF_PACKET engine.

Timing ends immediately after rte_eth_tx_burst() accepts the generated
outbound order packet.

## Included in Timing

- Ethernet/wire market-data decoding.
- EngineWireAdapter conversion.
- Fixed L2 order-book update.
- Imbalance strategy evaluation.
- Risk validation.
- Preallocated outbox interaction.
- Outbound order wire conversion.
- Ethernet order-frame encoding.
- TX mbuf allocation and frame copy.
- AF_PACKET TX enqueue.

## Excluded from Timing

- Generator packet creation and TX.
- Kernel/veth travel before DPDK RX returns.
- Generator-side returned-order validation.
- Physical NIC or wire latency.
- Exchange network or acknowledgement RTT.

## Workload

- 10 balanced seed packets.
- 100,000 warmup order-producing packets.
- 1,000,000 measured BUY order-producing packets.
- One response validation for each generated order.

## Classification

This is kernel-backed DPDK AF_PACKET application-side RX-to-TX-enqueue
latency over a Linux veth transport.

It is not a physical-NIC VFIO bypass benchmark.

# PulseBook Level 3 Final Results

## Overview

PulseBook is a C++20 low-latency electronic trading packet processor using a
fixed-size binary Ethernet protocol, fixed-depth L2 order book,
imbalance-based execution strategy, inline risk validation, preallocated order
handling and DPDK-based packet processing.

## Verified Packet Path

62-byte market-data Ethernet frame
  -> DPDK RX
  -> fixed-offset market-data decoder
  -> MarketUpdate adapter
  -> TradingEngine
  -> L2 order-book update
  -> imbalance strategy
  -> risk guard
  -> outbound order generation
  -> fixed-size Ethernet order encoder
  -> DPDK TX

## Correctness Validation

Validated processing outcomes:

- BUY order generation.
- SELL order generation.
- NO_SIGNAL balanced-book behavior.
- RISK_REJECT behavior.
- INVALID_FRAME filtering.

## DPDK Ring PMD Results

Measured boundary:

rte_eth_rx_burst() return -> trading/order processing -> rte_eth_tx_burst() accept

| Core | Events | p50 | p95 | p99 | p99.9 | Failures |
|---:|---:|---:|---:|---:|---:|---:|
| CPU 0 | 1,000,000 | 110.844 ns | 248.397 ns | 552.217 ns | 706.464 ns | 0 |
| CPU 2 | 1,000,000 | 112.847 ns | 254.407 ns | 550.214 ns | 754.541 ns | 0 |

Classification: virtual DPDK Ring PMD application-side packet-processing
latency.

## DPDK AF_PACKET Results

Topology:

Linux raw generator on pb_peer
  -> private veth pair
  -> pb_eng
  -> DPDK AF_PACKET RX
  -> TradingEngine
  -> DPDK AF_PACKET TX
  -> generator validates returned order

| Core | Events | p50 | p95 | p99 | p99.9 | Failures |
|---:|---:|---:|---:|---:|---:|---:|
| CPU 0 | 1,000,000 | 1.737 us | 2.594 us | 3.262 us | 10.542 us | 0 |
| CPU 2 | 1,000,000 | 1.832 us | 2.903 us | 7.822 us | 22.061 us | 0 |

Classification: kernel-backed DPDK AF_PACKET application-side
RX-to-TX-enqueue latency over a private Linux veth pair.

## Hardware Limitation

The development laptop does not expose a suitable wired PCIe Ethernet NIC for
a VFIO-backed physical DPDK benchmark. Therefore no claim is made regarding:

- physical NIC wire latency;
- VFIO-backed hardware bypass;
- switch/network latency;
- exchange acknowledgement round-trip latency.

## Best Honest Claim

PulseBook achieved 110.8 ns p50 / 552.2 ns p99 on a virtual DPDK Ring PMD
application-side RX-to-TX path and 1.74 us p50 / 3.26 us p99 on a
kernel-backed DPDK AF_PACKET path over an isolated Linux veth link, each
across one million measured order-producing events with zero failures.

# PulseBook Level 3 Phase 6: Virtual DPDK Latency Benchmark

## Benchmark Classification

This benchmark measures virtual DPDK application-side packet processing
latency.

It does not measure physical NIC latency or exchange/network round-trip
latency.

## Timed Boundary

Timing starts immediately after the engine port has received one market-data
packet using rte_eth_rx_burst().

Timing ends immediately after the generated outbound order packet is accepted
by rte_eth_tx_burst() on the engine virtual port.

## Included Operations

- PulseBook Ethernet market-data frame decode.
- Wire message to MarketUpdate conversion.
- TradingEngine invocation.
- Fixed L2 book update.
- Imbalance strategy evaluation.
- Risk validation.
- Preallocated outbox push/pop.
- Outbound wire-order conversion.
- Ethernet order-frame encoding.
- TX rte_mbuf allocation from a preallocated DPDK mempool.
- Copying the outbound 62-byte frame into the TX mbuf.
- DPDK TX enqueue.

## Excluded Operations

- Generator-side market packet creation.
- Generator-side virtual transmission.
- Engine RX polling wait before a packet is returned.
- Generator-side order drain after transmission.
- Physical Ethernet NIC ingress or egress.
- Physical wire transmission time.
- Exchange/network round-trip time.
- Order acknowledgment from a real venue.

## Traffic Scenario

The L2 book is initialized with five balanced bid and ask levels.

Each warmup and measured packet then modifies bid level zero to a high
quantity, creating strong bid-side imbalance and generating one BUY order.

## Measurement Parameters

- Timer overhead samples: 100,000
- Warmup order events: 100,000
- Measured order events: 1,000,000
- Fixed Ethernet frame size: 62 bytes
- Processing core: one EAL lcore

# PulseBook Level 3 Phase 8: Regime-Specific Virtual DPDK Benchmark

## Purpose

Measure separate application-side latencies for no-signal, BUY and SELL
processing paths.

## Why Results Are Reported Separately

A no-signal market update produces no outbound order packet.

Therefore:

- NO_SIGNAL measures RX-return to strategy no-order decision.
- BUY measures RX-return to outbound BUY TX enqueue.
- SELL measures RX-return to outbound SELL TX enqueue.

These paths must not be merged into one percentile distribution without clearly
describing the workload mixture.

## Test Setup

- DPDK Ring PMD virtual ports.
- One EAL lcore.
- Fixed 62-byte Ethernet frames.
- Five visible bid levels and five visible ask levels.
- A separate initialized engine is used for each regime.
- 100,000 warmup events per regime.
- 1,000,000 measured events per regime.

## NO_SIGNAL Regime

The book remains close to balanced by modifying bid level zero between
quantities 10 and 11.

Expected result:

- no outbound order packet;
- engine returns no-order decision.

## BUY Regime

Bid level zero is modified between quantities 1000 and 1001 while ask
liquidity remains small.

Expected result:

- one BUY order per market-data event;
- limit price equals best ask.

## SELL Regime

Ask level zero is modified between quantities 1000 and 1001 while bid
liquidity remains small.

Expected result:

- one SELL order per market-data event;
- limit price equals best bid.

## Measurement Limitations

This benchmark measures only virtual application-side DPDK processing using
Ring PMD ports.

It does not measure:

- physical NIC ingress;
- physical NIC egress;
- Ethernet wire latency;
- switch latency;
- exchange acknowledgment latency;
- end-to-end order round-trip time.

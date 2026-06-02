# PulseBook Level 3 Phase 7: Scenario Validation

## Purpose

Validate the functional behavior of the virtual DPDK packet path before
moving to hardware-backed NIC tests.

## Tested Scenarios

### NO_SIGNAL

A balanced five-level bid and ask book is constructed.

Expected outcome:

- all market-data frames decode correctly;
- no outbound order frame is transmitted.

### BUY

A balanced book is constructed, then bid level zero is modified to a large
quantity.

Expected outcome:

- strategy emits BUY;
- generated price is best ask;
- one outbound BUY Ethernet order frame is transmitted.

### SELL

A balanced book is constructed, then ask level zero is modified to a large
quantity.

Expected outcome:

- strategy emits SELL;
- generated price is best bid;
- one outbound SELL Ethernet order frame is transmitted.

### RISK_REJECT

A balanced book is constructed using a RiskGuard limit lower than the
strategy-generated order quantity.

Expected outcome:

- strategy reaches an order decision;
- risk validation rejects it;
- no outbound order frame is transmitted.

### INVALID_FRAME

A market-data Ethernet frame is injected with the wrong EtherType.

Expected outcome:

- packet is rejected by the Ethernet decoder;
- TradingEngine is not invoked;
- no outbound order frame is transmitted.

## Expected Counters

| Counter | Expected |
|---|---:|
| Market frames injected | 44 |
| Valid frames decoded | 43 |
| Invalid frames dropped | 1 |
| No-signal results | 40 |
| BUY orders emitted | 1 |
| SELL orders emitted | 1 |
| Risk rejected | 1 |
| Generator TX packets | 44 |
| Generator RX packets | 2 |
| Engine RX packets | 44 |
| Engine TX packets | 2 |

## Measurement Status

This phase validates correctness through DPDK virtual Ring PMD ports.

It does not claim:

- physical NIC latency;
- wire ingress-to-egress latency;
- exchange acknowledgment round-trip latency.

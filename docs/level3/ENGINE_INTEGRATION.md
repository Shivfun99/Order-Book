# PulseBook Level 3 Phase 5: Engine Integration

## Data Path

Generator virtual DPDK port
  -> market-data Ethernet frame
  -> Engine virtual DPDK port
  -> frame decoder
  -> EngineWireAdapter
  -> TradingEngine
  -> FixedL2Book
  -> ImbalanceStrategy
  -> RiskGuard
  -> PreallocatedOutbox
  -> EngineWireAdapter
  -> outbound-order Ethernet frame
  -> Generator virtual DPDK port

## Current Deterministic Scenario

Initial balanced book:

- Five ask levels, quantity 10 at each level.
- Five bid levels, quantity 10 at each level.
- No outbound order expected.

Trigger:

- Modify bid level zero to quantity 1000.
- BUY order expected.

Expected generated order:

- side: BUY
- instrument ID: 77
- limit price: 100100 ticks
- quantity: 10
- client order ID: 1

## Current Measurement Classification

This phase validates functional packet processing through virtual DPDK ports.

It does not measure:

- physical NIC ingress-to-egress latency;
- real hardware PMD latency;
- network round-trip latency;
- exchange acknowledgment latency.

## Next Phase

Phase 6 will add packet-path timing around:

RX packet available from DPDK
  -> decode
  -> TradingEngine
  -> order encode
  -> successful DPDK TX enqueue

# PulseBook  Project Details

## What is PulseBook?

**PulseBook** is a C++20 + DPDK low-latency trading-engine project.

The simple idea is:

```text
market-data packet comes in
    -> update the order book
    -> decide BUY / SELL / do nothing
    -> check risk
    -> send an order packet out
```

This project is not a full trading firm system. It is a focused low-latency systems project to understand the trading hot path and how fast packet-to-order processing can be when the code is kept small and predictable.

---

## Why I built it

I wanted to learn what actually matters in low-latency trading systems.

So instead of building a big backend with APIs, databases, dashboards, queues, and services, I focused only on the hot path:

```text
packet decode
    -> book update
    -> strategy
    -> risk
    -> order encode
    -> TX enqueue
```

The goal of V1 was to build a real packet-processing engine and benchmark it honestly.

---

## What PulseBook does

PulseBook currently supports:

- fixed-size binary market-data packets;
- fixed-size outbound order packets;
- 62-byte Ethernet-style frames;
- an in-memory L2 order book;
- a simple imbalance-based strategy;
- BUY, SELL, and NO_SIGNAL decisions;
- inline risk checks;
- preallocated order handling;
- DPDK Ring PMD virtual packet processing;
- DPDK AF_PACKET processing over a private Linux `veth` pair;
- correctness tests for normal and invalid cases;
- latency benchmarks over 1,000,000 measured order-producing events.

---

## Architecture

```text
Inbound market-data Ethernet frame
        |
        v
DPDK RX / rte_mbuf
        |
        v
Fixed binary decoder
        |
        v
MarketUpdate
        |
        v
TradingEngine
        |
        +--> L2 Order Book
        |
        +--> Imbalance Strategy
        |
        +--> Risk Guard
        |
        v
Preallocated order path
        |
        v
Outbound order encoder
        |
        v
DPDK TX enqueue
```

In easy words:

```text
PulseBook receives a small market packet,
updates bid/ask state,
checks if one side is much stronger,
creates an order if needed,
checks if the order is safe,
then sends it out.
```

---

## Strategy logic

The strategy is intentionally simple.

```text
More bid pressure  -> BUY
More ask pressure  -> SELL
Balanced book      -> NO_SIGNAL
Risk problem       -> RISK_REJECT
Bad packet         -> INVALID_FRAME
```

This strategy is not meant to be profitable. It is used as a controlled workload to test the packet-processing path.

---

## Why the design is low latency

The main design decisions were:

- fixed-size packets;
- no JSON or variable-size parsing;
- integer price ticks instead of floating-point prices;
- fixed-depth L2 order book;
- preallocated order/outbox memory;
- no logging inside the measured path;
- no database or network API inside the hot path;
- single-core run-to-completion processing;
- DPDK RX/TX style packet handling.

The biggest lesson was that low latency mostly came from **avoiding unnecessary work**.

---

## What mattered most

These things mattered the most:

| Design choice | Why it helped |
|---|---|
| Fixed 62-byte Ethernet frames | No dynamic parsing |
| Integer price ticks | Fast and deterministic |
| Fixed L2 book | Predictable memory access |
| Preallocated order path | Avoids heap allocation |
| Simple strategy | Keeps decision path small |
| Inline risk checks | Safety without service calls |
| No hot-path logging | Avoids unpredictable slowdowns |
| Single-core processing | Avoids locks and cross-core coordination |

---

## What did not matter much yet

For V1, these were not the main focus:

- complex trading strategy;
- multi-threading;
- databases;
- REST APIs;
- dashboards;
- Kafka or Redis;
- real exchange connectivity;
- physical NIC tuning.

Those may matter later, but they are not part of the current hot-path benchmark.

---

## Correctness validation

PulseBook validates these scenarios:

| Scenario | Meaning |
|---|---|
| NO_SIGNAL | Balanced book, no order should be sent |
| BUY | Bid side is stronger, send BUY |
| SELL | Ask side is stronger, send SELL |
| RISK_REJECT | Strategy wants to trade, but risk blocks it |
| INVALID_FRAME | Bad packet is dropped before engine logic |

Example validated output:

```text
Market packets processed:  11
No-signal events:          10
Orders emitted:            1
Generated order side:      BUY
Generated order price:     100100
Generated order quantity:  10
Status:                    OK
```

---

## DPDK paths implemented

PulseBook has two packet-processing paths.

### 1. DPDK Ring PMD path

This is a virtual in-process DPDK path.

```text
Generator virtual port
    -> DPDK Ring PMD
    -> Engine virtual port
    -> PulseBook engine
    -> DPDK Ring PMD
    -> Generator receives order
```

This gives a clean software baseline for the engine.

Correct label:

```text
Virtual DPDK Ring PMD application-side RX-to-TX latency.
```

### 2. DPDK AF_PACKET path over private `veth`

This uses a private Linux virtual Ethernet pair:

```text
pb_peer <----------------> pb_eng
```

Flow:

```text
Linux raw generator on pb_peer
    -> Linux veth
    -> pb_eng
    -> DPDK AF_PACKET RX
    -> PulseBook engine
    -> DPDK AF_PACKET TX
    -> pb_peer
    -> generator validates returned order
```

Correct label:

```text
Kernel-backed DPDK AF_PACKET application-side RX-to-TX-enqueue latency.
```

---

## Important measurement scope

The numbers below are **not** physical NIC latency.

They do not include:

- physical NIC ingress;
- physical NIC egress;
- switch latency;
- hardware timestamping;
- exchange acknowledgement;
- network round-trip time;
- real market-data burst queueing.

They measure the controlled application-side path only.

That means:

```text
Good claim:
application-side packet-processing latency

Bad claim:
exchange latency / wire latency / physical NIC latency
```

---

## Verified latency results

### Ring PMD virtual path

Measured boundary:

```text
after rte_eth_rx_burst() returned packet
    -> decode
    -> book update
    -> strategy/risk
    -> order encode
    -> rte_eth_tx_burst() accepted order
```

| Core | p50 | p95 | p99 | p99.9 | Events | Failures |
|---:|---:|---:|---:|---:|---:|---:|
| CPU 0 | 110.844 ns | 248.397 ns | 552.217 ns | 706.464 ns | 1,000,000 | 0 |
| CPU 2 | 112.847 ns | 254.407 ns | 550.214 ns | 754.541 ns | 1,000,000 | 0 |

Best short summary:

```text
~111 ns p50 / ~552 ns p99
1,000,000 measured order events
0 failures
```

### Regime-specific Ring PMD path

This separates BUY and SELL paths.

| Path | p50 | p95 | p99 | p99.9 | Failures |
|---|---:|---:|---:|---:|---:|
| BUY packet-out path | 89.476 ns | 234.375 ns | 508.146 ns | 713.809 ns | 0 |
| SELL packet-out path | 90.812 ns | 228.365 ns | 498.130 ns | 647.035 ns | 0 |

This run completed with:

```text
NO_SIGNAL decisions:   1,100,000
BUY orders:            1,100,000
SELL orders:           1,100,000
Total failures:        0
Status:                OK
```

### AF_PACKET over private `veth`

Measured boundary:

```text
after AF_PACKET rte_eth_rx_burst() returned packet
    -> decode
    -> book update
    -> strategy/risk
    -> order encode
    -> AF_PACKET rte_eth_tx_burst() accepted order
```

| Core | p50 | p95 | p99 | p99.9 | Events | Failures |
|---:|---:|---:|---:|---:|---:|---:|
| CPU 0 | 1.737 µs | 2.594 µs | 3.262 µs | 10.542 µs | 1,000,000 | 0 |
| CPU 2 | 1.832 µs | 2.903 µs | 7.822 µs | 22.061 µs | 1,000,000 | 0 |

Best short summary:

```text
1.737 µs p50 / 3.262 µs p99 on CPU 0
1,000,000 measured order events
0 failures
```

---

## Hardware limitation

The current laptop does not have a suitable wired PCIe Ethernet NIC for a true DPDK/VFIO physical benchmark.

Detected network hardware:

- Realtek PCIe Wi-Fi adapter;
- USB RNDIS phone tethering.

So I did **not** bind any real network device to VFIO, and I do **not** claim physical NIC latency.

Current V1 uses:

```text
Ring PMD      -> clean virtual DPDK baseline
AF_PACKET     -> kernel-backed Linux interface path
```

Future hardware goal:

```text
Supported wired NIC
    -> VFIO binding
    -> external peer / timestamping
    -> real wire-to-wire measurement
```

---

## Engineering problems solved

During the build, I had to fix real low-level issues:

### Ring PMD linking

Problem:

```text
undefined reference to rte_eth_from_rings
```

Fix:

```text
Explicitly link librte_net_ring.so.
```

### Release tests

Problem:

```text
assert() was disabled in Release builds.
```

Fix:

```text
Compile tests with assertions enabled and warnings as errors.
```

### DPDK object names

Problem:

```text
mempool/ring names were too long.
```

Fix:

```text
Use short DPDK object names.
```

### AF_PACKET mbuf size

Problem:

```text
2016 bytes will not fit in mbuf (1920 bytes)
```

Fix:

```text
Use 4096-byte mbuf data rooms for AF_PACKET apps.
```

### Missing veth interface

Problem:

```text
SIOCGIFINDEX: No such device
pb_peer was not found
```

Fix:

```text
Create pb_peer <-> pb_eng before every AF_PACKET run.
```

---

## Repository structure

```text
.
├── apps/
│   ├── pulsebook_dpdk_scenarios.cpp
│   ├── pulsebook_dpdk_latency_benchmark.cpp
│   ├── pulsebook_dpdk_regime_benchmark.cpp
│   ├── pulsebook_dpdk_afpacket_engine.cpp
│   ├── pulsebook_afpacket_generator.cpp
│   ├── pulsebook_dpdk_afpacket_latency.cpp
│   └── pulsebook_afpacket_benchmark_generator.cpp
├── include/pulsebook/
│   ├── book/
│   ├── common/
│   ├── dpdk/
│   ├── engine/
│   ├── order/
│   ├── risk/
│   ├── strategy/
│   └── wire/
├── tests/
├── scripts/level3/
├── docs/level3/
├── results/level3/
├── CMakeLists.txt
├── README.md
├── real_run.md
└── details.md
```

---

## Main executables

| Executable | Purpose |
|---|---|
| `pulsebook_dpdk_scenarios` | Runs correctness scenarios |
| `pulsebook_dpdk_latency_benchmark` | Ring PMD BUY-path benchmark |
| `pulsebook_dpdk_regime_benchmark` | Separate NO_SIGNAL / BUY / SELL benchmark |
| `pulsebook_dpdk_afpacket_engine` | AF_PACKET functional engine |
| `pulsebook_afpacket_generator` | Sends 11 test frames and validates returned order |
| `pulsebook_dpdk_afpacket_latency` | AF_PACKET latency engine |
| `pulsebook_afpacket_benchmark_generator` | Feeds 1M AF_PACKET benchmark events |

---

## How to reproduce

Use:

```text
real_run.md
```

That file contains the exact commands for:

- building the project;
- running tests;
- running Ring PMD validation;
- running Ring PMD benchmarks;
- creating the private AF_PACKET `veth` pair;
- running AF_PACKET functional checks;
- running AF_PACKET latency benchmarks;
- saving result logs.

---

## What I can honestly claim

Short version:

```text
Built PulseBook, a C++20/DPDK trading packet processor with fixed 62-byte
Ethernet frames, L2 book processing, imbalance strategy, risk checks and
preallocated order handling. Achieved 110.844 ns p50 / 552.217 ns p99 on a
virtual DPDK Ring PMD path and 1.737 µs p50 / 3.262 µs p99 on a kernel-backed
DPDK AF_PACKET path over private veth, each across 1M measured order events
with zero failures.
```

Resume version:

```text
Built PulseBook, a C++20/DPDK low-latency trading engine with fixed 62-byte
Ethernet frames, L2 order-book processing, imbalance-driven execution and
inline risk checks; achieved 110.8 ns p50 / 552.2 ns p99 on virtual Ring PMD
and 1.74 µs p50 / 3.26 µs p99 on kernel-backed AF_PACKET RX-to-TX processing
across 1M zero-failure order events.
```

---

## What I should not claim

I should not say:

- physical NIC latency;
- exchange latency;
- wire-to-wire latency;
- real market-data burst p99;
- kernel-bypass physical NIC result;
- production trading-system performance.

Correct wording:

```text
controlled engine/path benchmark
application-side RX-to-TX processing
virtual DPDK Ring PMD
kernel-backed AF_PACKET over veth
```

---

## Future work

The most meaningful next steps are:

1. replay real or high-rate market data;
2. measure queue buildup and real tail latency under bursts;
3. add multi-symbol books;
4. add fills, cancels and replace handling;
5. test AF_XDP and compare with AF_PACKET;
6. run on a machine with a supported wired DPDK NIC;
7. add hardware timestamps or external timestamping for wire-to-wire measurement;
8. isolate CPU cores and tune governor/IRQ settings for more stable results.

---

## Final summary

PulseBook V1 proves that a small C++20 trading engine can process fixed binary market-data packets through DPDK-style paths and emit validated outbound order packets with very low application-side latency.

Best verified numbers:

```text
Ring PMD virtual path:
    110.844 ns p50 / 552.217 ns p99
    1,000,000 measured order events
    0 failures

AF_PACKET over private veth:
    1.737 µs p50 / 3.262 µs p99
    1,000,000 measured order events
    0 failures
```

The project is strongest when described honestly:

```text
A low-latency trading-engine hot-path project with DPDK packet processing,
not a physical exchange-latency benchmark yet.
```

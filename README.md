# PulseBook Level 3 — Project Details, Architecture and Verified Results

## Project Overview

**PulseBook** is a C++20 low-latency electronic trading packet processor. It receives compact market-data messages, updates an in-memory order book, makes a BUY / SELL / NO_SIGNAL decision, applies pre-trade risk checks, and emits a compact outbound order packet.

The Level 3 goal was to move from a purely in-memory trading decision engine to an engine that processes **real Ethernet-shaped packets through DPDK RX/TX APIs**.

In simple words:

```text
Market price/depth packet comes in
    -> PulseBook reads it very quickly
    -> updates its view of buyers and sellers
    -> decides whether to buy, sell, or do nothing
    -> checks whether the order is safe
    -> sends an order packet out
```

PulseBook was designed as a focused low-latency systems project. It intentionally avoids web APIs, databases, Docker, Kafka, frontend code, and other features that do not belong in the trading hot path.

---

## Final Honest Project Claim

> Built **PulseBook**, a C++20/DPDK low-latency electronic trading packet processor with fixed 62-byte Ethernet market/order frames, an in-memory L2 order book, imbalance-based BUY/SELL strategy, inline risk validation, and preallocated order handling. Achieved **110.844 ns p50 / 552.217 ns p99** on a virtual DPDK Ring PMD RX-to-TX application path and **1.737 µs p50 / 3.262 µs p99** on a kernel-backed DPDK AF_PACKET RX-to-TX-enqueue path over a private Linux `veth` link, each across **1,000,000 measured order-producing events with zero failures**.

This claim is strong because it is specific and technically honest. It does **not** claim physical NIC latency or exchange round-trip latency.

---

# 1. The Problem PulseBook Solves

Electronic trading systems receive a stream of market updates: bid prices, ask prices and available quantities. A strategy must react to these updates quickly because a trading opportunity may disappear within microseconds.

A normal application stack is not ideal for demonstrating low-latency processing because it often contains:

- dynamic memory allocations;
- slow serialization formats;
- logging inside the decision loop;
- high-level network frameworks;
- blocking operations;
- unnecessary services.

PulseBook solves a smaller, well-defined systems problem:

> How quickly can a compact C++20 trading engine transform an inbound market-data Ethernet packet into an outbound order Ethernet packet while keeping the processing path deterministic and measurable?

---

# 2. What Was Built

PulseBook Level 3 implements:

| Area | Implementation |
|---|---|
| Language | C++20 |
| Build system | CMake + Ninja |
| Packet I/O framework | DPDK 25.11.0 |
| Packet format | Fixed 62-byte Ethernet frames |
| Market representation | Fixed-depth L2 order book |
| Strategy | Visible-depth bid/ask imbalance decision |
| Decisions | BUY, SELL, NO_SIGNAL |
| Risk protection | Quantity, position, notional, outstanding-order and kill-switch style checks |
| Order handling | Fixed-size 32-byte order request and preallocated outbox |
| Fast software transport | DPDK Ring PMD |
| Kernel-backed interface transport | DPDK AF_PACKET PMD over private Linux `veth` |
| Timing method | DPDK timestamp counter cycle readings converted to nanoseconds |
| Correctness scenarios | BUY, SELL, NO_SIGNAL, RISK_REJECT, INVALID_FRAME |

---

# 3. Development Environment

The verified Level 3 runs were executed with:

| Item | Value |
|---|---|
| Machine hostname | `shiv-Inspiron-15-3511` |
| Operating system | Ubuntu 26.04 LTS |
| Kernel | `7.0.0-22-generic` |
| Compiler seen during builds | GNU C++ 15.2.0 |
| DPDK runtime version reported by applications | DPDK 25.11.0 |
| CPU | 11th Gen Intel Core i3-1115G4 @ 3.00 GHz |
| Logical CPUs | 4 |
| TSC frequency reported in latency runs | 2,995,200,000 Hz |
| Benchmark cores used | CPU 0 and CPU 2 |

Important CPU detail: CPU 0 and CPU 2 are sibling logical threads on the same physical core. That means they are useful comparison runs, but they are not independent physical-core benchmarks.

---

# 4. System Architecture

## 4.1 High-Level Data Flow

```text
Inbound market-data Ethernet frame
        |
        v
DPDK RX queue / rte_mbuf
        |
        v
Fixed-offset Ethernet + wire decoder
        |
        v
MarketUpdate adapter
        |
        v
TradingEngine
   |        |        |
   v        v        v
L2 Book  Strategy   Risk Guard
        |
        v
Preallocated Outbox
        |
        v
Outbound OrderRequest
        |
        v
Fixed-offset outbound Ethernet encoder
        |
        v
DPDK TX queue / rte_mbuf
        |
        v
Outbound order Ethernet frame
```

## 4.2 Why This Architecture Is Low Latency

The design keeps the hot path small and predictable:

- packet messages have fixed sizes;
- prices are integer ticks rather than floating point values;
- the order book uses fixed memory;
- generated orders use a fixed-size object;
- the outbox is preallocated;
- processing runs on one selected logical CPU;
- packet decoding and encoding use direct fixed-field handling;
- no database, HTTP API or logging runs in the measured region.

---

# 5. Protocol and Message Design

## 5.1 Ethernet Frames

PulseBook uses compact fixed-length Ethernet-shaped messages.

```text
Fixed frame size used in DPDK benchmarks: 62 bytes
```

Two main packet types exist:

| Packet | Purpose |
|---|---|
| Market-data frame | Carries a bid/ask update into the engine |
| Outbound-order frame | Carries a generated BUY or SELL order out of the engine |

A fixed-size binary format avoids expensive parsing of JSON or variable-size messages.

## 5.2 Market Update Data

A market-data update contains information similar to:

```text
sequence number
exchange timestamp
instrument ID
price in integer ticks
quantity
side: BUY/BID or SELL/ASK
action: add, modify or erase
visible order-book level
flags
```

## 5.3 Order Request Data

The internal generated order object is fixed at:

```text
OrderRequest size: 32 bytes
```

It stores:

```text
client order ID
price ticks
quantity
instrument ID
source market-data sequence
side
order kind
flags
```

This makes it suitable for a predictable low-latency path.

---

# 6. Trading Engine Internals

## 6.1 Fixed L2 Order Book

The L2 book stores visible bid and ask liquidity.

In easy words:

```text
Bids = people willing to buy
Asks = people willing to sell
```

The Level 3 deterministic packet scenarios initialize a five-level visible book on both sides:

```text
Five bid levels
Five ask levels
```

The book can:

- apply an incoming market update;
- update or erase a price/quantity level;
- calculate total visible bid quantity;
- calculate total visible ask quantity;
- locate the best bid;
- locate the best ask.

## 6.2 Imbalance Strategy

The strategy compares total visible bid liquidity with total visible ask liquidity.

Example:

```text
Many more buyers than sellers
    -> bid-side pressure
    -> generate BUY signal

Many more sellers than buyers
    -> ask-side pressure
    -> generate SELL signal

Balanced quantities
    -> NO_SIGNAL
```

For deterministic validation:

```text
Balanced book:
  five ask levels quantity 10
  five bid levels quantity 10
  -> no order

BUY trigger:
  change bid level 0 quantity to 1000
  -> BUY order at best ask

SELL trigger:
  change ask level 0 quantity to 1000
  -> SELL order at best bid
```

## 6.3 Risk Guard

A low-latency engine must still reject unsafe orders.

PulseBook risk checks include:

- maximum order quantity;
- maximum absolute position;
- maximum notional exposure;
- maximum outstanding orders;
- kill-switch style rejection support.

The `RISK_REJECT` test deliberately uses a limit smaller than the strategy-generated quantity. The strategy reaches an order decision, but the risk guard prevents transmission.

## 6.4 Preallocated Outbox

Once an order passes risk checks, it is placed into a preallocated outbox.

Why this matters:

```text
Heap allocation inside the hot path can introduce unpredictable latency.
A fixed preallocated outbox avoids that allocation during normal order generation.
```

---

# 7. DPDK Packet Processing Paths

PulseBook validates two different DPDK paths. They answer different engineering questions.

## 7.1 DPDK Ring PMD Path

Ring PMD creates virtual Ethernet ports backed by DPDK software rings.

Architecture:

```text
Generator virtual port TX
    -> DPDK Ring PMD
    -> Engine virtual port RX
    -> PulseBook TradingEngine
    -> Engine virtual port TX
    -> DPDK Ring PMD
    -> Generator virtual port RX
```

What it proves:

- the complete packet decode → decision → order encode path works with DPDK mbufs;
- the engine can process packets through DPDK RX/TX APIs;
- a very low-overhead application-side software baseline can be measured.

Correct classification:

```text
Virtual DPDK Ring PMD application-side RX-to-TX order-generation latency.
```

It is **not** physical Ethernet or exchange latency.

## 7.2 DPDK AF_PACKET Path over Private `veth`

Because the laptop does not expose a suitable wired PCIe Ethernet port for a physical DPDK NIC test, PulseBook uses a private Linux virtual Ethernet cable:

```text
pb_peer <--------------------------> pb_eng
raw Linux generator                  DPDK AF_PACKET PMD
```

Architecture:

```text
Linux raw Ethernet packet generator on pb_peer
    -> private Linux veth pair
    -> pb_eng
    -> DPDK AF_PACKET PMD RX
    -> PulseBook decoder
    -> TradingEngine
    -> outbound order encoder
    -> DPDK AF_PACKET PMD TX
    -> pb_peer
    -> raw Linux generator validates returned order
```

What it proves:

- PulseBook processes raw Ethernet traffic through a Linux interface-backed DPDK PMD;
- generated order packets are returned and independently validated by the peer generator;
- the system works beyond an entirely in-process virtual ring path.

Correct classification:

```text
Kernel-backed DPDK AF_PACKET application-side RX-to-TX-enqueue latency over an isolated Linux veth link.
```

It is **not** a physical-NIC kernel-bypass result.

---

# 8. Hardware Readiness Finding

A hardware-readiness inspection was performed before attempting to bind any network interface.

## 8.1 What Was Found

| Hardware/OS Item | Result |
|---|---|
| IOMMU / Intel VT-d | Available |
| IOMMU groups visible | 12 |
| VFIO support | Available |
| Secure Boot | Disabled |
| Hugepage mount | Present |
| Allocated hugepages during report | 0 |
| Wired PCIe Ethernet NIC | Not available |
| PCI network device | Realtek RTL8821CE Wi-Fi adapter |
| Other network connection | Vivo USB RNDIS phone tether |

## 8.2 Why a True Hardware DPDK Benchmark Was Not Claimed

The available PCI network device is a Wi-Fi adapter, not a supported dedicated wired Ethernet testing port. The other connection is USB phone tethering. Both were active connectivity paths and were not bound away from Linux.

Therefore, it would be incorrect to say:

```text
PulseBook achieved physical NIC DPDK bypass latency on this laptop.
```

The correct and professional choice was to:

1. keep active network interfaces safe;
2. use Ring PMD for virtual DPDK measurement;
3. use AF_PACKET over an isolated `veth` pair for a kernel-backed network-interface path;
4. clearly state what the numbers do and do not measure.

---

# 9. Phase-by-Phase Achievement Summary

| Phase | Work Completed | Why It Matters |
|---:|---|---|
| 1 | Installed DPDK and verified EAL startup | Confirmed the DPDK runtime works on Ubuntu 26.04 |
| 2 | Created fixed binary market-data and order protocol | Avoided variable parsing overhead |
| 3 | Implemented 62-byte Ethernet encode/decode | Made packet-shaped inputs and outputs deterministic |
| 4 | Built DPDK Ring PMD virtual RX/TX packet transport | Established packet I/O through DPDK APIs |
| 5 | Connected TradingEngine to the DPDK path | Replaced temporary behavior with actual strategy/risk/order flow |
| 6 | Built one-million-event Ring PMD latency benchmark | Produced the first packet-in to packet-out application-side results |
| 7 | Validated BUY, SELL, NO_SIGNAL, RISK_REJECT and INVALID_FRAME | Proved correctness, not just speed |
| 8 | Added regime-specific latency benchmark | Separated no-order, BUY and SELL timing paths |
| 9A | Inspected real NIC, VFIO, IOMMU and hugepage readiness | Prevented incorrect hardware claims or unsafe interface binding |
| 9B | Built AF_PACKET functional path over `veth` | Demonstrated raw packet traffic through a Linux interface-backed PMD |
| 9C | Benchmarked AF_PACKET path for 1M order events | Added a more network-facing, kernel-backed latency result |

---

# 10. Correctness Validation Results

## 10.1 Functional DPDK Engine Path

The DPDK TradingEngine integration successfully processed:

```text
Market packets processed:  11
No-signal events:          10
Orders emitted:            1
Generated order side:      BUY
Generated order price:     100100
Generated order quantity:  10
Status:                    OK
```

## 10.2 Scenario Validation

The validated scenarios are:

| Scenario | Expected Result | Status |
|---|---|---|
| NO_SIGNAL | Balanced book sends no order | Passed |
| BUY | Bid-heavy book emits BUY | Passed |
| SELL | Ask-heavy book emits SELL | Passed |
| RISK_REJECT | Unsafe order is not transmitted | Passed |
| INVALID_FRAME | Bad frame is discarded before engine logic | Passed |

## 10.3 AF_PACKET Functional Path

The raw-packet generator sent eleven market packets and validated one returned BUY order:

| Item | Result |
|---|---:|
| Market packets sent by generator | 11 |
| Valid market packets decoded by engine | 11 |
| No-signal updates | 10 |
| Orders emitted | 1 |
| Outbound orders received by generator | 1 |
| Order side | BUY |
| Order price | 100100 ticks |
| Order quantity | 10 |
| Final status | OK |

The AF_PACKET engine observed a few additional frames and rejected them as invalid/non-PulseBook traffic. This is correct behavior: only valid market-data packets reached trading logic.

---

# 11. Latency Measurement Method

## 11.1 Timer

The DPDK benchmark applications read the CPU timestamp counter and convert cycles to nanoseconds using the measured TSC frequency:

```text
TSC frequency during Level 3 runs: 2,995,200,000 Hz
```

The benchmark collects latency samples into preallocated storage and computes:

- mean;
- p50 / median;
- p95;
- p99;
- p99.9;
- maximum.

Sorting and summary calculations happen after the measured loop rather than inside the hot path.

## 11.2 Warmup and Measurement Workload

For the main order-producing latency runs:

```text
Warmup order events:      100,000
Measured order events:    1,000,000
Result required:          0 processing failures
```

For the AF_PACKET benchmark:

```text
Seed packets:             10 balanced no-signal packets
Warmup order packets:     100,000
Measured order packets:   1,000,000
Returned orders validated by peer generator: every generated order
```

---

# 12. Latency Results

## 12.1 DPDK Ring PMD Virtual Application-Side Path

### Measurement Boundary

```text
Start: after engine-side rte_eth_rx_burst() returned the inbound packet
End:   after engine-side rte_eth_tx_burst() accepted the generated order packet
```

Included in timing:

- fixed Ethernet/wire decoding;
- adapter conversion to `MarketUpdate`;
- order-book update;
- strategy decision;
- risk validation;
- preallocated outbox operation;
- outbound order conversion;
- Ethernet frame encoding;
- TX mbuf allocation and frame copy;
- DPDK TX enqueue.

Excluded:

- physical NIC receive/send;
- Ethernet cable or switch traversal;
- exchange acknowledgement;
- network RTT.

### Verified Results

| Core | Mean | p50 | p95 | p99 | p99.9 | Maximum | Events | Failures |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| CPU 0 | 150.150 ns | **110.844 ns** | 248.397 ns | **552.217 ns** | 706.464 ns | 68.037 µs | 1,000,000 | 0 |
| CPU 2 | 155.641 ns | **112.847 ns** | 254.407 ns | **550.214 ns** | 754.541 ns | 63.246 µs | 1,000,000 | 0 |

### Meaning of This Result

The Ring PMD result is the cleanest DPDK software-path baseline. It shows that PulseBook can transform received packet bytes into an outbound order packet in approximately:

```text
~111 ns median
~550 ns p99
```

under the virtual Ring PMD test setup.

## 12.2 Regime-Specific Ring PMD Benchmark

Phase 8 separates timing by decision type:

| Regime | Timing Ends At |
|---|---|
| NO_SIGNAL | Strategy/engine confirms no order |
| BUY | BUY order TX enqueue is accepted |
| SELL | SELL order TX enqueue is accepted |

A successful captured run reported these packet-out values:

| Regime | Mean | p50 | p95 | p99 | p99.9 | Maximum |
|---|---:|---:|---:|---:|---:|---:|
| BUY packet-out path | 127.529 ns | **89.476 ns** | 234.375 ns | **508.146 ns** | 713.809 ns | 133.522 µs |
| SELL packet-out path | 126.388 ns | **90.812 ns** | 228.365 ns | **498.130 ns** | 647.035 ns | 20.894 µs |

The run completed with:

```text
NO_SIGNAL decisions:   1,100,000 including warmup
BUY orders:            1,100,000 including warmup
SELL orders:           1,100,000 including warmup
Total failures:        0
Status:                OK
```

Note: before publishing this Phase 8 table with a CPU label, check the complete saved output for its `Main lcore:` line. The successful excerpt available here confirms the measurements and status but does not include the core line.

## 12.3 DPDK AF_PACKET Kernel-Backed Path

### Measurement Boundary

```text
Start: after AF_PACKET rte_eth_rx_burst() returned the inbound market-data packet
End:   after AF_PACKET rte_eth_tx_burst() accepted the generated outbound order
```

This still measures application-side processing, not the travel time across the `veth` link before RX return or after TX acceptance.

### Verified Results

| Core | Mean | p50 | p95 | p99 | p99.9 | Maximum | Events | Failures |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| CPU 0 | 1.964 µs | **1.737 µs** | 2.594 µs | **3.262 µs** | 10.542 µs | 196.615 µs | 1,000,000 | 0 |
| CPU 2 | 2.162 µs | **1.832 µs** | 2.903 µs | **7.822 µs** | 22.061 µs | 200.913 µs | 1,000,000 | 0 |

### Why AF_PACKET Is Slower Than Ring PMD

The AF_PACKET path adds a kernel-backed Linux network-interface layer. It is therefore expected to be slower than the in-process Ring PMD path.

The important comparison is:

| Path | Best Verified CPU 0 p50 | Best Verified CPU 0 p99 | What It Represents |
|---|---:|---:|---|
| Ring PMD | 110.844 ns | 552.217 ns | Virtual in-process DPDK software baseline |
| AF_PACKET + `veth` | 1.737 µs | 3.262 µs | Kernel-backed DPDK Linux-interface application path |

---

# 13. How the Latency Was Achieved

The latency results did not come from a shortcut or from measuring only a strategy function. They came from deliberately controlling the whole application-side packet path.

## 13.1 Design Decisions That Kept the Path Fast

| Design Decision | Benefit |
|---|---|
| C++20 native code | Direct control over memory and execution |
| Fixed 62-byte packet frames | No dynamic parsing or variable lengths |
| Integer price ticks | Simple deterministic arithmetic |
| Fixed-depth L2 book | Predictable memory access |
| Small imbalance strategy | Minimal decision logic |
| Inline risk checks | Safety without service calls |
| 32-byte order request | Compact outbound internal object |
| Preallocated outbox | Avoids hot-path heap allocation |
| Single-core DPDK loop | Avoids locks and cross-core coordination |
| Warmup before measurements | Reduces cold-start distortion |
| Separate metrics by path | Avoids misleading mixed benchmark claims |

## 13.2 What Was Kept Out of the Hot Path

PulseBook intentionally excludes from the measured decision loop:

- console logging;
- database storage;
- network APIs such as HTTP;
- dynamic JSON serialization;
- Kafka, Redis or message brokers;
- frontend/dashboard work;
- container or service orchestration.

Those systems may be useful in a complete trading platform, but they should not sit in the order-generation hot path.

---

# 14. Engineering Problems Solved During Development

Several real systems issues appeared and were corrected.

## 14.1 Ring PMD Linker Error

Problem:

```text
undefined reference to rte_eth_from_rings
```

Cause:

```text
The Ring PMD library implementing the API was not explicitly linked.
```

Fix:

```text
Locate and link librte_net_ring.so in the DPDK CMake target.
```

Lesson:

> Successfully compiling DPDK core libraries does not automatically mean every PMD-specific API has been linked.

## 14.2 Release-Build Tests Had Assertions Disabled

Problem:

```text
Release mode added -DNDEBUG, disabling assert(...) in tests.
```

Fix:

```text
Re-enable assertions for tests using -UNDEBUG and compile tests with -Werror.
```

Lesson:

> A test executable that exits successfully is not meaningful if its checks were compiled out.

## 14.3 DPDK Object Names Were Too Long

Problem:

```text
cannot create mbuf pool: File name too long
```

Cause:

```text
Long mempool/ring object names exceeded DPDK internal resource-name limits.
```

Fix:

```text
Use short DPDK object names such as pb_mp, pb_md and pb_ord.
```

## 14.4 AF_PACKET Mbuf Data Room Was Too Small

Problem:

```text
2016 bytes will not fit in mbuf (1920 bytes)
```

Cause:

```text
A 2048-byte mbuf data room loses available packet space to DPDK headroom,
while AF_PACKET RX setup expected more usable space.
```

Fix:

```text
Use a 4096-byte mbuf data room in AF_PACKET applications.
```

## 14.5 AF_PACKET Interface Did Not Exist

Problem:

```text
SIOCGIFINDEX: No such device
Generator failure: interface pb_peer was not found
```

Cause:

```text
The private pb_peer <-> pb_eng veth pair had been removed before the run.
```

Fix:

```text
Always create the veth pair before starting AF_PACKET programs and remove it only after all AF_PACKET checks finish.
```

These fixes make the project more credible because they reflect genuine low-level debugging work rather than only writing application logic.

---

# 15. Codebase Architecture

A cleaned final repository can be organized as follows:

```text
PulseBook/
├── CMakeLists.txt
├── real_run.md
├── details.md
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
│   │   └── fixed_l2_book.hpp
│   ├── common/
│   │   └── types.hpp
│   ├── dpdk/
│   │   ├── dpdk_config.hpp
│   │   ├── dpdk_latency_recorder.hpp
│   │   ├── engine_wire_adapter.hpp
│   │   ├── latency_virtual_link.hpp
│   │   └── virtual_link.hpp
│   ├── engine/
│   │   └── trading_engine.hpp
│   ├── order/
│   │   ├── order_request.hpp
│   │   └── preallocated_outbox.hpp
│   ├── risk/
│   │   └── risk_guard.hpp
│   ├── strategy/
│   │   └── imbalance_strategy.hpp
│   └── wire/
│       ├── endian.hpp
│       ├── ethernet_frame.hpp
│       ├── market_data_packet.hpp
│       ├── order_packet.hpp
│       ├── wire_constants.hpp
│       └── wire_header.hpp
├── tests/
│   ├── test_wire_market_data.cpp
│   ├── test_wire_order.cpp
│   ├── test_wire_invalid_packet.cpp
│   ├── test_ethernet_market_frame.cpp
│   ├── test_ethernet_order_frame.cpp
│   ├── test_engine_wire_adapter.cpp
│   └── test_engine_scenarios.cpp
├── scripts/level3/
│   ├── phase9_hardware_readiness.sh
│   ├── setup_veth_afpacket.sh
│   └── remove_veth_afpacket.sh
├── docs/level3/
│   ├── AF_PACKET_PATH.md
│   ├── AF_PACKET_LATENCY.md
│   ├── LATENCY_BENCHMARK.md
│   ├── REGIME_BENCHMARK.md
│   ├── FINAL_RESULTS.md
│   └── WIRE_PROTOCOL.md
└── results/level3/
    └── saved successful benchmark logs
```

## Main Applications

| Program | Purpose |
|---|---|
| `pulsebook_dpdk_scenarios` | Validates outcomes such as BUY, SELL and rejection through Ring PMD |
| `pulsebook_dpdk_latency_benchmark` | Measures the repeated BUY virtual Ring PMD path |
| `pulsebook_dpdk_regime_benchmark` | Separately measures NO_SIGNAL, BUY and SELL paths |
| `pulsebook_dpdk_afpacket_engine` | Processes raw frames on the AF_PACKET functional path |
| `pulsebook_afpacket_generator` | Sends eleven deterministic frames and validates returned BUY order |
| `pulsebook_dpdk_afpacket_latency` | Measures AF_PACKET application-side order path |
| `pulsebook_afpacket_benchmark_generator` | Feeds and validates AF_PACKET benchmark orders |

---

# 16. What You Achieved

## Technical Achievements

You built and validated:

- a compact binary market-data and order wire format;
- fixed-size Ethernet packet encoding and decoding;
- a low-latency L2 order book and decision pipeline;
- BUY, SELL and no-order strategy handling;
- inline risk rejection;
- invalid-frame filtering;
- DPDK virtual packet RX/TX integration;
- DPDK timestamp-based latency collection;
- a kernel-backed raw Ethernet interface path using DPDK AF_PACKET;
- an independent generator that validates returned order packets;
- one-million-event latency validation runs with zero processing failures.

## Systems Engineering Achievements

You also handled practical systems issues:

- DPDK installation and EAL initialization on Ubuntu 26.04;
- explicit PMD linking in CMake;
- resource naming limitations;
- Release test correctness;
- DPDK mbuf sizing;
- isolated Linux `veth` creation and removal;
- hardware capability inspection and honest benchmarking boundaries.

---

# 17. What You Can Claim on a Resume or Portfolio

## Short Resume Bullet

> Built **PulseBook**, a C++20/DPDK low-latency trading engine with fixed 62-byte Ethernet frames, L2 order-book processing, imbalance-driven execution and inline risk checks; achieved **110.8 ns p50 / 552.2 ns p99** on virtual Ring PMD and **1.74 µs p50 / 3.26 µs p99** on kernel-backed AF_PACKET RX-to-TX processing across **1M zero-failure order events**.

## Two-Bullet Resume Version

- Built **PulseBook**, a C++20 electronic trading packet processor implementing fixed binary Ethernet market/order frames, a low-latency L2 book, imbalance-based BUY/SELL strategy, risk validation and preallocated order handling.
- Integrated **DPDK Ring PMD** and **DPDK AF_PACKET** packet paths; measured **110.8 ns p50 / 552.2 ns p99** for the virtual Ring PMD application path and **1.74 µs p50 / 3.26 µs p99** for a kernel-backed `veth` AF_PACKET path over **1M measured events with zero failures**.

## Portfolio Description

> PulseBook is a low-latency trading engine built to study how fast an application can turn binary market-data packets into validated outbound orders. It uses fixed-size Ethernet frames, an in-memory order book, imbalance-based execution logic, inline risk checks and DPDK RX/TX processing. I validated correctness across BUY, SELL, balanced-market, risk-rejected and malformed-packet scenarios, then measured both a virtual Ring PMD baseline and a kernel-backed AF_PACKET path over a private Linux virtual Ethernet link.

## Interview Explanation

> I built PulseBook to understand the hot path of an electronic trading engine, not just application-level backend development. I designed compact binary Ethernet messages, built the order-book/strategy/risk pipeline in C++20, integrated it with DPDK mbufs and burst RX/TX APIs, and benchmarked the packet-in to packet-out application path. On virtual DPDK Ring PMD, it reached around 111 ns median and 552 ns p99. On a more network-facing AF_PACKET path over an isolated Linux veth link, it reached 1.74 µs median and 3.26 µs p99 on CPU 0. Both results used one million measured order-producing events with zero failures. I clearly distinguish these from physical NIC or exchange latency because my laptop does not have a suitable wired PCIe test NIC.

---

# 18. Claims You Must Not Make

Do **not** write or say:

- “110 ns physical NIC latency.”
- “1.74 µs wire-to-wire Ethernet latency.”
- “Achieved exchange order acknowledgement in microseconds.”
- “Completed VFIO hardware bypass benchmark on the laptop.”
- “End-to-end network latency is 552 ns.”
- “Real exchange-grade NIC latency benchmark.”

Why:

```text
Ring PMD is an in-process virtual DPDK transport.
AF_PACKET over veth is a kernel-backed Linux interface path.
Neither is a physical NIC or exchange connection.
```

Correct language is a strength, not a weakness. Engineers and recruiters trust precise benchmarking claims.

---

# 19. Future Improvements

## 19.1 True Physical NIC DPDK Benchmark

Current blocker:

```text
No suitable wired PCIe Ethernet NIC is available in the laptop.
```

Upgrade path:

1. obtain a DPDK-supported wired Ethernet NIC or a system with supported ports;
2. maintain a separate management connection so test ports can safely be bound;
3. allocate hugepages;
4. bind the test NIC to `vfio-pci`;
5. use a second physical port or an external traffic generator;
6. measure physical ingress-to-egress latency honestly.

This would allow a new result category:

```text
Physical DPDK application-side packet processing latency
```

With proper external timestamping hardware, it could later become:

```text
Physical wire ingress-to-egress latency
```

## 19.2 More Realistic Market Feed Replay

Current deterministic workloads are excellent for correctness and clean comparisons. A future version can add:

- recorded market-data replay;
- bursts of multiple instruments;
- sequence gaps and recovery handling;
- cancels and replaces;
- crossed/locked market edge cases;
- variable order-generation rates.

## 19.3 Multi-Instrument Support

The current testing focuses on one deterministic instrument. A production-style evolution could maintain books for many instruments using:

- a fixed instrument table;
- preallocated per-symbol state;
- cache-aware placement;
- per-symbol risk limits.

## 19.4 More Complete Order Lifecycle

Future trading behavior could include:

- exchange acknowledgements;
- rejects;
- partial fills;
- cancel requests;
- replace requests;
- outstanding-order tracking;
- position updates from fills;
- end-of-day kill switch.

These should remain separated from the lowest-latency market-data hot path when possible.

## 19.5 Benchmark Stability Improvements

Latency measurements can become more repeatable by adding:

- CPU isolation for the benchmark core;
- performance CPU governor runs;
- IRQ isolation;
- turbo/governor documentation;
- repeated-run summary statistics;
- cold-cache and warm-cache comparison;
- `perf` counters for cache misses, branch misses and cycles.

## 19.6 Alternative Linux I/O Comparison

Before physical NIC hardware is available, compare additional software/network-facing paths:

- AF_XDP;
- TAP PMD;
- Linux raw socket baseline;
- different AF_PACKET settings;
- different burst sizes.

These comparisons should still be labelled as software/kernel-interface benchmarks rather than physical NIC results.

---

# 20. How to Reproduce the Project

The companion file:

```text
real_run.md
```

contains the complete commands for:

- building the clean project;
- running tests;
- running Ring PMD functional validation;
- running Ring PMD benchmarks;
- creating and removing the private `veth` pair;
- running AF_PACKET functional validation;
- running AF_PACKET latency benchmarks;
- saving verified result logs.

Use that file for exact command-line reproduction. Use this `details.md` file for the project explanation, architecture, evidence, claim wording and future roadmap.

---

# 21. Recommended README Summary

## PulseBook

PulseBook is a C++20/DPDK low-latency electronic trading packet processor. It consumes fixed 62-byte Ethernet market-data frames, updates a compact in-memory L2 book, evaluates bid/ask imbalance, applies inline risk checks and emits outbound order frames through DPDK TX.

### Highlights

- Fixed-size binary Ethernet market-data and outbound-order messages.
- L2 order-book processing with BUY / SELL / NO_SIGNAL behavior.
- Inline risk guard and fixed-size preallocated order path.
- DPDK Ring PMD virtual packet processing.
- DPDK AF_PACKET raw packet path over an isolated Linux `veth` link.
- Scenario validation for BUY, SELL, NO_SIGNAL, RISK_REJECT and INVALID_FRAME.

### Performance

| Benchmark Path | Core | p50 | p99 | Measured Events | Failures |
|---|---:|---:|---:|---:|---:|
| Virtual DPDK Ring PMD RX-to-TX application path | CPU 0 | 110.844 ns | 552.217 ns | 1,000,000 | 0 |
| Virtual DPDK Ring PMD RX-to-TX application path | CPU 2 | 112.847 ns | 550.214 ns | 1,000,000 | 0 |
| Kernel-backed DPDK AF_PACKET over private `veth` | CPU 0 | 1.737 µs | 3.262 µs | 1,000,000 | 0 |
| Kernel-backed DPDK AF_PACKET over private `veth` | CPU 2 | 1.832 µs | 7.822 µs | 1,000,000 | 0 |

### Measurement Scope

Ring PMD numbers measure a virtual in-process DPDK software path. AF_PACKET numbers measure a kernel-backed DPDK application-side path over a private Linux `veth` interface. These measurements do not represent physical NIC wire latency, VFIO-backed hardware bypass or exchange RTT.

---

# 22. Evidence Checklist Before Publishing Results

Before publishing the repository or adding performance claims to a resume:

- [ ] Keep `real_run.md` in the repository.
- [ ] Keep this `details.md` document.
- [ ] Save successful benchmark outputs under `results/level3/`.
- [ ] Confirm every published latency log includes `Status: OK`.
- [ ] Confirm every published measured run includes `Failures: 0`.
- [ ] For Phase 8 regime data, attach the full saved log before assigning a CPU label.
- [ ] Never claim physical NIC performance unless future hardware tests are actually performed.

---

# 23. Final Summary

PulseBook Level 3 is a completed low-latency systems project that demonstrates much more than an isolated algorithm benchmark. It processes binary Ethernet-shaped market messages through DPDK, makes trading decisions using an L2 order book and imbalance strategy, applies risk controls, produces outbound order packets, validates correctness across multiple trading outcomes, and reports honest latency results for two different DPDK transport categories.

The strongest verified result currently supported by the laptop is:

```text
Virtual DPDK Ring PMD application path:
    110.844 ns p50 / 552.217 ns p99 on CPU 0
    1,000,000 measured order events
    0 failures

Kernel-backed DPDK AF_PACKET path over private veth:
    1.737 µs p50 / 3.262 µs p99 on CPU 0
    1,000,000 measured order events
    0 failures
```


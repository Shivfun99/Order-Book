# PulseBook Upcoming Version V2

## Goal

V1 proved the engine hot path with controlled packets and DPDK-based RX/TX processing.

**V2 will focus on making the benchmark closer to a real trading environment.**

---

## V2 Main Features

### 1. Real Market-Data Replay

Add replay support for recorded/high-rate market data.

Goal:

```text
realistic packet bursts
    -> queue buildup
    -> true tail-latency behavior
```

This will make p99 and p99.9 more meaningful.

---

### 2. Burst-Load Benchmarking

Current V1 uses controlled synthetic packets.

V2 should test:

- bursty packet arrival;
- queue buildup;
- dropped/late packets;
- p99 / p99.9 under pressure;
- sustained throughput.

---

### 3. Multi-Symbol Order Books

Add support for many instruments instead of one test instrument.

Planned design:

- fixed instrument table;
- one L2 book per symbol;
- preallocated per-symbol state;
- cache-aware layout.

---

### 4. Better Order Lifecycle

Add more exchange-like order behavior:

- order acknowledgements;
- rejects;
- partial fills;
- full fills;
- cancels;
- replace/modify orders;
- outstanding-order tracking.

---

### 5. Position and Risk State

Improve the risk system with:

- live position tracking;
- per-symbol limits;
- global notional limits;
- open-order exposure;
- kill switch;
- max loss guard.

---

### 6. AF_XDP Comparison

Compare AF_PACKET with AF_XDP.

Goal:

```text
AF_PACKET vs AF_XDP
    -> latency
    -> p99/p99.9
    -> throughput
    -> CPU usage
```

---

### 7. Real NIC / VFIO Path

Run PulseBook on a machine with a supported wired DPDK NIC.

Needed:

- supported PCIe Ethernet NIC;
- VFIO binding;
- hugepages;
- second machine or external traffic generator;
- proper timestamping.

This will allow a real physical DPDK benchmark.

---

### 8. Wire-to-Wire Measurement

V2/V3 should eventually include:

- hardware timestamps;
- external timestamp source;
- ingress-to-egress measurement;
- real NIC latency separation.

This is required before claiming physical NIC or wire latency.

---

### 9. CPU and OS Tuning

Improve benchmark stability with:

- CPU isolation;
- performance governor;
- IRQ isolation;
- reduced background processes;
- repeated benchmark runs;
- perf counters for cache misses and branch misses.

---

### 10. Better Documentation and Result Logs

Add clean result folders for:

```text
results/v1/
results/v2/
```

Each benchmark should include:

- command used;
- CPU core;
- workload type;
- packet count;
- p50 / p95 / p99 / p99.9;
- failures;
- measurement scope.

---

## V2 Success Criteria

V2 should be considered successful when it can show:

```text
realistic market-data replay
    -> burst-load benchmark
    -> multi-symbol processing
    -> queueing/tail-latency report
    -> zero correctness failures
```

Minimum target:

- 1M+ replayed market events;
- multi-symbol book updates;
- p99 and p99.9 under burst load;
- no invalid order generation;
- clear separation between engine latency and network latency.

---

## V2 Honest Claim

After V2, the project should be described as:

```text
PulseBook V2 extends the C++20/DPDK trading engine from controlled synthetic
packet benchmarks to replay-driven burst-load testing, with multi-symbol
order books, improved risk state, and more meaningful p99/p99.9 tail-latency
measurement under market-like packet pressure.
```

---

## Not Yet Claimed in V2

Unless real hardware testing is completed, do not claim:

- physical NIC latency;
- exchange round-trip latency;
- wire-to-wire latency;
- production HFT performance.

---

## Short Roadmap

```text
V1: controlled engine hot-path benchmark
V2: replay + burst-load + multi-symbol benchmark
V3: real NIC / VFIO / hardware timestamp benchmark
V4: exchange-style order lifecycle and strategy simulator
```

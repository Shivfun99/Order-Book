# PulseBook Level 3 Phase 9: Physical NIC Readiness

## Purpose

Determine whether the development laptop can run a real DPDK physical-port
packet path safely.

## Completed Before This Phase

The following paths have already been validated using DPDK virtual Ring PMD
ports:

- fixed binary Ethernet protocol;
- market-data RX packet decoding;
- TradingEngine integration;
- BUY and SELL packet emission;
- no-signal handling;
- risk rejection;
- invalid frame rejection;
- virtual application-side packet latency measurement.

## Hardware Qualification Requirements

A physical NIC path requires:

1. A wired Ethernet device visible to Linux.
2. Preferably a PCI or PCIe device with a supported DPDK PMD.
3. A safe port that may be removed from Linux networking during testing.
4. VFIO/IOMMU readiness for safe userspace device access.
5. Hugepage configuration for hardware-mode DPDK execution.
6. An external traffic source or second usable port for realistic RX/TX tests.

## Safe Binding Rule

Never bind the interface carrying the active default route unless an alternate
management/network path is already confirmed.

Once an Ethernet interface is bound to vfio-pci for DPDK use, ordinary Linux
networking does not manage that port during the test.

## Benchmark Classifications

### Virtual DPDK Application-Side Latency

Measured using Ring PMD software ports. This is already implemented.

### Physical DPDK Application-Side Latency

Measured from a packet returned by physical-port rte_eth_rx_burst() until the
outbound order is accepted by rte_eth_tx_burst().

### Physical Wire Ingress-to-Egress Latency

Requires hardware timestamps or an external latency measurement system.

### Exchange Round-Trip Latency

Includes the network and actual venue acknowledgement path and is outside the
local DPDK benchmark.

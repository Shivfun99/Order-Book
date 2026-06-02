# PulseBook Level 3 Wire Protocol

## Purpose

PulseBook uses fixed-size binary messages for the DPDK packet hot path.

## Ethernet Frame Layout

| Region | Bytes |
|---|---:|
| Ethernet header | 14 |
| PulseBook header | 16 |
| PulseBook payload | 32 |
| Total excluding Ethernet FCS | 62 |

## PulseBook Header

| Offset | Bytes | Field |
|---:|---:|---|
| 0 | 2 | Magic: 0x5042 |
| 2 | 1 | Protocol version |
| 3 | 1 | Message type |
| 4 | 2 | Header length |
| 6 | 2 | Payload length |
| 8 | 4 | Sequence number |
| 12 | 4 | Flags |

## Message Types

| Value | Meaning |
|---:|---|
| 1 | Market data |
| 2 | New order |

## Market Data Payload

| Offset | Bytes | Field |
|---:|---:|---|
| 0 | 8 | Exchange timestamp in nanoseconds |
| 8 | 4 | Instrument ID |
| 12 | 8 | Price ticks |
| 20 | 4 | Quantity |
| 24 | 1 | Side |
| 25 | 1 | Update action |
| 26 | 2 | Level |
| 28 | 4 | Source flags |

## Outbound Order Payload

| Offset | Bytes | Field |
|---:|---:|---|
| 0 | 8 | Client order ID |
| 8 | 4 | Instrument ID |
| 12 | 8 | Price ticks |
| 20 | 4 | Quantity |
| 24 | 1 | Side |
| 25 | 1 | Order type |
| 26 | 1 | Time in force |
| 27 | 1 | Order flags |
| 28 | 4 | Reserved |

## Hot-Path Rules

- No heap allocation.
- No string formatting.
- No logging.
- No variable-length packet parsing.
- No exceptions.
- All integer fields are encoded in big-endian order.

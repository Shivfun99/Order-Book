# PulseBook Level 3 Ethernet Frame Format

## Current Phase Scope

This phase defines untagged Ethernet II frames for PulseBook market-data and
outbound-order messages.

VLAN tags, IPv4, UDP, TCP and multicast subscription logic are intentionally
not included in this initial ultra-low-latency path.

## Frame Layout

| Offset | Bytes | Field |
|---:|---:|---|
| 0 | 6 | Destination MAC address |
| 6 | 6 | Source MAC address |
| 12 | 2 | PulseBook EtherType: 0x88B5 |
| 14 | 16 | PulseBook wire header |
| 30 | 32 | PulseBook message payload |

Total frame size excluding Ethernet FCS: 62 bytes.

## Why a Custom EtherType

The first implementation uses a compact raw Ethernet protocol instead of
IPv4/UDP so that the local packet path performs only:

- MAC and EtherType validation;
- fixed-offset field decoding;
- trading engine invocation;
- fixed-offset outbound packet encoding.

## Validation Per Packet

Incoming packets must satisfy:

- packet length is at least 62 bytes;
- EtherType is 0x88B5;
- PulseBook magic is 0x5042;
- protocol version is 1;
- payload length is 32 bytes;
- message type is market data;
- all enum fields are valid.

## Later Hardware Notes

The initial packet path is intentionally untagged. VLAN support can be added
later as a separate decoder branch without changing the strategy or risk
engine.

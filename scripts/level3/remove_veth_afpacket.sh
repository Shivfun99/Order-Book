#!/usr/bin/env bash

set -euo pipefail

if ip link show dev pb_peer >/dev/null 2>&1; then
    sudo ip link delete dev pb_peer
    echo "Removed PulseBook test pair: pb_peer <-> pb_eng"
elif ip link show dev pb_eng >/dev/null 2>&1; then
    sudo ip link delete dev pb_eng
    echo "Removed PulseBook test pair: pb_peer <-> pb_eng"
else
    echo "PulseBook veth pair is not present."
fi

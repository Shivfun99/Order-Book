#!/usr/bin/env bash

set -euo pipefail

if ip link show pb_peer >/dev/null 2>&1; then
    sudo ip link delete pb_peer
    echo "Removed PulseBook veth test cable: pb_peer <-> pb_eng"
else
    echo "PulseBook veth test cable is not present."
fi

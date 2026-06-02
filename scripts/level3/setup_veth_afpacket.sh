#!/usr/bin/env bash

set -euo pipefail

PEER_IF="pb_peer"
ENGINE_IF="pb_eng"

PEER_MAC="02:00:00:00:00:10"
ENGINE_MAC="02:00:00:00:00:20"

if ip link show "${PEER_IF}" >/dev/null 2>&1; then
    sudo ip link delete "${PEER_IF}"
fi

sudo ip link add "${PEER_IF}" type veth peer name "${ENGINE_IF}"

sudo ip link set dev "${PEER_IF}" address "${PEER_MAC}"
sudo ip link set dev "${ENGINE_IF}" address "${ENGINE_MAC}"

sudo ip link set dev "${PEER_IF}" up
sudo ip link set dev "${ENGINE_IF}" up

echo "============================================================"
echo "PulseBook private AF_PACKET test cable created"
echo "============================================================"
echo "Generator interface: ${PEER_IF} ${PEER_MAC}"
echo "Engine interface:    ${ENGINE_IF} ${ENGINE_MAC}"
echo
ip -br link show "${PEER_IF}" "${ENGINE_IF}"
echo
echo "No IP address is configured on these interfaces."
echo "Existing Wi-Fi and tether interfaces were not modified."

#!/usr/bin/env bash

set -euo pipefail

PEER_IF="pb_peer"
ENGINE_IF="pb_eng"

PEER_MAC="02:00:00:00:00:10"
ENGINE_MAC="02:00:00:00:00:20"

sudo modprobe veth 2>/dev/null || true

if ip link show dev "${PEER_IF}" >/dev/null 2>&1; then
    sudo ip link delete dev "${PEER_IF}"
elif ip link show dev "${ENGINE_IF}" >/dev/null 2>&1; then
    sudo ip link delete dev "${ENGINE_IF}"
fi

sudo ip link add name "${PEER_IF}" type veth peer name "${ENGINE_IF}"

sudo ip link set dev "${PEER_IF}" address "${PEER_MAC}"
sudo ip link set dev "${ENGINE_IF}" address "${ENGINE_MAC}"

sudo ip link set dev "${PEER_IF}" up
sudo ip link set dev "${ENGINE_IF}" up

echo "============================================================"
echo "PulseBook private AF_PACKET veth pair created"
echo "============================================================"
echo "Generator side: ${PEER_IF} ${PEER_MAC}"
echo "Engine side:    ${ENGINE_IF} ${ENGINE_MAC}"
echo

ip -br link show dev "${PEER_IF}"
ip -br link show dev "${ENGINE_IF}"

echo
echo "Existing internet interfaces were not modified."

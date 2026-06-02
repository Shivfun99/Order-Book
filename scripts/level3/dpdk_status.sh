#!/usr/bin/env bash

set -euo pipefail

OUT_DIR="diagnostics/level3_phase1"
mkdir -p "${OUT_DIR}"
OUT="${OUT_DIR}/dpdk_status_$(date +%Y%m%d_%H%M%S).txt"

{
    echo "============================================================"
    echo "PulseBook Level 3 - DPDK Status Report"
    echo "============================================================"
    echo "Generated: $(date -Is)"
    echo

    echo "============================================================"
    echo "1. Ubuntu"
    echo "============================================================"
    grep -E 'PRETTY_NAME|VERSION_ID' /etc/os-release || true
    uname -r
    echo

    echo "============================================================"
    echo "2. DPDK Version"
    echo "============================================================"
    pkg-config --modversion libdpdk || echo "libdpdk unavailable"
    dpkg-query -W -f='${Package} ${Version}\n' \
        dpdk dpdk-dev libdpdk-dev 2>/dev/null || true
    echo

    echo "============================================================"
    echo "3. PCI Network Controllers"
    echo "============================================================"
    lspci -Dnnk | grep -A4 -Ei 'Ethernet controller|Network controller' || true
    echo

    echo "============================================================"
    echo "4. Linux Interfaces"
    echo "============================================================"
    ip -br link || true
    echo
    ip -br addr || true
    echo
    ip route show default || true
    echo

    echo "============================================================"
    echo "5. DPDK Device Binding Status"
    echo "============================================================"

    if command -v dpdk-devbind.py >/dev/null 2>&1; then
        dpdk-devbind.py --status-dev net || true
    elif [ -f /usr/share/dpdk/usertools/dpdk-devbind.py ]; then
        python3 /usr/share/dpdk/usertools/dpdk-devbind.py --status-dev net || true
    else
        echo "dpdk-devbind.py not found."
    fi

    echo
    echo "============================================================"
    echo "6. Hugepages"
    echo "============================================================"
    grep -i Huge /proc/meminfo || true
    mount | grep hugetlbfs || echo "No hugetlbfs mount active."
    echo

    echo "============================================================"
    echo "7. VFIO"
    echo "============================================================"
    lsmod | grep -E '^vfio|vfio_pci' || echo "VFIO modules not loaded."
    modinfo vfio_pci 2>/dev/null | grep -E 'filename:|description:' || true
    echo

    echo "============================================================"
    echo "8. IOMMU Groups"
    echo "============================================================"
    if [ -d /sys/kernel/iommu_groups ]; then
        find /sys/kernel/iommu_groups -mindepth 1 -maxdepth 1 -type d | wc -l
        find /sys/kernel/iommu_groups -type l 2>/dev/null | head -n 30
    else
        echo "No IOMMU groups directory visible."
    fi

} | tee "${OUT}"

echo
echo "Saved report to ${OUT}"

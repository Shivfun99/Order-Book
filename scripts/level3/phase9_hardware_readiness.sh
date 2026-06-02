#!/usr/bin/env bash

set -u

OUT_DIR="diagnostics/level3_phase9"
mkdir -p "${OUT_DIR}"

OUT="${OUT_DIR}/hardware_readiness_$(date +%Y%m%d_%H%M%S).txt"

section() {
    echo
    echo "================================================================"
    echo "$1"
    echo "================================================================"
}

run_optional() {
    echo "\$ $*"
    "$@" 2>&1 || echo "[command failed or unavailable]"
}

find_devbind() {
    if command -v dpdk-devbind.py >/dev/null 2>&1; then
        command -v dpdk-devbind.py
        return 0
    fi

    for candidate in \
        /usr/share/dpdk/usertools/dpdk-devbind.py \
        /usr/bin/dpdk-devbind.py \
        /usr/local/bin/dpdk-devbind.py; do
        if [ -f "${candidate}" ]; then
            echo "${candidate}"
            return 0
        fi
    done

    return 1
}

{
    echo "PulseBook Level 3 Phase 9A - Physical NIC Readiness Report"
    echo "Generated: $(date -Is)"
    echo "Host: $(hostname)"
    echo

    section "1. Operating System and Kernel"
    run_optional grep -E 'PRETTY_NAME|VERSION_ID|VERSION_CODENAME' /etc/os-release
    run_optional uname -a
    run_optional uname -r

    section "2. DPDK Installation"
    run_optional pkg-config --modversion libdpdk
    run_optional pkg-config --variable=libdir libdpdk

    echo
    echo "Installed DPDK packages:"
    dpkg-query -W -f='${Package} ${Version}\n' \
        dpdk dpdk-dev libdpdk-dev 2>/dev/null || true

    section "3. Current Network Connectivity"
    run_optional ip -br link
    run_optional ip -br addr
    run_optional ip route show

    echo
    echo "Default route interfaces:"
    ip route show default 2>/dev/null | awk '{print $5}' | sort -u || true

    echo
    echo "NetworkManager status when available:"
    if command -v nmcli >/dev/null 2>&1; then
        nmcli -f DEVICE,TYPE,STATE,CONNECTION device status 2>&1 || true
    else
        echo "nmcli unavailable"
    fi

    section "4. PCI Network Controllers"
    if command -v lspci >/dev/null 2>&1; then
        lspci -Dnnk | grep -A5 -Ei 'Ethernet controller|Network controller' || true
    else
        echo "lspci unavailable. Install pciutils."
    fi

    section "5. Linux Interface to Hardware Mapping"
    DEFAULT_INTERFACES="$(ip route show default 2>/dev/null | awk '{print $5}' | sort -u | tr '\n' ' ')"

    for iface_path in /sys/class/net/*; do
        iface="$(basename "${iface_path}")"

        if [ "${iface}" = "lo" ]; then
            continue
        fi

        echo
        echo "------------------------------------------------------------"
        echo "Interface: ${iface}"
        echo "------------------------------------------------------------"

        if echo " ${DEFAULT_INTERFACES} " | grep -q " ${iface} "; then
            echo "Default route: YES -- DO NOT BIND THIS PORT YET"
        else
            echo "Default route: no"
        fi

        echo -n "MAC address: "
        cat "${iface_path}/address" 2>/dev/null || true

        echo -n "Operational state: "
        cat "${iface_path}/operstate" 2>/dev/null || true

        DEVICE_PATH="$(readlink -f "${iface_path}/device" 2>/dev/null || true)"
        echo "Device path: ${DEVICE_PATH:-not visible}"

        if [ -n "${DEVICE_PATH}" ]; then
            BDF="$(basename "${DEVICE_PATH}")"
            echo "Potential PCI BDF: ${BDF}"

            if [[ "${BDF}" =~ ^[0-9a-fA-F]{4}:[0-9a-fA-F]{2}:[0-9a-fA-F]{2}\.[0-9a-fA-F]$ ]] && \
               command -v lspci >/dev/null 2>&1; then
                echo "PCI details:"
                lspci -Dnnk -s "${BDF}" 2>&1 || true

                echo "IOMMU group:"
                if [ -L "${DEVICE_PATH}/iommu_group" ]; then
                    readlink -f "${DEVICE_PATH}/iommu_group" || true
                    echo "Other devices in same IOMMU group:"
                    GROUP_PATH="$(readlink -f "${DEVICE_PATH}/iommu_group")"
                    find "${GROUP_PATH}/devices" -maxdepth 1 -type l -printf '%f\n' 2>/dev/null | sort || true
                else
                    echo "No IOMMU group link visible for this device."
                fi
            fi
        fi

        if command -v ethtool >/dev/null 2>&1; then
            echo "Driver details:"
            ethtool -i "${iface}" 2>&1 || true

            echo "Link details:"
            ethtool "${iface}" 2>/dev/null | \
                grep -E 'Supported ports:|Speed:|Duplex:|Port:|Link detected:' || true
        else
            echo "ethtool unavailable."
        fi
    done

    section "6. USB Devices Potentially Related to Networking"
    if command -v lsusb >/dev/null 2>&1; then
        lsusb
        echo
        echo "USB topology:"
        lsusb -t 2>&1 || true
    else
        echo "lsusb unavailable. Install usbutils if required."
    fi

    section "7. DPDK Network Binding Status - Read Only"
    DEVBIND="$(find_devbind || true)"

    if [ -n "${DEVBIND}" ]; then
        echo "Using: ${DEVBIND}"

        if [[ "${DEVBIND}" == *.py ]]; then
            python3 "${DEVBIND}" --status-dev net 2>&1 || \
                python3 "${DEVBIND}" --status 2>&1 || true
        else
            "${DEVBIND}" --status-dev net 2>&1 || \
                "${DEVBIND}" --status 2>&1 || true
        fi
    else
        echo "dpdk-devbind.py not found."
    fi

    section "8. Installed Network PMD Libraries"
    DPDK_LIBDIR="$(pkg-config --variable=libdir libdpdk 2>/dev/null || true)"

    echo "pkg-config DPDK libdir: ${DPDK_LIBDIR:-unavailable}"
    echo

    for root in \
        "${DPDK_LIBDIR}" \
        /usr/lib/x86_64-linux-gnu \
        /usr/local/lib/x86_64-linux-gnu \
        /usr/local/lib; do
        [ -d "${root}" ] || continue

        find "${root}" -type f -o -type l 2>/dev/null | \
            grep -E 'librte_net_(igc|igb|ixgbe|i40e|ice|e1000|r8169|mlx5|af_packet|af_xdp|pcap|ring|tap|virtio)\.so' | \
            sort -u || true
    done

    section "9. CPU Topology and Benchmark Core"
    run_optional lscpu

    for cpu in 0 2; do
        echo
        echo "CPU ${cpu}:"
        for file in \
            "/sys/devices/system/cpu/cpu${cpu}/topology/core_id" \
            "/sys/devices/system/cpu/cpu${cpu}/topology/thread_siblings_list" \
            "/sys/devices/system/cpu/cpu${cpu}/cpufreq/scaling_governor" \
            "/sys/devices/system/cpu/cpu${cpu}/cpufreq/scaling_cur_freq"; do
            echo -n "$(basename "${file}"): "
            cat "${file}" 2>/dev/null || echo "unavailable"
        done
    done

    section "10. IOMMU and VFIO Readiness"
    echo "Kernel command line:"
    cat /proc/cmdline 2>/dev/null || true

    echo
    echo "IOMMU groups count:"
    if [ -d /sys/kernel/iommu_groups ]; then
        find /sys/kernel/iommu_groups -mindepth 1 -maxdepth 1 -type d 2>/dev/null | wc -l
    else
        echo "No /sys/kernel/iommu_groups directory."
    fi

    echo
    echo "VFIO module information:"
    modinfo vfio_pci 2>/dev/null | grep -E 'filename:|description:' || \
        echo "vfio_pci module information unavailable."

    echo
    echo "Currently loaded VFIO modules:"
    lsmod 2>/dev/null | grep -E '^vfio|vfio_pci' || \
        echo "VFIO modules are not currently loaded."

    echo
    echo "Relevant IOMMU/VFIO kernel log messages:"
    if command -v sudo >/dev/null 2>&1; then
        sudo dmesg 2>/dev/null | \
            grep -Ei 'DMAR|IOMMU|Intel-IOMMU|AMD-Vi|vfio' | tail -n 80 || true
    else
        echo "sudo unavailable."
    fi

    echo
    echo "Secure Boot state:"
    if command -v mokutil >/dev/null 2>&1; then
        mokutil --sb-state 2>&1 || true
    else
        echo "mokutil unavailable."
    fi

    section "11. Hugepage Readiness"
    grep -i Huge /proc/meminfo 2>/dev/null || true

    echo
    echo "Mounted hugetlbfs:"
    mount | grep hugetlbfs || echo "No hugetlbfs mount currently active."

    echo
    echo "Available hugepage sizes:"
    for directory in /sys/kernel/mm/hugepages/*; do
        [ -d "${directory}" ] || continue
        echo "$(basename "${directory}")"
        echo -n "  configured: "
        cat "${directory}/nr_hugepages" 2>/dev/null || true
        echo -n "  free:       "
        cat "${directory}/free_hugepages" 2>/dev/null || true
    done

    section "12. Phase 9A Classification Checklist"
    echo "[ ] Wired Ethernet interface exists"
    echo "[ ] Ethernet interface maps to PCI BDF rather than unsupported USB-only path"
    echo "[ ] Exact vendor/device ID captured"
    echo "[ ] Linux kernel driver captured"
    echo "[ ] Matching DPDK PMD library available"
    echo "[ ] Interface is NOT the required default route when binding"
    echo "[ ] IOMMU group visible or VFIO strategy determined"
    echo "[ ] Hugepage setup possible"
    echo "[ ] External packet peer/topology selected"

    section "End of Read-Only Report"
    echo "No interface was modified."
    echo "No port was bound to vfio-pci."
    echo "No hugepages were allocated."

} 2>&1 | tee "${OUT}"

echo
echo "Saved report to: ${OUT}"

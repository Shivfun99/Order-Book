#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_lcore.h>
#include <rte_version.h>

int main(int argc, char** argv) {
    const int eal_args = rte_eal_init(argc, argv);

    if (eal_args < 0) {
        std::fprintf(stderr, "PulseBook: failed to initialize DPDK EAL\n");
        return EXIT_FAILURE;
    }

    const unsigned int main_lcore = rte_lcore_id();
    const unsigned int lcore_count = rte_lcore_count();
    const std::uint16_t available_ports = rte_eth_dev_count_avail();

    std::printf("========================================\n");
    std::printf("PulseBook Level 3 - DPDK EAL Smoke Test\n");
    std::printf("========================================\n");
    std::printf("DPDK version:      %s\n", rte_version());
    std::printf("Main lcore:        %u\n", main_lcore);
    std::printf("Enabled lcores:    %u\n", lcore_count);
    std::printf("Available ports:   %u\n", available_ports);
    std::printf("EAL consumed args: %d\n", eal_args);
    std::printf("Status:            OK\n");

    const int cleanup_result = rte_eal_cleanup();

    if (cleanup_result != 0) {
        std::fprintf(stderr, "PulseBook: DPDK EAL cleanup failed\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

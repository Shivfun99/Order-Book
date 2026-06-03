cmake_minimum_required(VERSION 3.22)

project(PulseBook
    VERSION 1.0.0
    LANGUAGES CXX
)

option(PULSEBOOK_ENABLE_DPDK "Build DPDK applications and benchmarks" ON)
option(PULSEBOOK_BUILD_BENCHMARKS "Build latency benchmark executables" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

add_library(pulsebook_headers INTERFACE)

target_include_directories(pulsebook_headers INTERFACE
    ${PROJECT_SOURCE_DIR}/include
)

target_compile_features(pulsebook_headers INTERFACE
    cxx_std_20
)

function(pulsebook_enable_warnings target_name)
    target_compile_options(${target_name} PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
    )
endfunction()

function(pulsebook_add_test target_name source_file)
    if(BUILD_TESTING)
        add_executable(${target_name}
            ${PROJECT_SOURCE_DIR}/${source_file}
        )

        target_link_libraries(${target_name} PRIVATE
            pulsebook_headers
        )

        pulsebook_enable_warnings(${target_name})

        # Tests currently use assert(...); keep assertions enabled in Release builds.
        target_compile_options(${target_name} PRIVATE
            -UNDEBUG
        )

        add_test(
            NAME ${target_name}
            COMMAND ${target_name}
        )
    endif()
endfunction()

# ---------------------------------------------------------------------------
# Unit and engine correctness tests
# ---------------------------------------------------------------------------

pulsebook_add_test(
    pulsebook_test_wire_market_data
    tests/test_wire_market_data.cpp
)

pulsebook_add_test(
    pulsebook_test_wire_order
    tests/test_wire_order.cpp
)

pulsebook_add_test(
    pulsebook_test_wire_invalid_packet
    tests/test_wire_invalid_packet.cpp
)

pulsebook_add_test(
    pulsebook_test_ethernet_market_frame
    tests/test_ethernet_market_frame.cpp
)

pulsebook_add_test(
    pulsebook_test_ethernet_order_frame
    tests/test_ethernet_order_frame.cpp
)

pulsebook_add_test(
    pulsebook_test_engine_wire_adapter
    tests/test_engine_wire_adapter.cpp
)

pulsebook_add_test(
    pulsebook_test_engine_scenarios
    tests/test_engine_scenarios.cpp
)

# ---------------------------------------------------------------------------
# Linux raw-packet peer applications
# ---------------------------------------------------------------------------

add_executable(pulsebook_afpacket_generator
    ${PROJECT_SOURCE_DIR}/apps/pulsebook_afpacket_generator.cpp
)

target_link_libraries(pulsebook_afpacket_generator PRIVATE
    pulsebook_headers
)

pulsebook_enable_warnings(pulsebook_afpacket_generator)

target_compile_options(pulsebook_afpacket_generator PRIVATE
    -O3
)

if(PULSEBOOK_BUILD_BENCHMARKS)
    add_executable(pulsebook_afpacket_benchmark_generator
        ${PROJECT_SOURCE_DIR}/apps/pulsebook_afpacket_benchmark_generator.cpp
    )

    target_link_libraries(pulsebook_afpacket_benchmark_generator PRIVATE
        pulsebook_headers
    )

    pulsebook_enable_warnings(pulsebook_afpacket_benchmark_generator)

    target_compile_options(pulsebook_afpacket_benchmark_generator PRIVATE
        -O3
    )
endif()

# ---------------------------------------------------------------------------
# DPDK applications
# ---------------------------------------------------------------------------

if(PULSEBOOK_ENABLE_DPDK)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(DPDK REQUIRED IMPORTED_TARGET libdpdk)

    pkg_get_variable(DPDK_LIBDIR libdpdk libdir)

    find_library(DPDK_NET_RING_LIBRARY
        NAMES rte_net_ring
        HINTS
            "${DPDK_LIBDIR}"
            "/usr/lib/x86_64-linux-gnu"
        PATH_SUFFIXES
            "dpdk/pmds-25.11"
            "dpdk/pmds-26.0"
            "dpdk/pmds-26"
            "dpdk/pmds-25"
    )

    if(NOT DPDK_NET_RING_LIBRARY)
        message(FATAL_ERROR
            "Could not find librte_net_ring required for virtual Ring PMD executables.")
    endif()

    message(STATUS "DPDK Ring PMD library: ${DPDK_NET_RING_LIBRARY}")

    get_filename_component(DPDK_NET_RING_DIRECTORY
        "${DPDK_NET_RING_LIBRARY}"
        DIRECTORY
    )

    function(pulsebook_add_dpdk_app target_name source_file)
        add_executable(${target_name}
            ${PROJECT_SOURCE_DIR}/${source_file}
        )

        target_link_libraries(${target_name} PRIVATE
            pulsebook_headers
            PkgConfig::DPDK
        )

        pulsebook_enable_warnings(${target_name})

        target_compile_options(${target_name} PRIVATE
            -O3
        )
    endfunction()

    function(pulsebook_link_ring_pmd target_name)
        target_link_libraries(${target_name} PRIVATE
            "${DPDK_NET_RING_LIBRARY}"
        )

        set_property(
            TARGET ${target_name}
            APPEND PROPERTY BUILD_RPATH "${DPDK_NET_RING_DIRECTORY}"
        )
    endfunction()

    # Virtual Ring PMD correctness validation.
    pulsebook_add_dpdk_app(
        pulsebook_dpdk_scenarios
        apps/pulsebook_dpdk_scenarios.cpp
    )

    pulsebook_link_ring_pmd(pulsebook_dpdk_scenarios)

    if(BUILD_TESTING)
        add_test(
            NAME pulsebook_test_dpdk_scenarios_virtual
            COMMAND pulsebook_dpdk_scenarios
                -l 0
                --no-pci
                --no-huge
                --in-memory
                --mbuf-pool-ops-name=ring_mp_mc
                --file-prefix=pb_ctest_scenarios
        )
    endif()

    # Kernel-backed AF_PACKET functional engine.
    pulsebook_add_dpdk_app(
        pulsebook_dpdk_afpacket_engine
        apps/pulsebook_dpdk_afpacket_engine.cpp
    )

    if(PULSEBOOK_BUILD_BENCHMARKS)
        # Ring PMD latency baseline used for the published virtual result.
        pulsebook_add_dpdk_app(
            pulsebook_dpdk_latency_benchmark
            apps/pulsebook_dpdk_latency_benchmark.cpp
        )

        pulsebook_link_ring_pmd(pulsebook_dpdk_latency_benchmark)

        # Regime-specific Ring PMD benchmark.
        pulsebook_add_dpdk_app(
            pulsebook_dpdk_regime_benchmark
            apps/pulsebook_dpdk_regime_benchmark.cpp
        )

        pulsebook_link_ring_pmd(pulsebook_dpdk_regime_benchmark)

        # AF_PACKET / veth application-side latency benchmark.
        pulsebook_add_dpdk_app(
            pulsebook_dpdk_afpacket_latency
            apps/pulsebook_dpdk_afpacket_latency.cpp
        )
    endif()
endif()

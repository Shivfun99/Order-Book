include_guard(GLOBAL)

if(PULSEBOOK_ENABLE_DPDK)
    if(NOT TARGET pulsebook_wire)
        message(FATAL_ERROR
            "pulsebook_wire target is missing. Include PulseBookWire.cmake first.")
    endif()

    if(NOT TARGET PkgConfig::DPDK)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(DPDK REQUIRED IMPORTED_TARGET libdpdk)
    endif()

    if(NOT DPDK_NET_RING_LIBRARY)
        pkg_get_variable(DPDK_LIBDIR libdpdk libdir)

        find_library(DPDK_NET_RING_LIBRARY
            NAMES rte_net_ring
            HINTS
                "${DPDK_LIBDIR}"
                "/usr/lib/x86_64-linux-gnu"
            PATH_SUFFIXES
                "dpdk/pmds-25.11"
                "dpdk/pmds-25"
        )
    endif()

    if(NOT DPDK_NET_RING_LIBRARY)
        message(FATAL_ERROR
            "Could not find librte_net_ring required by the Phase 6 latency benchmark.")
    endif()

    add_executable(pulsebook_dpdk_latency_benchmark
        ${PROJECT_SOURCE_DIR}/apps/pulsebook_dpdk_latency_benchmark.cpp
    )

    target_compile_features(pulsebook_dpdk_latency_benchmark PRIVATE
        cxx_std_20
    )

    target_compile_options(pulsebook_dpdk_latency_benchmark PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -O3
    )

    target_include_directories(pulsebook_dpdk_latency_benchmark PRIVATE
        ${PROJECT_SOURCE_DIR}/include
    )

    target_link_libraries(pulsebook_dpdk_latency_benchmark PRIVATE
        pulsebook_headers
        pulsebook_wire
        PkgConfig::DPDK
        "${DPDK_NET_RING_LIBRARY}"
    )

    get_filename_component(DPDK_NET_RING_DIRECTORY
        "${DPDK_NET_RING_LIBRARY}"
        DIRECTORY
    )

    set_target_properties(pulsebook_dpdk_latency_benchmark PROPERTIES
        BUILD_RPATH "${DPDK_NET_RING_DIRECTORY}"
    )
endif()

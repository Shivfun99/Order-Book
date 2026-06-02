include_guard(GLOBAL)

include(CTest)

if(BUILD_TESTING)
    add_executable(pulsebook_test_engine_scenarios
        ${PROJECT_SOURCE_DIR}/tests/test_engine_scenarios.cpp
    )

    target_link_libraries(pulsebook_test_engine_scenarios PRIVATE
        pulsebook_headers
        pulsebook_wire
    )

    target_compile_features(pulsebook_test_engine_scenarios PRIVATE
        cxx_std_20
    )

    target_compile_options(pulsebook_test_engine_scenarios PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -UNDEBUG
        -Werror
    )

    add_test(
        NAME pulsebook_test_engine_scenarios
        COMMAND pulsebook_test_engine_scenarios
    )
endif()

if(PULSEBOOK_ENABLE_DPDK)
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
            "Could not find librte_net_ring required by Phase 7.")
    endif()

    add_executable(pulsebook_dpdk_scenarios
        ${PROJECT_SOURCE_DIR}/apps/pulsebook_dpdk_scenarios.cpp
    )

    target_compile_features(pulsebook_dpdk_scenarios PRIVATE
        cxx_std_20
    )

    target_compile_options(pulsebook_dpdk_scenarios PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -O3
    )

    target_include_directories(pulsebook_dpdk_scenarios PRIVATE
        ${PROJECT_SOURCE_DIR}/include
    )

    target_link_libraries(pulsebook_dpdk_scenarios PRIVATE
        pulsebook_headers
        pulsebook_wire
        PkgConfig::DPDK
        "${DPDK_NET_RING_LIBRARY}"
    )

    get_filename_component(DPDK_NET_RING_DIRECTORY
        "${DPDK_NET_RING_LIBRARY}"
        DIRECTORY
    )

    set_target_properties(pulsebook_dpdk_scenarios PROPERTIES
        BUILD_RPATH "${DPDK_NET_RING_DIRECTORY}"
    )

    if(BUILD_TESTING)
        add_test(
            NAME pulsebook_test_dpdk_scenarios_virtual
            COMMAND pulsebook_dpdk_scenarios
                -l 0
                --no-pci
                --no-huge
                --in-memory
                --mbuf-pool-ops-name=ring_mp_mc
                --file-prefix=pb_t7
        )
    endif()
endif()

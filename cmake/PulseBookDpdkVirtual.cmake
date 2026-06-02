include_guard(GLOBAL)

if(PULSEBOOK_ENABLE_DPDK)
    if(NOT TARGET pulsebook_wire)
        message(FATAL_ERROR
            "pulsebook_wire target is missing. Include cmake/PulseBookWire.cmake before cmake/PulseBookDpdkVirtual.cmake.")
    endif()

    if(NOT TARGET PkgConfig::DPDK)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(DPDK REQUIRED IMPORTED_TARGET libdpdk)
    endif()

    pkg_get_variable(DPDK_LIBDIR libdpdk libdir)

    find_library(DPDK_NET_RING_LIBRARY
        NAMES rte_net_ring
        HINTS
            "${DPDK_LIBDIR}"
        PATH_SUFFIXES
            "dpdk/pmds-25.11"
            "dpdk/pmds-25.11.0"
            "dpdk/pmds-25"
    )

    if(NOT DPDK_NET_RING_LIBRARY)
        file(GLOB DPDK_NET_RING_CANDIDATES
            "${DPDK_LIBDIR}/librte_net_ring.so"
            "${DPDK_LIBDIR}/librte_net_ring.so.*"
            "${DPDK_LIBDIR}/dpdk/pmds-*/librte_net_ring.so"
            "${DPDK_LIBDIR}/dpdk/pmds-*/librte_net_ring.so.*"
        )

        list(LENGTH DPDK_NET_RING_CANDIDATES DPDK_NET_RING_COUNT)

        if(DPDK_NET_RING_COUNT EQUAL 0)
            message(FATAL_ERROR
                "Could not find librte_net_ring. Install the DPDK Ring PMD runtime package.")
        endif()

        list(GET DPDK_NET_RING_CANDIDATES 0 DPDK_NET_RING_LIBRARY)
    endif()

    message(STATUS "DPDK Ring PMD library: ${DPDK_NET_RING_LIBRARY}")

    if(NOT TARGET pulsebook_dpdk_ring_test)
        add_executable(pulsebook_dpdk_ring_test
            ${PROJECT_SOURCE_DIR}/apps/pulsebook_dpdk_ring_test.cpp
        )

        target_compile_features(pulsebook_dpdk_ring_test PRIVATE
            cxx_std_20
        )

        target_compile_options(pulsebook_dpdk_ring_test PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -O3
        )

        target_include_directories(pulsebook_dpdk_ring_test PRIVATE
            ${PROJECT_SOURCE_DIR}/include
        )
    endif()

    target_link_libraries(pulsebook_dpdk_ring_test PRIVATE
        pulsebook_wire
        PkgConfig::DPDK
        "${DPDK_NET_RING_LIBRARY}"
    )

    get_filename_component(DPDK_NET_RING_DIRECTORY
        "${DPDK_NET_RING_LIBRARY}"
        DIRECTORY
    )

    set_target_properties(pulsebook_dpdk_ring_test PROPERTIES
        BUILD_RPATH "${DPDK_NET_RING_DIRECTORY}"
    )
endif()

include_guard(GLOBAL)

include(CTest)

if(BUILD_TESTING)
    add_executable(pulsebook_test_engine_wire_adapter
        ${PROJECT_SOURCE_DIR}/tests/test_engine_wire_adapter.cpp
    )

    target_link_libraries(pulsebook_test_engine_wire_adapter PRIVATE
        pulsebook_headers
        pulsebook_wire
    )

    target_compile_features(pulsebook_test_engine_wire_adapter PRIVATE
        cxx_std_20
    )

    target_compile_options(pulsebook_test_engine_wire_adapter PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -UNDEBUG
        -Werror
    )

    add_test(
        NAME pulsebook_test_engine_wire_adapter
        COMMAND pulsebook_test_engine_wire_adapter
    )
endif()

if(PULSEBOOK_ENABLE_DPDK)
    if(NOT TARGET pulsebook_dpdk_engine)
        message(FATAL_ERROR
            "pulsebook_dpdk_engine target must already exist in the root CMakeLists.txt.")
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
            "Could not find librte_net_ring required by pulsebook_dpdk_engine.")
    endif()

    target_link_libraries(pulsebook_dpdk_engine PRIVATE
        pulsebook_wire
        "${DPDK_NET_RING_LIBRARY}"
    )

    target_compile_features(pulsebook_dpdk_engine PRIVATE
        cxx_std_20
    )

    target_compile_options(pulsebook_dpdk_engine PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -O3
    )

    get_filename_component(DPDK_NET_RING_DIRECTORY
        "${DPDK_NET_RING_LIBRARY}"
        DIRECTORY
    )

    set_property(
        TARGET pulsebook_dpdk_engine
        APPEND PROPERTY BUILD_RPATH "${DPDK_NET_RING_DIRECTORY}"
    )
endif()

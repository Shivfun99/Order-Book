include_guard(GLOBAL)

add_executable(pulsebook_afpacket_benchmark_generator
    ${PROJECT_SOURCE_DIR}/apps/pulsebook_afpacket_benchmark_generator.cpp
)

target_compile_features(pulsebook_afpacket_benchmark_generator PRIVATE
    cxx_std_20
)

target_compile_options(pulsebook_afpacket_benchmark_generator PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
    -O3
)

target_include_directories(pulsebook_afpacket_benchmark_generator PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(pulsebook_afpacket_benchmark_generator PRIVATE
    pulsebook_headers
    pulsebook_wire
)

if(PULSEBOOK_ENABLE_DPDK)
    if(NOT TARGET PkgConfig::DPDK)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(DPDK REQUIRED IMPORTED_TARGET libdpdk)
    endif()

    add_executable(pulsebook_dpdk_afpacket_latency
        ${PROJECT_SOURCE_DIR}/apps/pulsebook_dpdk_afpacket_latency.cpp
    )

    target_compile_features(pulsebook_dpdk_afpacket_latency PRIVATE
        cxx_std_20
    )

    target_compile_options(pulsebook_dpdk_afpacket_latency PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -O3
    )

    target_include_directories(pulsebook_dpdk_afpacket_latency PRIVATE
        ${PROJECT_SOURCE_DIR}/include
    )

    target_link_libraries(pulsebook_dpdk_afpacket_latency PRIVATE
        pulsebook_headers
        pulsebook_wire
        PkgConfig::DPDK
    )
endif()

include_guard(GLOBAL)

add_executable(pulsebook_afpacket_generator
    ${PROJECT_SOURCE_DIR}/apps/pulsebook_afpacket_generator.cpp
)

target_compile_features(pulsebook_afpacket_generator PRIVATE
    cxx_std_20
)

target_compile_options(pulsebook_afpacket_generator PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
    -O3
)

target_include_directories(pulsebook_afpacket_generator PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(pulsebook_afpacket_generator PRIVATE
    pulsebook_headers
    pulsebook_wire
)

if(PULSEBOOK_ENABLE_DPDK)
    if(NOT TARGET PkgConfig::DPDK)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(DPDK REQUIRED IMPORTED_TARGET libdpdk)
    endif()

    add_executable(pulsebook_dpdk_afpacket_engine
        ${PROJECT_SOURCE_DIR}/apps/pulsebook_dpdk_afpacket_engine.cpp
    )

    target_compile_features(pulsebook_dpdk_afpacket_engine PRIVATE
        cxx_std_20
    )

    target_compile_options(pulsebook_dpdk_afpacket_engine PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -O3
    )

    target_include_directories(pulsebook_dpdk_afpacket_engine PRIVATE
        ${PROJECT_SOURCE_DIR}/include
    )

    target_link_libraries(pulsebook_dpdk_afpacket_engine PRIVATE
        pulsebook_headers
        pulsebook_wire
        PkgConfig::DPDK
    )
endif()

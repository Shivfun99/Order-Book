option(PULSEBOOK_ENABLE_DPDK "Build PulseBook Level 3 DPDK applications" OFF)

if(PULSEBOOK_ENABLE_DPDK)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(DPDK REQUIRED IMPORTED_TARGET libdpdk)

    add_executable(pulsebook_dpdk_hello
        ${PROJECT_SOURCE_DIR}/apps/pulsebook_dpdk_hello.cpp
    )

    target_compile_features(pulsebook_dpdk_hello PRIVATE cxx_std_20)

    target_compile_options(pulsebook_dpdk_hello PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )

    target_include_directories(pulsebook_dpdk_hello PRIVATE
        ${PROJECT_SOURCE_DIR}/include
    )

    target_link_libraries(pulsebook_dpdk_hello PRIVATE
        PkgConfig::DPDK
    )
endif()

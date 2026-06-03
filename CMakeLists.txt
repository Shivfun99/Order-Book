cmake_minimum_required(VERSION 3.22)

project(PulseBook
    VERSION 0.3.0
    LANGUAGES CXX
)

option(PULSEBOOK_ENABLE_DPDK "Build DPDK apps" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(pulsebook_headers INTERFACE)
target_include_directories(pulsebook_headers INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(pulsebook_demo apps/pulsebook_demo.cpp)
target_link_libraries(pulsebook_demo PRIVATE pulsebook_headers)

add_executable(pulsebook_benchmark apps/pulsebook_benchmark.cpp)
target_link_libraries(pulsebook_benchmark PRIVATE pulsebook_headers)

if(PULSEBOOK_ENABLE_DPDK)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(DPDK REQUIRED IMPORTED_TARGET libdpdk)

    add_executable(pulsebook_dpdk_hello apps/pulsebook_dpdk_hello.cpp)
    target_link_libraries(pulsebook_dpdk_hello PRIVATE pulsebook_headers PkgConfig::DPDK)

    add_executable(pulsebook_dpdk_minimal apps/pulsebook_dpdk_minimal.cpp)
    target_link_libraries(pulsebook_dpdk_minimal PRIVATE pulsebook_headers PkgConfig::DPDK)

    add_executable(pulsebook_dpdk_engine apps/pulsebook_dpdk_engine.cpp)
    target_link_libraries(pulsebook_dpdk_engine PRIVATE pulsebook_headers PkgConfig::DPDK)

    add_executable(pulsebook_packet_generator apps/pulsebook_packet_generator.cpp)
    target_link_libraries(pulsebook_packet_generator PRIVATE pulsebook_headers PkgConfig::DPDK)
endif()

include(cmake/PulseBookWire.cmake)

include(cmake/PulseBookEthernet.cmake)

include(cmake/PulseBookDpdkVirtual.cmake)

include(cmake/PulseBookTestSafety.cmake)

include(cmake/PulseBookEngineIntegration.cmake)

include(cmake/PulseBookDpdkLatency.cmake)

include(cmake/PulseBookDpdkScenarios.cmake)

include(cmake/PulseBookDpdkRegimeBenchmark.cmake)

include(cmake/PulseBookDpdkAfPacket.cmake)

include(cmake/PulseBookDpdkAfPacketLatency.cmake)

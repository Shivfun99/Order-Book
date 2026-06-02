# CMake generated Testfile for 
# Source directory: /home/shiv/Order Book
# Build directory: /home/shiv/Order Book/build-dpdk
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[pulsebook_test_wire_market_data]=] "/home/shiv/Order Book/build-dpdk/pulsebook_test_wire_market_data")
set_tests_properties([=[pulsebook_test_wire_market_data]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/shiv/Order Book/cmake/PulseBookWire.cmake;30;add_test;/home/shiv/Order Book/cmake/PulseBookWire.cmake;0;;/home/shiv/Order Book/CMakeLists.txt;42;include;/home/shiv/Order Book/CMakeLists.txt;0;")
add_test([=[pulsebook_test_wire_order]=] "/home/shiv/Order Book/build-dpdk/pulsebook_test_wire_order")
set_tests_properties([=[pulsebook_test_wire_order]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/shiv/Order Book/cmake/PulseBookWire.cmake;49;add_test;/home/shiv/Order Book/cmake/PulseBookWire.cmake;0;;/home/shiv/Order Book/CMakeLists.txt;42;include;/home/shiv/Order Book/CMakeLists.txt;0;")
add_test([=[pulsebook_test_wire_invalid_packet]=] "/home/shiv/Order Book/build-dpdk/pulsebook_test_wire_invalid_packet")
set_tests_properties([=[pulsebook_test_wire_invalid_packet]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/shiv/Order Book/cmake/PulseBookWire.cmake;68;add_test;/home/shiv/Order Book/cmake/PulseBookWire.cmake;0;;/home/shiv/Order Book/CMakeLists.txt;42;include;/home/shiv/Order Book/CMakeLists.txt;0;")
add_test([=[pulsebook_test_ethernet_market_frame]=] "/home/shiv/Order Book/build-dpdk/pulsebook_test_ethernet_market_frame")
set_tests_properties([=[pulsebook_test_ethernet_market_frame]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/shiv/Order Book/cmake/PulseBookEthernet.cmake;25;add_test;/home/shiv/Order Book/cmake/PulseBookEthernet.cmake;0;;/home/shiv/Order Book/CMakeLists.txt;44;include;/home/shiv/Order Book/CMakeLists.txt;0;")
add_test([=[pulsebook_test_ethernet_order_frame]=] "/home/shiv/Order Book/build-dpdk/pulsebook_test_ethernet_order_frame")
set_tests_properties([=[pulsebook_test_ethernet_order_frame]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/shiv/Order Book/cmake/PulseBookEthernet.cmake;44;add_test;/home/shiv/Order Book/cmake/PulseBookEthernet.cmake;0;;/home/shiv/Order Book/CMakeLists.txt;44;include;/home/shiv/Order Book/CMakeLists.txt;0;")
add_test([=[pulsebook_test_engine_wire_adapter]=] "/home/shiv/Order Book/build-dpdk/pulsebook_test_engine_wire_adapter")
set_tests_properties([=[pulsebook_test_engine_wire_adapter]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/shiv/Order Book/cmake/PulseBookEngineIntegration.cmake;27;add_test;/home/shiv/Order Book/cmake/PulseBookEngineIntegration.cmake;0;;/home/shiv/Order Book/CMakeLists.txt;50;include;/home/shiv/Order Book/CMakeLists.txt;0;")
add_test([=[pulsebook_test_engine_scenarios]=] "/home/shiv/Order Book/build-dpdk/pulsebook_test_engine_scenarios")
set_tests_properties([=[pulsebook_test_engine_scenarios]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/shiv/Order Book/cmake/PulseBookDpdkScenarios.cmake;27;add_test;/home/shiv/Order Book/cmake/PulseBookDpdkScenarios.cmake;0;;/home/shiv/Order Book/CMakeLists.txt;54;include;/home/shiv/Order Book/CMakeLists.txt;0;")
add_test([=[pulsebook_test_dpdk_scenarios_virtual]=] "/home/shiv/Order Book/build-dpdk/pulsebook_dpdk_scenarios" "-l" "0" "--no-pci" "--no-huge" "--in-memory" "--mbuf-pool-ops-name=ring_mp_mc" "--file-prefix=pb_t7")
set_tests_properties([=[pulsebook_test_dpdk_scenarios_virtual]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/shiv/Order Book/cmake/PulseBookDpdkScenarios.cmake;94;add_test;/home/shiv/Order Book/cmake/PulseBookDpdkScenarios.cmake;0;;/home/shiv/Order Book/CMakeLists.txt;54;include;/home/shiv/Order Book/CMakeLists.txt;0;")

# CMake generated Testfile for 
# Source directory: /home/shiv/Order Book
# Build directory: /home/shiv/Order Book/build-dpdk
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[pulsebook_test_wire_market_data]=] "/home/shiv/Order Book/build-dpdk/pulsebook_test_wire_market_data")
set_tests_properties([=[pulsebook_test_wire_market_data]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/shiv/Order Book/CMakeLists.txt;53;add_test;/home/shiv/Order Book/CMakeLists.txt;64;pulsebook_add_test;/home/shiv/Order Book/CMakeLists.txt;0;")
add_test([=[pulsebook_test_wire_order]=] "/home/shiv/Order Book/build-dpdk/pulsebook_test_wire_order")
set_tests_properties([=[pulsebook_test_wire_order]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/shiv/Order Book/CMakeLists.txt;53;add_test;/home/shiv/Order Book/CMakeLists.txt;69;pulsebook_add_test;/home/shiv/Order Book/CMakeLists.txt;0;")
add_test([=[pulsebook_test_wire_invalid_packet]=] "/home/shiv/Order Book/build-dpdk/pulsebook_test_wire_invalid_packet")
set_tests_properties([=[pulsebook_test_wire_invalid_packet]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/shiv/Order Book/CMakeLists.txt;53;add_test;/home/shiv/Order Book/CMakeLists.txt;74;pulsebook_add_test;/home/shiv/Order Book/CMakeLists.txt;0;")
add_test([=[pulsebook_test_ethernet_market_frame]=] "/home/shiv/Order Book/build-dpdk/pulsebook_test_ethernet_market_frame")
set_tests_properties([=[pulsebook_test_ethernet_market_frame]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/shiv/Order Book/CMakeLists.txt;53;add_test;/home/shiv/Order Book/CMakeLists.txt;79;pulsebook_add_test;/home/shiv/Order Book/CMakeLists.txt;0;")
add_test([=[pulsebook_test_ethernet_order_frame]=] "/home/shiv/Order Book/build-dpdk/pulsebook_test_ethernet_order_frame")
set_tests_properties([=[pulsebook_test_ethernet_order_frame]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/shiv/Order Book/CMakeLists.txt;53;add_test;/home/shiv/Order Book/CMakeLists.txt;84;pulsebook_add_test;/home/shiv/Order Book/CMakeLists.txt;0;")
add_test([=[pulsebook_test_engine_wire_adapter]=] "/home/shiv/Order Book/build-dpdk/pulsebook_test_engine_wire_adapter")
set_tests_properties([=[pulsebook_test_engine_wire_adapter]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/shiv/Order Book/CMakeLists.txt;53;add_test;/home/shiv/Order Book/CMakeLists.txt;89;pulsebook_add_test;/home/shiv/Order Book/CMakeLists.txt;0;")
add_test([=[pulsebook_test_engine_scenarios]=] "/home/shiv/Order Book/build-dpdk/pulsebook_test_engine_scenarios")
set_tests_properties([=[pulsebook_test_engine_scenarios]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/shiv/Order Book/CMakeLists.txt;53;add_test;/home/shiv/Order Book/CMakeLists.txt;94;pulsebook_add_test;/home/shiv/Order Book/CMakeLists.txt;0;")
add_test([=[pulsebook_test_dpdk_scenarios_virtual]=] "/home/shiv/Order Book/build-dpdk/pulsebook_dpdk_scenarios" "-l" "0" "--no-pci" "--no-huge" "--in-memory" "--mbuf-pool-ops-name=ring_mp_mc" "--file-prefix=pb_ctest_scenarios")
set_tests_properties([=[pulsebook_test_dpdk_scenarios_virtual]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/shiv/Order Book/CMakeLists.txt;204;add_test;/home/shiv/Order Book/CMakeLists.txt;0;")

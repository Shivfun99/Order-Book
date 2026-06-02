# CMake generated Testfile for 
# Source directory: /home/shiv/Order Book
# Build directory: /home/shiv/Order Book/build-frame
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[pulsebook_test_wire_market_data]=] "/home/shiv/Order Book/build-frame/pulsebook_test_wire_market_data")
set_tests_properties([=[pulsebook_test_wire_market_data]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/shiv/Order Book/cmake/PulseBookWire.cmake;30;add_test;/home/shiv/Order Book/cmake/PulseBookWire.cmake;0;;/home/shiv/Order Book/CMakeLists.txt;42;include;/home/shiv/Order Book/CMakeLists.txt;0;")
add_test([=[pulsebook_test_wire_order]=] "/home/shiv/Order Book/build-frame/pulsebook_test_wire_order")
set_tests_properties([=[pulsebook_test_wire_order]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/shiv/Order Book/cmake/PulseBookWire.cmake;49;add_test;/home/shiv/Order Book/cmake/PulseBookWire.cmake;0;;/home/shiv/Order Book/CMakeLists.txt;42;include;/home/shiv/Order Book/CMakeLists.txt;0;")
add_test([=[pulsebook_test_wire_invalid_packet]=] "/home/shiv/Order Book/build-frame/pulsebook_test_wire_invalid_packet")
set_tests_properties([=[pulsebook_test_wire_invalid_packet]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/shiv/Order Book/cmake/PulseBookWire.cmake;68;add_test;/home/shiv/Order Book/cmake/PulseBookWire.cmake;0;;/home/shiv/Order Book/CMakeLists.txt;42;include;/home/shiv/Order Book/CMakeLists.txt;0;")
add_test([=[pulsebook_test_ethernet_market_frame]=] "/home/shiv/Order Book/build-frame/pulsebook_test_ethernet_market_frame")
set_tests_properties([=[pulsebook_test_ethernet_market_frame]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/shiv/Order Book/cmake/PulseBookEthernet.cmake;25;add_test;/home/shiv/Order Book/cmake/PulseBookEthernet.cmake;0;;/home/shiv/Order Book/CMakeLists.txt;44;include;/home/shiv/Order Book/CMakeLists.txt;0;")
add_test([=[pulsebook_test_ethernet_order_frame]=] "/home/shiv/Order Book/build-frame/pulsebook_test_ethernet_order_frame")
set_tests_properties([=[pulsebook_test_ethernet_order_frame]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/shiv/Order Book/cmake/PulseBookEthernet.cmake;44;add_test;/home/shiv/Order Book/cmake/PulseBookEthernet.cmake;0;;/home/shiv/Order Book/CMakeLists.txt;44;include;/home/shiv/Order Book/CMakeLists.txt;0;")

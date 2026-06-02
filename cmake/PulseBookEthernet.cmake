include_guard(GLOBAL)

include(CTest)

if(NOT TARGET pulsebook_wire)
    message(FATAL_ERROR
        "pulsebook_wire target is missing. Include cmake/PulseBookWire.cmake before cmake/PulseBookEthernet.cmake.")
endif()

if(BUILD_TESTING)
    add_executable(pulsebook_test_ethernet_market_frame
        ${PROJECT_SOURCE_DIR}/tests/test_ethernet_market_frame.cpp
    )

    target_link_libraries(pulsebook_test_ethernet_market_frame PRIVATE
        pulsebook_wire
    )

    target_compile_options(pulsebook_test_ethernet_market_frame PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )

    add_test(
        NAME pulsebook_test_ethernet_market_frame
        COMMAND pulsebook_test_ethernet_market_frame
    )

    add_executable(pulsebook_test_ethernet_order_frame
        ${PROJECT_SOURCE_DIR}/tests/test_ethernet_order_frame.cpp
    )

    target_link_libraries(pulsebook_test_ethernet_order_frame PRIVATE
        pulsebook_wire
    )

    target_compile_options(pulsebook_test_ethernet_order_frame PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )

    add_test(
        NAME pulsebook_test_ethernet_order_frame
        COMMAND pulsebook_test_ethernet_order_frame
    )
endif()

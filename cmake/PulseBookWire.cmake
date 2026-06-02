include_guard(GLOBAL)

include(CTest)

add_library(pulsebook_wire INTERFACE)

target_include_directories(pulsebook_wire INTERFACE
    ${PROJECT_SOURCE_DIR}/include
)

target_compile_features(pulsebook_wire INTERFACE
    cxx_std_20
)

if(BUILD_TESTING)
    add_executable(pulsebook_test_wire_market_data
        ${PROJECT_SOURCE_DIR}/tests/test_wire_market_data.cpp
    )

    target_link_libraries(pulsebook_test_wire_market_data PRIVATE
        pulsebook_wire
    )

    target_compile_options(pulsebook_test_wire_market_data PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )

    add_test(
        NAME pulsebook_test_wire_market_data
        COMMAND pulsebook_test_wire_market_data
    )

    add_executable(pulsebook_test_wire_order
        ${PROJECT_SOURCE_DIR}/tests/test_wire_order.cpp
    )

    target_link_libraries(pulsebook_test_wire_order PRIVATE
        pulsebook_wire
    )

    target_compile_options(pulsebook_test_wire_order PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )

    add_test(
        NAME pulsebook_test_wire_order
        COMMAND pulsebook_test_wire_order
    )

    add_executable(pulsebook_test_wire_invalid_packet
        ${PROJECT_SOURCE_DIR}/tests/test_wire_invalid_packet.cpp
    )

    target_link_libraries(pulsebook_test_wire_invalid_packet PRIVATE
        pulsebook_wire
    )

    target_compile_options(pulsebook_test_wire_invalid_packet PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )

    add_test(
        NAME pulsebook_test_wire_invalid_packet
        COMMAND pulsebook_test_wire_invalid_packet
    )
endif()

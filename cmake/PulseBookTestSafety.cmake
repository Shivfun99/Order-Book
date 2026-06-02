include_guard(GLOBAL)

if(BUILD_TESTING)
    foreach(test_target IN ITEMS
        pulsebook_test_wire_market_data
        pulsebook_test_wire_order
        pulsebook_test_wire_invalid_packet
        pulsebook_test_ethernet_market_frame
        pulsebook_test_ethernet_order_frame
    )
        if(TARGET ${test_target})
            target_compile_options(${test_target} PRIVATE
                -UNDEBUG
                -Werror
            )
        endif()
    endforeach()
endif()

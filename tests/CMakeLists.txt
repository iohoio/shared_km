add_executable(shared_km_protocol_test
    protocol_message_test.cpp
)

target_link_libraries(shared_km_protocol_test
    PRIVATE
        shared_km_protocol
)

add_test(
    NAME shared_km_protocol_test
    COMMAND shared_km_protocol_test
)

add_executable(shared_km_input_injector_test
    input_injector_test.cpp
)

target_link_libraries(shared_km_input_injector_test
    PRIVATE
        shared_km_input
)

add_test(
    NAME shared_km_input_injector_test
    COMMAND shared_km_input_injector_test
)

add_executable(shared_km_input_event_dispatcher_test
    input_event_dispatcher_test.cpp
)

target_link_libraries(shared_km_input_event_dispatcher_test
    PRIVATE
        shared_km_service
)

add_test(
    NAME shared_km_input_event_dispatcher_test
    COMMAND shared_km_input_event_dispatcher_test
)

add_library(shared_km_core
    core/config.cpp
    core/logger.cpp
)

target_include_directories(shared_km_core
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_compile_definitions(shared_km_core
    PUBLIC
        WIN32_LEAN_AND_MEAN
        NOMINMAX
)

target_link_libraries(shared_km_core
    PUBLIC
        ws2_32
        user32
)

add_library(shared_km_protocol
    protocol/message.cpp
)

target_include_directories(shared_km_protocol
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

add_library(shared_km_network
    network/socket_runtime.cpp
    network/tcp_client.cpp
    network/tcp_server.cpp
    network/discovery.cpp
    network/file_transfer.cpp
)

target_include_directories(shared_km_network
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(shared_km_network
    PUBLIC
        shared_km_core
        shared_km_protocol
)

add_library(shared_km_input
    input/input_injector.cpp
    input/mouse_hook.cpp
    input/keyboard_hook.cpp
    input/clipboard_monitor.cpp
    input/raw_input_reader.cpp
)

target_include_directories(shared_km_input
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(shared_km_input
    PUBLIC
        shared_km_core
)

add_library(shared_km_service
    service/input_event_dispatcher.cpp
    service/receiver_app.cpp
    service/sender_app.cpp
)

target_include_directories(shared_km_service
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(shared_km_service
    PUBLIC
        shared_km_core
        shared_km_input
        shared_km_network
        shared_km_protocol
)

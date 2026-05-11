add_executable(shared_km_mouse_hook_probe
    tools/mouse_hook_probe.cpp
)

target_link_libraries(shared_km_mouse_hook_probe
    PRIVATE
        shared_km_core
        shared_km_input
)

add_executable(shared_km_click_target_probe_v3 WIN32
    tools/click_target_probe.cpp
)

target_link_libraries(shared_km_click_target_probe_v3
    PRIVATE
        user32
        gdi32
)

add_executable(shared_km_edge_switch_probe
    tools/edge_switch_probe.cpp
)

target_link_libraries(shared_km_edge_switch_probe
    PRIVATE
        user32
)

# Unified executable — supports --receiver / --sender flags, defaults to UI mode
add_executable(shared_km WIN32
    ui/main.cpp
)

target_link_libraries(shared_km
    PRIVATE
        shell32
        comctl32
        ws2_32
        shared_km_core
        shared_km_network
        shared_km_service
        shared_km_input
        shared_km_protocol
)

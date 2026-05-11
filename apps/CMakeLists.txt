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

# Read and format version from version.txt
file(READ "${PROJECT_SOURCE_DIR}/version.txt" SHARED_KM_BUILD_NUM)
string(STRIP "${SHARED_KM_BUILD_NUM}" SHARED_KM_BUILD_NUM)
if(SHARED_KM_BUILD_NUM MATCHES "^[0-9]+$")
    math(EXPR SHARED_KM_BUILD_NUM "${SHARED_KM_BUILD_NUM}")
    if(SHARED_KM_BUILD_NUM LESS 10)
        set(SHARED_KM_VER_TAG "v0.0${SHARED_KM_BUILD_NUM}")
    else()
        set(SHARED_KM_VER_TAG "v0.${SHARED_KM_BUILD_NUM}")
    endif()
else()
    set(SHARED_KM_VER_TAG "v0.00")
endif()

# Unified executable — supports --receiver / --sender flags, defaults to UI mode
add_executable(shared_km WIN32
    ui/main.cpp
)

set_target_properties(shared_km PROPERTIES
    OUTPUT_NAME "shared_km_${SHARED_KM_VER_TAG}"
)

target_compile_definitions(shared_km PRIVATE
    SHARED_KM_VERSION="${SHARED_KM_VER_TAG}"
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

# Post-build: auto-increment version.txt
add_custom_command(TARGET shared_km POST_BUILD
    COMMAND ${CMAKE_COMMAND} -DPROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}" -P "${PROJECT_SOURCE_DIR}/cmake/version_bump.cmake"
    COMMENT "Incrementing build version"
)

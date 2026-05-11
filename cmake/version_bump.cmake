# Increments version.txt by 1
# Usage: cmake -DPROJECT_SOURCE_DIR=<path> -P cmake/version_bump.cmake

set(VER "0")
if(EXISTS "${PROJECT_SOURCE_DIR}/version.txt")
    file(READ "${PROJECT_SOURCE_DIR}/version.txt" VER)
    string(STRIP "${VER}" VER)
endif()
math(EXPR VER "${VER} + 1")
file(WRITE "${PROJECT_SOURCE_DIR}/version.txt" "${VER}\n")
message(STATUS "Version bumped to ${VER}")

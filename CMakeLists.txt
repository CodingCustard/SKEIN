cmake_minimum_required(VERSION 3.25)

project(
    SKEIN
    VERSION 0.1.0
    DESCRIPTION "SKEIN Engine"
    LANGUAGES CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake")

include(SkeinProjectOptions)
include(SkeinWarnings)

skein_configure_project_options()

add_subdirectory(Source)
add_subdirectory(Tests)

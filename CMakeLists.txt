cmake_minimum_required(VERSION 3.25)

project(
    SKEIN
    VERSION 0.1.0
    DESCRIPTION "SKEIN Engine"
    LANGUAGES C CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake")

include(SkeinProjectOptions)
include(SkeinCompilerChecks)
include(SkeinWarnings)
include(SkeinDependencies)

skein_configure_project_options()
skein_validate_toolchain()
skein_configure_dependencies()

if(SKEIN_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(Source)
add_subdirectory(Tests)

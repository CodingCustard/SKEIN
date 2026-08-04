if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(SKEIN_BUILD_DEBUG_VALUE 1)
    set(SKEIN_BUILD_RELEASE_VALUE 0)
    set(SKEIN_ENABLE_ASSERTS_VALUE 1)
    set(SKEIN_ENABLE_TRACING_VALUE 1)
else()
    set(SKEIN_BUILD_DEBUG_VALUE 0)
    set(SKEIN_BUILD_RELEASE_VALUE 1)
    set(SKEIN_ENABLE_ASSERTS_VALUE 0)
    set(SKEIN_ENABLE_TRACING_VALUE 0)
endif()

set(SKEIN_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/Generated")
configure_file(
    Public/Skein/Foundation/BuildConfig.h.in
    "${SKEIN_GENERATED_INCLUDE_DIR}/Skein/Foundation/BuildConfig.h"
    @ONLY
)

add_library(SkeinFoundation STATIC
    Private/Log.cpp
    Public/Skein/Foundation/Build.h
    Public/Skein/Foundation/Endian.h
    Public/Skein/Foundation/Log.h
    Public/Skein/Foundation/StrongId.h
    Public/Skein/Foundation/Types.h
)

add_library(SKEIN::Foundation ALIAS SkeinFoundation)

target_include_directories(SkeinFoundation
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/Public"
        "${SKEIN_GENERATED_INCLUDE_DIR}"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private"
)

target_link_libraries(SkeinFoundation PUBLIC SKEIN::ProjectOptions)
skein_apply_warnings(SkeinFoundation)

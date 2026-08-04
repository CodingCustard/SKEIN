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
    Private/Arena.cpp
    Private/Assert.cpp
    Private/Log.cpp
    Private/Memory.cpp
    Private/Name.cpp
    Private/Path.cpp
    Private/Pool.cpp
    Private/String.cpp
    Private/Uuid.cpp
    Public/Skein/Foundation/Arena.h
    Public/Skein/Foundation/Assert.h
    Public/Skein/Foundation/Build.h
    Public/Skein/Foundation/Endian.h
    Public/Skein/Foundation/Error.h
    Public/Skein/Foundation/FlatMap.h
    Public/Skein/Foundation/Hash.h
    Public/Skein/Foundation/IntrusiveList.h
    Public/Skein/Foundation/Log.h
    Public/Skein/Foundation/Memory.h
    Public/Skein/Foundation/Name.h
    Public/Skein/Foundation/Path.h
    Public/Skein/Foundation/Pool.h
    Public/Skein/Foundation/Result.h
    Public/Skein/Foundation/RingBuffer.h
    Public/Skein/Foundation/SmallVector.h
    Public/Skein/Foundation/String.h
    Public/Skein/Foundation/StrongId.h
    Public/Skein/Foundation/Types.h
    Public/Skein/Foundation/Uuid.h
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

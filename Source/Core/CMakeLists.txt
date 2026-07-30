add_library(SkeinCore STATIC
    Private/Engine.cpp
    Public/Skein/Core/Engine.h
)

add_library(SKEIN::Core ALIAS SkeinCore)

target_include_directories(SkeinCore
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Public"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private"
)

target_link_libraries(SkeinCore
    PUBLIC
        SKEIN::Foundation
        SKEIN::Platform
    PRIVATE
        SKEIN::ProjectOptions
)

skein_apply_warnings(SkeinCore)

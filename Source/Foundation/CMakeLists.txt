add_library(SkeinFoundation STATIC
    Private/Log.cpp
    Public/Skein/Foundation/Build.h
    Public/Skein/Foundation/Log.h
)

add_library(SKEIN::Foundation ALIAS SkeinFoundation)

target_include_directories(SkeinFoundation
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Public"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private"
)

target_link_libraries(SkeinFoundation PUBLIC SKEIN::ProjectOptions)
skein_apply_warnings(SkeinFoundation)

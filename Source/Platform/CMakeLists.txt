add_library(SkeinPlatform STATIC
    Private/Platform.cpp
    Public/Skein/Platform/Platform.h
)

add_library(SKEIN::Platform ALIAS SkeinPlatform)

target_include_directories(SkeinPlatform
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Public"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private"
)

target_link_libraries(SkeinPlatform
    PUBLIC SKEIN::Foundation
    PRIVATE SKEIN::ProjectOptions
)

if(WIN32)
    target_compile_definitions(SkeinPlatform PRIVATE SKEIN_PLATFORM_WINDOWS=1)
    target_link_libraries(SkeinPlatform PRIVATE dbghelp ole32 shell32 user32)
endif()

skein_apply_warnings(SkeinPlatform)

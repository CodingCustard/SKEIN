add_library(SkeinUI STATIC
    Private/UI.cpp
    Public/Skein/UI/UI.h
)

add_library(SKEIN::UI ALIAS SkeinUI)

target_include_directories(SkeinUI
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Public"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private"
)

target_link_libraries(SkeinUI
    PUBLIC SKEIN::Foundation
    PRIVATE SKEIN::ProjectOptions
)

skein_apply_warnings(SkeinUI)

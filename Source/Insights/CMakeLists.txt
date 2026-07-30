add_library(SkeinTrace STATIC
    Private/Trace.cpp
    Public/Skein/Insights/Trace.h
)

add_library(SKEIN::Trace ALIAS SkeinTrace)

target_include_directories(SkeinTrace
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Public"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private"
)

target_link_libraries(SkeinTrace
    PUBLIC SKEIN::Foundation
    PRIVATE SKEIN::ProjectOptions
)

skein_apply_warnings(SkeinTrace)

if(SKEIN_BUILD_INSIGHTS)
    add_executable(SkeinInsights
        App/Main.cpp
    )

    target_link_libraries(SkeinInsights
        PRIVATE
            SKEIN::ProjectOptions
            SKEIN::Trace
            SKEIN::UI
    )

    skein_apply_warnings(SkeinInsights)
endif()

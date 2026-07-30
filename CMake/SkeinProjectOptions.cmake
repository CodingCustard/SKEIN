include_guard(GLOBAL)

function(skein_configure_project_options)
    option(SKEIN_BUILD_EDITOR "Build SkeinEditor" ON)
    option(SKEIN_BUILD_INSIGHTS "Build SkeinInsights" ON)
    option(SKEIN_BUILD_TESTS "Build SKEIN tests" ON)

    add_library(SkeinProjectOptions INTERFACE)
    add_library(SKEIN::ProjectOptions ALIAS SkeinProjectOptions)

    target_compile_features(SkeinProjectOptions INTERFACE cxx_std_20)

if(MSVC)
    target_compile_definitions(SkeinProjectOptions INTERFACE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        UNICODE
        _UNICODE
    )

    target_compile_options(SkeinProjectOptions INTERFACE
        /permissive-
        /Zc:__cplusplus
        /utf-8
    )

    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        target_compile_options(SkeinProjectOptions INTERFACE
            /Zc:preprocessor
        )
    endif()
endif()

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_definitions(SkeinProjectOptions INTERFACE
            SKEIN_DEBUG=1
            SKEIN_ENABLE_ASSERTS=1
            SKEIN_ENABLE_TRACING=1
        )
    else()
        target_compile_definitions(SkeinProjectOptions INTERFACE
            SKEIN_RELEASE=1
            SKEIN_ENABLE_ASSERTS=0
            SKEIN_ENABLE_TRACING=0
        )
    endif()
endfunction()

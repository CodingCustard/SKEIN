include_guard(GLOBAL)

function(skein_configure_project_options)
    option(SKEIN_BUILD_EDITOR "Build SkeinEditor" ON)
    option(SKEIN_BUILD_INSIGHTS "Build SkeinInsights" ON)
    option(SKEIN_BUILD_TESTS "Build SKEIN tests" ON)

    add_library(SkeinProjectOptions INTERFACE)
    add_library(SKEIN::ProjectOptions ALIAS SkeinProjectOptions)

    target_compile_features(SkeinProjectOptions INTERFACE cxx_std_20)

    if(WIN32)
        target_compile_definitions(SkeinProjectOptions INTERFACE
            _CRT_SECURE_NO_WARNINGS
            NOMINMAX
            WIN32_LEAN_AND_MEAN
            UNICODE
            _UNICODE
        )
    endif()

    if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
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

endfunction()

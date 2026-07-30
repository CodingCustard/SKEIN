include_guard(GLOBAL)

function(skein_apply_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /WX
            /wd4100
        )
    else()
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-unused-parameter
        )
    endif()
endfunction()

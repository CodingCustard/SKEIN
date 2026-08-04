include_guard(GLOBAL)

function(skein_validate_toolchain)
    if(NOT WIN32)
        message(FATAL_ERROR "SKEIN Volume 01 currently supports native Windows builds only.")
    endif()

    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "SKEIN requires an x64 toolchain.")
    endif()

    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" AND
       NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR
            "Unsupported compiler '${CMAKE_CXX_COMPILER_ID}'. "
            "Volume 01 requires MSVC or clang-cl.")
    endif()

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND
       NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        message(FATAL_ERROR
            "Unsupported Clang frontend '${CMAKE_CXX_COMPILER_FRONTEND_VARIANT}'. "
            "Native Windows builds require clang-cl.")
    endif()

    if(NOT "cxx_std_20" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
        message(FATAL_ERROR
            "Compiler '${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}' "
            "does not advertise the required C++20 feature set.")
    endif()

    message(STATUS
        "SKEIN toolchain: ${CMAKE_CXX_COMPILER_ID} "
        "${CMAKE_CXX_COMPILER_VERSION}, "
        "frontend ${CMAKE_CXX_COMPILER_FRONTEND_VARIANT}, x64")
endfunction()

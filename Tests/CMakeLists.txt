if(NOT SKEIN_BUILD_TESTS)
    return()
endif()

include(CTest)
enable_testing()

add_executable(SkeinSmokeTests
    SmokeTests.cpp
)

target_link_libraries(SkeinSmokeTests
    PRIVATE
        SKEIN::ProjectOptions
        SKEIN::Core
)

skein_apply_warnings(SkeinSmokeTests)

add_test(NAME SkeinSmokeTests COMMAND SkeinSmokeTests)

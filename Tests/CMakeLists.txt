if(NOT SKEIN_BUILD_TESTS)
    return()
endif()

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

add_executable(SkeinBuildConfigurationTests
    Foundation/BuildConfigurationTests.cpp
)

target_link_libraries(SkeinBuildConfigurationTests
    PRIVATE
        SKEIN::ProjectOptions
        SKEIN::Foundation
)

skein_apply_warnings(SkeinBuildConfigurationTests)

add_test(
    NAME Skein.Volume01.Chapter01_04.BuildConfiguration
    COMMAND SkeinBuildConfigurationTests
)

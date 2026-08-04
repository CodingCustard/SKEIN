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

add_executable(SkeinFoundationTypesTests
    Foundation/FoundationTypesTests.cpp
)

target_link_libraries(SkeinFoundationTypesTests
    PRIVATE
        SKEIN::ProjectOptions
        SKEIN::Foundation
)

skein_apply_warnings(SkeinFoundationTypesTests)

add_test(
    NAME Skein.Volume01.Chapter01_05.FoundationTypes
    COMMAND SkeinFoundationTypesTests
)

add_executable(SkeinErrorResultTests
    Foundation/ErrorResultTests.cpp
)

target_link_libraries(SkeinErrorResultTests
    PRIVATE
        SKEIN::ProjectOptions
        SKEIN::Foundation
)

skein_apply_warnings(SkeinErrorResultTests)

add_test(
    NAME Skein.Volume01.Chapter01_06.ErrorResultAssertions
    COMMAND SkeinErrorResultTests
)

add_executable(SkeinStringsPathsIdentifiersTests
    Foundation/StringsPathsIdentifiersTests.cpp
)

target_link_libraries(SkeinStringsPathsIdentifiersTests
    PRIVATE
        SKEIN::ProjectOptions
        SKEIN::Foundation
)

skein_apply_warnings(SkeinStringsPathsIdentifiersTests)

add_test(
    NAME Skein.Volume01.Chapter01_07.StringsPathsIdentifiers
    COMMAND SkeinStringsPathsIdentifiersTests
)

add_executable(SkeinContainerTests
    Foundation/ContainerTests.cpp
)

target_link_libraries(SkeinContainerTests
    PRIVATE
        SKEIN::ProjectOptions
        SKEIN::Foundation
)

skein_apply_warnings(SkeinContainerTests)

add_test(
    NAME Skein.Volume01.Chapter01_08.Containers
    COMMAND SkeinContainerTests
)

add_executable(SkeinMemoryArchitectureTests
    Foundation/MemoryArchitectureTests.cpp
)

target_link_libraries(SkeinMemoryArchitectureTests
    PRIVATE
        SKEIN::ProjectOptions
        SKEIN::Foundation
)

skein_apply_warnings(SkeinMemoryArchitectureTests)

add_test(
    NAME Skein.Volume01.Chapter01_09.MemoryArchitecture
    COMMAND SkeinMemoryArchitectureTests
)

add_executable(SkeinWindowsPlatformTests
    Platform/WindowsPlatformTests.cpp
)

target_link_libraries(SkeinWindowsPlatformTests
    PRIVATE
        SKEIN::ProjectOptions
        SKEIN::Platform
)

skein_apply_warnings(SkeinWindowsPlatformTests)

add_test(
    NAME Skein.Volume01.Chapter01_10.WindowsPlatform
    COMMAND SkeinWindowsPlatformTests
)

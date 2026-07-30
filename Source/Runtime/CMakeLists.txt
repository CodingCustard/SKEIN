add_executable(SkeinRuntime
    Main.cpp
)

target_link_libraries(SkeinRuntime
    PRIVATE
        SKEIN::ProjectOptions
        SKEIN::Core
        SKEIN::Trace
)

skein_apply_warnings(SkeinRuntime)

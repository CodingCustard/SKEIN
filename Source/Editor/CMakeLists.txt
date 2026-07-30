add_executable(SkeinEditor
    Main.cpp
)

target_link_libraries(SkeinEditor
    PRIVATE
        SKEIN::ProjectOptions
        SKEIN::Core
        SKEIN::Trace
        SKEIN::UI
)

skein_apply_warnings(SkeinEditor)

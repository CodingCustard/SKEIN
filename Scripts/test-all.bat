@echo off
setlocal

for %%P in (msvc-debug msvc-release clang-debug clang-release) do (
    echo.
    echo === Testing %%P ===
    ctest --preset %%P
    if errorlevel 1 exit /b 1
)

echo.
echo All SKEIN presets passed.

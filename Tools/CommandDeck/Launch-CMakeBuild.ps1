# Copyright Coding Custard Studios.

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $VsInstall,

    [Parameter(Mandatory = $true)]
    [string] $RepositoryRoot,

    [string] $Arch = "amd64",
    [string] $HostArch = "amd64",

    [switch] $NoStartup
)

$ErrorActionPreference = "Stop"

$devShell = Join-Path $VsInstall "Common7\Tools\Launch-VsDevShell.ps1"
if (-not (Test-Path -LiteralPath $devShell -PathType Leaf)) {
    throw "Visual Studio developer shell was not found: $devShell"
}
if (-not (Test-Path -LiteralPath $RepositoryRoot -PathType Container)) {
    throw "SKEIN repository does not exist: $RepositoryRoot"
}
$presets = Join-Path $RepositoryRoot "CMakePresets.json"
if (-not (Test-Path -LiteralPath $presets -PathType Leaf)) {
    throw "CMakePresets.json does not exist: $presets"
}

& $devShell -Arch $Arch -HostArch $HostArch -SkipAutomaticLocation
Set-Location -LiteralPath $RepositoryRoot
$Host.UI.RawUI.WindowTitle = "SKEIN - CMAKE BUILD"

Write-Host ""
Write-Host "SKEIN CMake build environment ready." -ForegroundColor DarkYellow
Write-Host "Repository: $RepositoryRoot" -ForegroundColor DarkGray
Write-Host ""

if (-not $NoStartup) {
    Write-Host "Configure and build presets:" -ForegroundColor DarkYellow
    Write-Host "  cmake --preset msvc-debug"
    Write-Host "  cmake --build --preset msvc-debug"
    Write-Host "  ctest --preset msvc-debug"
    Write-Host ""
    Write-Host "Available presets:" -ForegroundColor DarkYellow
    & cmake --list-presets
    Write-Host ""
}

# Copyright Coding Custard Studios.

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $VsInstall,

    [Parameter(Mandatory = $true)]
    [string] $RepositoryRoot,

    [Parameter(Mandatory = $true)]
    [string] $Preset,

    [string] $Arch = "amd64",
    [string] $HostArch = "amd64",

    [switch] $NoBuild
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

if (-not $NoBuild) {
    & cmake --preset $Preset
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    & cmake --build --preset $Preset
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

& ctest --preset $Preset --output-on-failure
exit $LASTEXITCODE

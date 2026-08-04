# Copyright Coding Custard Studios.

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $VsInstall,

    [Parameter(Mandatory = $true)]
    [string] $RepositoryRoot,

    [string] $Arch = "amd64",
    [string] $HostArch = "amd64"
)

$ErrorActionPreference = "Stop"

$devShell = Join-Path $VsInstall "Common7\Tools\Launch-VsDevShell.ps1"
if (-not (Test-Path -LiteralPath $devShell -PathType Leaf)) {
    throw "Visual Studio developer shell was not found: $devShell"
}

if (-not (Test-Path -LiteralPath $RepositoryRoot -PathType Container)) {
    throw "SKEIN repository does not exist: $RepositoryRoot"
}

& $devShell `
    -Arch $Arch `
    -HostArch $HostArch `
    -SkipAutomaticLocation

Set-Location -LiteralPath $RepositoryRoot
$Host.UI.RawUI.WindowTitle = "SKEIN - VS DEVELOPER"

Write-Host ""
Write-Host "SKEIN Visual Studio developer environment ready." -ForegroundColor DarkYellow
Write-Host "Repository: $RepositoryRoot" -ForegroundColor DarkGray
Write-Host ""

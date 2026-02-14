param(
  [string]$Config = "Debug"
)

$ErrorActionPreference = "Stop"

# Repo root
$root  = Split-Path -Parent $MyInvocation.MyCommand.Path
$root  = Split-Path -Parent $root

$build = Join-Path $root "build"

# Build plugin
cmake --build $build --config $Config --target TwoCCompressor_VST3

# Source path (what JUCE builds)
$src = Join-Path $root "build\TwoCCompressor_artefacts\$Config\VST3\2C-Compressor.vst3"

# User VST3 install path (no admin required)
$dst = Join-Path $env:LOCALAPPDATA "Programs\Common\VST3"

# Create folder if missing
if (!(Test-Path $dst)) {
    New-Item -ItemType Directory -Path $dst | Out-Null
}

if (!(Test-Path $src)) {
    throw "Built plugin not found: $src"
}

Copy-Item -Recurse -Force $src $dst

Write-Host ""
Write-Host "Installed to:"
Write-Host $dst
Write-Host ""

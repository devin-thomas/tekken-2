# Windows equivalent of build.sh: configure and build the native runtime from generated/
# (run generate.ps1 first).
#   pwsh -File scripts\build.ps1 [-Config game.toml] [-BuildType Release|RelWithDebInfo]
#        [-BuildDir build-release] [-Jobs 4] [-DebugTools] [-Release] [-Msvc]
# -DebugTools (or PSX_DEBUG_TOOLS=ON) builds the loopback TCP debug server in; use its own
# build dir, because the option is cached per build dir. A plain Release build can still
# open the debug server at run time with PSX_DEBUG_SERVER=1.
# -Release (or T2_RELEASE=ON) builds the player release's settings panel (scripts\release.ps1).
param([string]$Config = 'game.toml',
      [ValidateSet('Release', 'RelWithDebInfo')][string]$BuildType = 'Release',
      [string]$BuildDir = 'build-release', [int]$Jobs = 4, [switch]$DebugTools, [switch]$Release, [switch]$Msvc)
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
. (Join-Path $PSScriptRoot 'windows_env.ps1') -Msvc:$Msvc

Invoke-T2Python @('scripts\project_config.py', '--config', $Config)
if (-not (Test-Path 'psxrecomp\runtime\runtime.cmake')) {
    throw 'psxrecomp submodule missing; run: git submodule update --init --recursive recomp/psxrecomp'
}
# Tekken 2-owned framework fixes (patches/*.patch), applied to the pinned submodule.
Invoke-T2Python @('scripts\apply_framework_patches.py')

Write-Host "=== Tekken 2 Recomp: building psx-runtime ($BuildType, $Jobs jobs) ==="
$configure = @('-S', '.', '-B', $BuildDir, '-G', 'Ninja', "-DCMAKE_BUILD_TYPE=$BuildType",
               "-DGAME_CONFIG=$Config", "-DPython3_EXECUTABLE=$T2Python")
$debug = if ($DebugTools) { 'ON' } else { $env:PSX_DEBUG_TOOLS }
if ($debug) { $configure += "-DPSX_DEBUG_TOOLS=$debug" }
$t2Release = if ($Release) { 'ON' } else { $env:T2_RELEASE }
if ($t2Release) { $configure += "-DT2_RELEASE=$t2Release" }
cmake @configure
if ($LASTEXITCODE) { throw "cmake configure failed ($LASTEXITCODE)" }
cmake --build $BuildDir --target psx-runtime --parallel $Jobs
if ($LASTEXITCODE) { throw "cmake build failed ($LASTEXITCODE)" }
Write-Host '=== Build complete ==='

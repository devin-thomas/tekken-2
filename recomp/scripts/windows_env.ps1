# Dot-sourced by generate.ps1, build.ps1 and package_windows.ps1. Sets up this PowerShell
# session only (no registry, no profile): a real Python 3.11+ and the cmake-clang-v1 pack
# (llvm-mingw clang, CMake, Ninja, ccache, SDL3, zlib), which psxrecomp recommends on Windows.
#   -Msvc  use Visual Studio 2022's cl (vcvars64) instead of the pack's clang; cmake and
#          ninja still come from the pack.
# Why: the Microsoft Store python stubs can sit ahead of the real Python on PATH, and with
# CC unset CMake can pick another MinGW gcc on the machine instead of the pack.
param([switch]$Msvc)

function Test-T2Python([string]$Exe) {
    if (-not $Exe -or -not (Test-Path -LiteralPath $Exe)) { return $false }
    try { & $Exe -c "import sys; sys.exit(0 if sys.version_info >= (3, 11) else 1)" 2>$null | Out-Null }
    catch { return $false }
    return $LASTEXITCODE -eq 0
}

$candidates = @()
if ($env:T2_PYTHON) { $candidates += $env:T2_PYTHON }
if (Get-Command py -ErrorAction SilentlyContinue) {
    $candidates += (& py -3 -c "import sys; print(sys.executable)" 2>$null)
}
$candidates += @(Get-Command python, python3 -All -ErrorAction SilentlyContinue | ForEach-Object Source)
$script:T2Python = $candidates | Where-Object { Test-T2Python $_ } | Select-Object -First 1
if (-not $T2Python) { throw "Python 3.11+ not found: install it or set T2_PYTHON to python.exe" }

$T2Root = Split-Path $PSScriptRoot -Parent
# psxrecomp's shared pack cache (tools/toolchain_pack.py): %LOCALAPPDATA%\retcomm or ~\.local\share\retcomm.
function Find-T2Pack {
    $roots = @()
    if ($env:RETCOMM_TOOLCHAIN_DIR) { $roots += $env:RETCOMM_TOOLCHAIN_DIR }
    if ($env:LOCALAPPDATA) { $roots += Join-Path $env:LOCALAPPDATA 'retcomm\toolchains\cmake-clang-v1\latest' }
    $roots += Join-Path $HOME '.local\share\retcomm\toolchains\cmake-clang-v1\latest'
    $roots | Where-Object { Test-Path (Join-Path $_ 'bin\cmake.exe') } | Select-Object -First 1
}
$pack = Find-T2Pack
if (-not $pack) {
    Write-Host 'cmake-clang-v1 pack not found; asking psxrecomp to resolve it'
    & $T2Python (Join-Path $T2Root 'psxrecomp\psxrecomp_cli.py') ensure-toolchain
    if ($LASTEXITCODE) { throw "psxrecomp ensure-toolchain failed ($LASTEXITCODE)" }
    $pack = Find-T2Pack
    if (-not $pack) { throw 'cmake-clang-v1 pack not found (set RETCOMM_TOOLCHAIN_DIR)' }
}
$script:T2Pack = $pack

function Add-T2PathFront([string]$Dir) {
    $keep = @($env:PATH -split ';' | Where-Object { $_ -and ($_.TrimEnd('\') -ine $Dir.TrimEnd('\')) })
    $env:PATH = (@($Dir) + $keep) -join ';'
}

if ($Msvc) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw "-Msvc: no Visual Studio with the x64 C++ tools found" }
    $vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
    cmd /c "`"$vcvars`" >nul && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "Env:$($Matches[1])" -Value $Matches[2] }
    }
    $keep = @($env:PATH -split ';' | Where-Object { $_ -and ($_.TrimEnd('\') -ine "$pack\bin") })
    $env:PATH = (@($keep) + "$pack\bin") -join ';'   # cmake, ninja; cl stays first
    $env:CC = 'cl'; $env:CXX = 'cl'
} else {
    Add-T2PathFront "$pack\bin"
    $env:CC     = "$pack\bin\clang.exe"
    $env:CXX    = "$pack\bin\clang++.exe"
    $env:AR     = "$pack\bin\llvm-ar.exe"
    $env:RANLIB = "$pack\bin\llvm-ranlib.exe"
    $env:RETCOMM_TOOLCHAIN_DIR   = $pack
    $env:PSXRECOMP_TOOLCHAIN_DIR = $pack
    if (Test-Path "$pack\deps\include\zlib.h") { $env:ZLIB_ROOT = "$pack\deps" }
    if (Test-Path "$pack\deps\lib\cmake\SDL3\SDL3Config.cmake") { $env:SDL3_DIR = "$pack\deps\lib\cmake\SDL3" }
}
Add-T2PathFront (Join-Path (Split-Path $T2Python) 'Scripts')
Add-T2PathFront (Split-Path $T2Python)

function Invoke-T2Python([string[]]$PyArgs) {
    & $T2Python @PyArgs
    if ($LASTEXITCODE) { throw "python $($PyArgs -join ' ') failed ($LASTEXITCODE)" }
}

Write-Host ("Tekken 2 Windows env: python {0}; {1}" -f $T2Python,
    $(if ($Msvc) { 'MSVC (vcvars64)' } else { "cmake-clang-v1 at $pack" }))

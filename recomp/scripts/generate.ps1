# Windows equivalent of generate.sh: emit the game C under generated/ (gitignored).
#   pwsh -File scripts\generate.ps1 [-Config game.toml] [-Disc path\to\disc.cue] [-Bios SCPH1001.BIN] [-Msvc]
param([string]$Config = 'game.toml', [string]$Disc, [string]$Bios, [switch]$Msvc)
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
. (Join-Path $PSScriptRoot 'windows_env.ps1') -Msvc:$Msvc

Write-Host '=== Tekken 2 Recomp: code generation ==='
if (-not (Test-Path 'psxrecomp\psxrecomp_cli.py')) {
    throw 'psxrecomp submodule missing; run: git submodule update --init --recursive recomp/psxrecomp'
}
Invoke-T2Python @('scripts\project_config.py', '--config', $Config)
Invoke-T2Python @('-B', 'scripts\sync_symbols.py', '--check')
# Tekken 2-owned framework fixes (patches/*.patch), applied to the pinned submodule.
Invoke-T2Python @('scripts\apply_framework_patches.py')

$cli = @('psxrecomp\psxrecomp_cli.py', 'generate', '--config', $Config, '--project-root', '.')
if ($Disc) { $cli += @('--disc', $Disc) }
if ($Bios) { $cli += @('--bios', $Bios) }
Invoke-T2Python $cli
Write-Host '=== Code generation complete ==='

# release.ps1: build, test, package, zip and audit the Windows release of Tekken 2 Recompiled
# from a clean commit. The Mac counterpart is release.sh.
#   pwsh -File scripts\release.ps1 -Out <dir> -Disc <Tekken 2 (USA) (Rev 1).cue> [-BuildDir build-player] [-Jobs 4]
# With release\ICON "default", the app icon is built from that disc first
# (assets\icon\src\extract_icon.py, in a venv under <dir>); "fallback" ships the psxrecomp logo.
# Needs generated\ (scripts\generate.ps1, or a generated\ copied from another machine with matching hashes).
# Writes to <dir>:
#   Tekken-2-Recompiled-<version>-Windows-x64.zip   the player download
#   Tekken-2-Recompiled-<version>-Windows-x64.zip.sha256
#   release-windows.json      private build record (commit, pin, sizes): never published
#   audit-windows.json        tools\release_audit.py's report
# The zip holds one folder, "Tekken 2 Recompiled <version>", with the launcher exe, runtime\,
# README.txt, CHANGELOG.txt, CREDITS.txt and LICENSES\.
param([Parameter(Mandatory)][string]$Out, [string]$Disc, [string]$BuildDir = 'build-player', [int]$Jobs = 4)
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
. (Join-Path $PSScriptRoot 'windows_env.ps1')
function Die([string]$m) { throw "release: $m" }

$version = (Get-Content -LiteralPath 'VERSION' -TotalCount 1).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') { Die "VERSION is not major.minor.patch: $version" }
if (-not (Test-Path 'generated')) { Die 'no generated\ (run scripts\generate.ps1 first)' }
$dirty = git status --porcelain --untracked-files=no -- . ':(exclude)psxrecomp'
if ($dirty) { Die "uncommitted changes:`n$($dirty -join "`n")" }
$commit = (git rev-parse HEAD).Trim()
$pin = ((git ls-tree HEAD psxrecomp) -split '\s+')[2]

Write-Host "=== Tekken 2 Recompiled $version for Windows x64 from $($commit.Substring(0, 12)) ==="
# A clean build from the pinned framework: undo earlier patch applications (patches may
# have changed since), so build.ps1 applies today's stack to a pristine submodule.
if (Test-Path $BuildDir) { Remove-Item $BuildDir -Recurse -Force }
git -C psxrecomp checkout -q -- .
git -C psxrecomp clean -fdq
& (Join-Path $PSScriptRoot 'build.ps1') -BuildDir $BuildDir -Jobs $Jobs -Release
cmake --build $BuildDir --parallel $Jobs
if ($LASTEXITCODE) { Die "test build failed ($LASTEXITCODE)" }
ctest --test-dir $BuildDir --output-on-failure
if ($LASTEXITCODE) { Die "ctest failed ($LASTEXITCODE)" }
$cache = Get-Content (Join-Path $BuildDir 'CMakeCache.txt')
if (-not ($cache -match '^T2_RELEASE:BOOL=ON')) { Die "$BuildDir was not configured with T2_RELEASE=ON" }
if (-not ($cache -match '^PSX_DEBUG_TOOLS:BOOL=OFF')) { Die "$BuildDir has PSX_DEBUG_TOOLS on" }

New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path $Out).Path
$stage = Join-Path $Out 'stage-windows'
if (Test-Path $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
# release\ICON: "default" builds assets\icon\default from the disc and ships it;
# "fallback" ships the psxrecomp logo.
$icon = (Get-Content -LiteralPath 'release\ICON' -TotalCount 1).Trim()
if ($icon -notin 'default', 'fallback') { Die "release\ICON must be default or fallback, not '$icon'" }
if ($icon -eq 'default') {
    if (-not $Disc -or -not (Test-Path -LiteralPath $Disc)) { Die 'release\ICON is default: pass -Disc <the Tekken 2 (USA) (Rev 1) .cue>' }
    if (Test-Path 'assets\icon\default') { Remove-Item 'assets\icon\default' -Recurse -Force }
    $venv = Join-Path $Out 'icon-venv'
    if (-not (Test-Path (Join-Path $venv 'Scripts\python.exe'))) { Invoke-T2Python @('-m', 'venv', $venv) }
    & (Join-Path $venv 'Scripts\python.exe') -m pip install -q -r 'assets\icon\src\requirements.txt'
    if ($LASTEXITCODE) { Die 'pip install for the icon failed' }
    # -X utf8: the script prints the logo's kanji, which the Windows console code page can't.
    & (Join-Path $venv 'Scripts\python.exe') -X utf8 -I 'assets\icon\src\extract_icon.py' --disc $Disc
    if ($LASTEXITCODE) { Die 'the icon build failed' }
    if (-not (Test-Path 'assets\icon\default\windows\app.ico')) { Die 'the icon build made no windows\app.ico' }
}
& (Join-Path $PSScriptRoot 'package_windows.ps1') -BuildDir $BuildDir -Out (Join-Path $stage 'app') -Icon $icon

$name = "Tekken 2 Recompiled $version"
$dir = Join-Path $stage $name
Move-Item -LiteralPath (Join-Path $stage 'app\Tekken 2 Recompiled') $dir
Copy-Item 'release\README.txt', 'release\CHANGELOG.txt', 'release\CREDITS.txt', 'release\CONTRIBUTORS.txt' $dir
Copy-Item -LiteralPath (Join-Path $dir 'runtime\licenses') (Join-Path $dir 'LICENSES') -Recurse
# Windows readers: CRLF line ends in the text files players open.
foreach ($t in Get-ChildItem $dir -File -Filter *.txt) {
    $text = [IO.File]::ReadAllText($t.FullName) -replace "`r?`n", "`r`n"
    [IO.File]::WriteAllText($t.FullName, $text)
}

$zip = "Tekken-2-Recompiled-$version-Windows-x64.zip"
$zipPath = Join-Path $Out $zip
Remove-Item -LiteralPath $zipPath, "$zipPath.sha256" -ErrorAction SilentlyContinue
# tar.exe (bsdtar, in Windows 10 1803 and later) writes forward-slash entry names, unlike
# Compress-Archive on Windows PowerShell.
Push-Location $stage
try { tar.exe -a -c -f $zipPath $name; if ($LASTEXITCODE) { Die 'zip failed' } } finally { Pop-Location }
$sha = (Get-FileHash -Algorithm SHA256 $zipPath).Hash.ToLower()
Set-Content -LiteralPath "$zipPath.sha256" "$sha  $zip" -Encoding ascii -NoNewline

if (Test-Path 'tools\release_audit.py') {  # the maintainers' privacy audit; not in the public repo
    Invoke-T2Python @('-I', 'tools\release_audit.py', $zipPath, '--deny', $commit, '--deny', $commit.Substring(0, 8),
                      '--deny', $pin, '--deny', $pin.Substring(0, 8), '--json', (Join-Path $Out 'audit-windows.json'))
}

$size = (Get-Item $zipPath).Length
@{ version = $version; platform = 'Windows x64'; commit = $commit; psxrecomp = $pin; zip = $zip; bytes = $size;
   sha256 = $sha; built = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ') } |
    ConvertTo-Json | Set-Content (Join-Path $Out 'release-windows.json') -Encoding utf8
Write-Host "RELEASE: $zipPath ($size bytes, sha256 $sha)"

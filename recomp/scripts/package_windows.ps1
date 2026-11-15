# Package the Windows release folder, the counterpart of package_macos.sh.
#   pwsh -File scripts\package_windows.ps1 [-BuildDir build-release] [-Out scratch\win-app]
#        [-Shortcut "$HOME\Desktop\Tekken 2 Recompiled.lnk"] [-Icon default|fallback]
# -Icon fallback leaves assets\icon\default out (the psxrecomp logo instead), while an icon
# is held back from release (release\ICON).
# Layout of <Out>\Tekken 2 Recompiled\:
#   Tekken 2 Recompiled.exe   launcher (scripts/windows_launcher.c): mirrors runtime\ into
#                             %LOCALAPPDATA%\Tekken 2 Recompiled\ and runs the game from there,
#                             so settings, disc.cfg, saves and reports never land in the install
#   runtime\                  Tekken_2_Recompiled.exe, any non-system DLLs it imports, bios\
#                             (OpenBIOS), assets\, mods\, game.toml (no disc line), licenses\
# The disc is never bundled. On a launch with no remembered disc the runtime shows its own
# Open dialog and remembers the choice in disc.cfg in the per-user folder.
# Gates: every PE imports only system DLLs or DLLs shipped beside it; no build-machine paths
# in the runtime exe; OpenBIOS is 512 KiB; no disc images, boot EXE, memory cards, settings,
# disc.cfg or generated C in the package.
param([string]$BuildDir = 'build-release', [string]$Out = 'scratch\win-app', [string]$Shortcut,
      [ValidateSet('default', 'fallback')][string]$Icon = 'default')
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
. (Join-Path $PSScriptRoot 'windows_env.ps1')
$project = (Get-Location).Path
function Die([string]$m) { throw "package_windows: $m" }

$runtimeExe = 'Tekken_2_Recompiled.exe'
$bin = Join-Path $BuildDir $runtimeExe
if (-not (Test-Path $bin)) { Die "no $bin; run scripts\build.ps1 first" }
New-Item -ItemType Directory -Force $Out | Out-Null
$app = Join-Path (Resolve-Path $Out).Path 'Tekken 2 Recompiled'
if (Test-Path $app) { Remove-Item -LiteralPath $app -Recurse -Force }
$rt = Join-Path $app 'runtime'
New-Item -ItemType Directory -Force $rt | Out-Null

Copy-Item $bin (Join-Path $rt $runtimeExe)
foreach ($name in 'bios', 'assets', 'mods') {
    $src = Join-Path $BuildDir $name
    if (-not (Test-Path $src)) { Die "missing $src" }
    Copy-Item -LiteralPath $src -Destination (Join-Path $rt $name) -Recurse
}
Get-ChildItem (Join-Path $rt 'mods') -Recurse -File |
    Where-Object { $_.Name -in 'state.toml', 'state.toml.tmp' } | Remove-Item -Force
# game.toml without its comments or disc line: the shipped relative path never exists in the per-user
# folder, and with no [game] disc the runtime goes to disc.cfg, then its Open dialog.
$toml = Get-Content -LiteralPath 'game.toml'
if (@($toml | Where-Object { $_ -match '^disc = ' }).Count -ne 1) { Die "game.toml must have exactly one 'disc = ' line" }
$toml | Where-Object { $_ -notmatch '^disc = ' -and $_ -notmatch '^\s*#' } | Set-Content -LiteralPath (Join-Path $rt 'game.toml') -Encoding utf8NoBOM
# Licences and notices (the same set as package_macos.sh; SDL3 is static, its zlib notice is
# in release\licenses).
$lic = Join-Path $rt 'licenses'
New-Item -ItemType Directory -Force $lic | Out-Null
Copy-Item 'psxrecomp\runtime\licenses\*' $lic
Copy-Item 'psxrecomp\LICENSE' (Join-Path $lic 'psxrecomp-LICENSE.txt')
Copy-Item 'psxrecomp\PERMISSIONS.md' (Join-Path $lic 'psxrecomp-PERMISSIONS.md')
Copy-Item 'psxrecomp\bios\OpenBIOS.LICENSE' (Join-Path $lic 'OpenBIOS.LICENSE')
Copy-Item 'src\fonts\LICENSE-NotoSans-OFL.txt' (Join-Path $lic 'NotoSans-OFL.txt')
Copy-Item 'src\third_party\stb\LICENSE' (Join-Path $lic 'stb_truetype-LICENSE.txt')
Copy-Item 'release\licenses\*' $lic

# Non-system DLLs the runtime imports go beside it (none for a static MinGW Release build).
$objdump = Join-Path $T2Pack 'bin\llvm-objdump.exe'
function Get-Imports([string]$pe) {
    & $objdump -p $pe | Select-String '^\s*DLL Name:\s*(\S+)' | ForEach-Object { $_.Matches[0].Groups[1].Value }
}
$system = Join-Path $env:SystemRoot 'System32'
$searchDirs = @((Join-Path $T2Pack 'bin'), (Join-Path $T2Pack 'deps\bin'), (Join-Path $T2Pack 'x86_64-w64-mingw32\bin'))
$queue = [System.Collections.Generic.Queue[string]]::new()
$queue.Enqueue((Join-Path $rt $runtimeExe))
while ($queue.Count) {
    foreach ($dll in Get-Imports $queue.Dequeue()) {
        if ($dll -like 'api-ms-win-*' -or $dll -like 'ext-ms-*' -or (Test-Path (Join-Path $system $dll))) { continue }
        $dest = Join-Path $rt $dll
        if (Test-Path $dest) { continue }
        $src = $searchDirs | ForEach-Object { Join-Path $_ $dll } | Where-Object { Test-Path $_ } | Select-Object -First 1
        if (-not $src) { Die "$dll is imported but not found in the toolchain pack" }
        Copy-Item $src $dest
        Write-Host "bundled $dll from $src"
        $queue.Enqueue($dest)
    }
}

# Launcher, with the project's icon (assets\icon\default\windows\app.ico) or, until that lands, the psxrecomp
# logo (PNG-in-ICO, Windows Vista and later), and a version resource from VERSION.
$version = (Get-Content -LiteralPath 'VERSION' -TotalCount 1).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') { Die "VERSION must be major.minor.patch, not '$version'" }
$vparts = ($version.Split('.') + '0') -join ','
$tmp = Join-Path $env:TEMP ("t2-launcher-" + [guid]::NewGuid())
New-Item -ItemType Directory $tmp | Out-Null
try {
    $png = Join-Path $BuildDir 'assets\psxrecomp.png'
    $ownIco = if ($Icon -eq 'default') { Get-Item 'assets\icon\default\windows\app.ico' -ErrorAction SilentlyContinue }
    $objs = @()
    $rc = @()
    if ($ownIco) {
        Copy-Item -LiteralPath $ownIco.FullName (Join-Path $tmp 'icon.ico')
        $rc += '1 ICON "icon.ico"'
        Write-Host "icon from $($ownIco.FullName)"
    } elseif (Test-Path $png) {
        Add-Type -AssemblyName System.Drawing
        $src = [System.Drawing.Image]::FromFile((Resolve-Path $png).Path)
        $w = [Math]::Min(256, $src.Width)
        $bmp = New-Object System.Drawing.Bitmap $w, $w
        $gfx = [System.Drawing.Graphics]::FromImage($bmp)
        $gfx.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $gfx.DrawImage($src, 0, 0, $w, $w); $gfx.Dispose(); $src.Dispose()
        $ms = [IO.MemoryStream]::new(); $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
        $bytes = $ms.ToArray()
        if ($true) {
            $ico = [IO.MemoryStream]::new(); $bw = [IO.BinaryWriter]::new($ico)
            $bw.Write([uint16]0); $bw.Write([uint16]1); $bw.Write([uint16]1)
            $bw.Write([byte]($w % 256)); $bw.Write([byte]($w % 256)); $bw.Write([byte]0); $bw.Write([byte]0)
            $bw.Write([uint16]1); $bw.Write([uint16]32); $bw.Write([uint32]$bytes.Length); $bw.Write([uint32]22)
            $bw.Write($bytes); $bw.Flush()
            [IO.File]::WriteAllBytes((Join-Path $tmp 'icon.ico'), $ico.ToArray())
            $rc += '1 ICON "icon.ico"'
            Write-Host "icon from the psxrecomp logo (fallback: -Icon $Icon or no assets\icon\default)"
        }
    }
    $rc += @(
        "1 VERSIONINFO FILEVERSION $vparts PRODUCTVERSION $vparts",
        'BEGIN', ' BLOCK "StringFileInfo"', ' BEGIN', '  BLOCK "040904B0"', '  BEGIN',
        '   VALUE "ProductName", "Tekken 2 Recompiled"',
        '   VALUE "FileDescription", "Tekken 2 Recompiled"',
        "   VALUE `"ProductVersion`", `"$version`"",
        "   VALUE `"FileVersion`", `"$version`"",
        '   VALUE "CompanyName", "Lil Gohan"',
        '   VALUE "OriginalFilename", "Tekken 2 Recompiled.exe"',
        '  END', ' END', ' BLOCK "VarFileInfo"', ' BEGIN', '  VALUE "Translation", 0x409, 1200', ' END', 'END')
    Set-Content (Join-Path $tmp 'launcher.rc') $rc -Encoding ascii
    & (Join-Path $T2Pack 'bin\llvm-windres.exe') -I $tmp (Join-Path $tmp 'launcher.rc') -O coff -o (Join-Path $tmp 'launcher.o')
    if ($LASTEXITCODE) { Die 'windres failed' }
    $objs += (Join-Path $tmp 'launcher.o')
    & $env:CC -O2 -municode -mwindows -static (Join-Path $PSScriptRoot 'windows_launcher.c') @objs -o (Join-Path $app 'Tekken 2 Recompiled.exe')
    if ($LASTEXITCODE) { Die 'launcher compile failed' }
} finally { Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue }

# Gates.
$bad = @()
foreach ($pe in Get-ChildItem $app -Recurse -Include *.exe, *.dll) {
    foreach ($dll in Get-Imports $pe.FullName) {
        $ok = $dll -like 'api-ms-win-*' -or $dll -like 'ext-ms-*' -or (Test-Path (Join-Path $system $dll)) -or
              (Test-Path (Join-Path $pe.DirectoryName $dll))
        if (-not $ok) { $bad += "$($pe.Name) -> $dll" }
    }
}
if ($bad) { Die ("imports outside System32 and the package: " + ($bad -join ', ')) }
$exeText = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes((Join-Path $rt $runtimeExe)))
foreach ($leak in @($project, $HOME, "$env:SystemDrive\Users\", "$env:SystemDrive/Users/")) {
    if ($exeText.IndexOf($leak, [StringComparison]::OrdinalIgnoreCase) -ge 0) { Write-Host "warning: runtime exe contains '$leak'" }
}
if ((Get-Item (Join-Path $rt 'bios\openbios.bin')).Length -ne 524288) { Die 'OpenBIOS image missing or wrong size' }
$stray = Get-ChildItem $app -Recurse -File | Where-Object {
    ($_.Extension -in '.cue', '.iso', '.chd', '.mcd', '.mcr' -or
     ($_.Extension -eq '.bin' -and $_.Name -ne 'openbios.bin') -or
     $_.Name -like 'SLUS_*' -or $_.Name -in 'settings.toml', 'disc.cfg', 'bios.cfg' -or
     $_.FullName -match '\\generated\\') }
if ($stray) { Die ("disc-derived or per-user files in the package: " + ($stray.FullName -join ', ')) }
if ((Get-ChildItem (Join-Path $rt 'saves') -ErrorAction SilentlyContinue)) { Die 'saves\ must not be packaged' }

if ($Shortcut) {
    $shell = New-Object -ComObject WScript.Shell
    $lnk = $shell.CreateShortcut($Shortcut)
    $lnk.TargetPath = Join-Path $app 'Tekken 2 Recompiled.exe'
    $lnk.WorkingDirectory = $app
    $lnk.IconLocation = (Join-Path $app 'Tekken 2 Recompiled.exe') + ',0'
    $lnk.Description = 'Tekken 2 Recompiled (psxrecomp)'
    $lnk.Save()
    Write-Host "shortcut: $Shortcut"
}
$size = (Get-ChildItem $app -Recurse -File | Measure-Object Length -Sum).Sum / 1MB
Write-Host ("packaged: {0} ({1:n1} MB)" -f $app, $size)

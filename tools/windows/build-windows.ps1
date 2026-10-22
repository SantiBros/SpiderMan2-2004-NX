# build-windows.ps1: builds the port from YOUR copy of Spider-Man 2 (Xbox) on Windows 10 or 11, through WSL (the Linux
# that Windows can install). build-windows.cmd starts it: double-click it, or drop your game onto it.
#   1. WSL with Ubuntu, installed if it is missing (once: Windows then asks to restart)
#   2. your game: a disc image (.iso .xiso .cci .cso, or the first part of a split one), or default.xbe
#   3. build.sh inside Linux: the first time it installs its tools, then it makes the .nro
#   4. the SD card folder, with your game already in it, in "SD card" beside build-windows.cmd
# The .nro holds code made from your copy of the game: it is for you only, never share or upload it.
param([string]$Game = '')

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$Version = (Get-Content -LiteralPath (Join-Path $Root 'VERSION') -Raw).Trim()
$Wsl = Join-Path $env:SystemRoot 'System32\wsl.exe'
if (-not (Test-Path -LiteralPath $Wsl)) {
    # a 32-bit PowerShell sees the 64-bit System32 here
    $other = Join-Path $env:SystemRoot 'Sysnative\wsl.exe'
    if (Test-Path -LiteralPath $other) { $Wsl = $other }
}
# WSL's output as UTF-8, read as UTF-8: folder names with accents or other letters stay right
$env:WSL_UTF8 = '1'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

function Say([string]$Text) {
    Write-Host ''
    Write-Host $Text -ForegroundColor Cyan
}

function Fail([string]$Text) {
    Write-Host ''
    Write-Host "Error: $Text" -ForegroundColor Red
    Write-Host 'The full guide: docs\INSTALL.md, "On Windows".'
    exit 1
}

# a word for bash, in single quotes
function Quote([string]$Text) {
    return "'" + $Text.Replace("'", "'\''") + "'"
}

# a Windows path as Linux sees it, /mnt/c/...
function ToLinux([string]$Path) {
    $p = & $Wsl -d $Distro -e wslpath -a $Path
    if ($LASTEXITCODE -ne 0 -or -not $p) { Fail "WSL cannot see $Path" }
    # the last line: WSL may print a notice before it
    return (($p | Select-Object -Last 1) -replace "`0", '').Trim()
}

Write-Host "Spider-Man 2 (Xbox) for Switch $Version - the Windows builder" -ForegroundColor White
Write-Host 'It makes the port from YOUR copy of the game. Keep the .nro for yourself: never share or upload it.'

# 1. WSL with a Linux
Say '[1/4] Checking WSL'
$distros = @()
if (Test-Path -LiteralPath $Wsl) {
    # Windows PowerShell stops at a native program's redirected error output when errors stop the script
    $ErrorActionPreference = 'Continue'
    $list = & $Wsl --list --quiet 2>$null
    $code = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($code -eq 0 -and $list) {
        $distros = @($list | ForEach-Object { ($_ -replace "`0", '').Trim() } |
            Where-Object { $_ -and $_ -notmatch '^docker-desktop' })
    }
}
if ($distros.Count -eq 0) {
    Write-Host 'WSL with Ubuntu is not installed yet. It is free, from Microsoft, and the builder runs inside it.'
    $answer = Read-Host 'Install it now? Windows asks for permission, then to restart the computer [Y/n]'
    if ($answer -and $answer -notmatch '^[Yy]') { Fail 'WSL is needed to build the port.' }
    try {
        Start-Process -FilePath $Wsl -ArgumentList '--install', '-d', 'Ubuntu' -Verb RunAs -Wait
    } catch {
        Fail 'WSL could not be installed (Windows did not allow it). Run "wsl --install" as administrator yourself.'
    }
    Write-Host ''
    Write-Host 'Next:' -ForegroundColor Yellow
    Write-Host '  1. Restart the computer.'
    Write-Host '  2. Open "Ubuntu" from the Start menu once, and choose a user name and a password (remember the password:'
    Write-Host '     the builder asks for it once, to install its tools).'
    Write-Host '  3. Double-click build-windows.cmd again.'
    exit 0
}
$Distro = $distros | Where-Object { $_ -match '^Ubuntu' } | Select-Object -First 1
if (-not $Distro) { $Distro = $distros[0] }
Write-Host "  Linux: $Distro"

# 2. your game
Say '[2/4] Your game'
if (-not $Game) {
    Write-Host '  Choose your Spider-Man 2 for the original Xbox in the window that opens.'
    Add-Type -AssemblyName System.Windows.Forms
    $owner = New-Object System.Windows.Forms.Form
    $owner.TopMost = $true
    $dialog = New-Object System.Windows.Forms.OpenFileDialog
    $dialog.Title = 'Your Spider-Man 2 (original Xbox): a disc image, or default.xbe from the disc''s files'
    $dialog.Filter = 'Spider-Man 2 (Xbox) (*.iso, *.xiso, *.cci, *.cso, default.xbe)|*.iso;*.xiso;*.cci;*.cso;*.xbe|All files (*.*)|*.*'
    $result = $dialog.ShowDialog($owner)
    $owner.Dispose()
    if ($result -ne [System.Windows.Forms.DialogResult]::OK) { Fail 'no game was chosen.' }
    $Game = $dialog.FileName
}
if (-not (Test-Path -LiteralPath $Game)) { Fail "$Game does not exist." }
$Game = (Resolve-Path -LiteralPath $Game).ProviderPath
Write-Host "  $Game"

# 3. the build, in Linux: the port's files are copied into Linux first (building on C: is much slower)
Say '[3/4] Building the .nro in Linux'
Write-Host '  The first time it installs the tools (about 10 minutes, it asks for your Linux password), then builds'
Write-Host '  (10 to 30 minutes). Later builds are quicker.'
$LinuxRoot = ToLinux $Root
$LinuxGame = ToLinux $Game
$Work = "~/SpiderMan2-2004-NX-$Version"
# without the SD card folder of an earlier build, nor a disc image left beside the builder
$script = "set -e; mkdir -p $Work; tar -C $(Quote $LinuxRoot) --exclude='./SD card' --exclude='*.iso' --exclude='*.xiso' " +
          "--exclude='*.cci' --exclude='*.cso' -cf - . | tar -C $Work -xf -; " +
          "cd $Work; bash build.sh --yes $(Quote $LinuxGame)"
& $Wsl -d $Distro -e bash -lc $script
if ($LASTEXITCODE -ne 0) { Fail 'the build stopped: the messages above say why.' }

# 4. the SD card folder, on Windows, with the game in it
Say '[4/4] The SD card folder'
$Out = Join-Path $Root 'SD card'
if (Test-Path -LiteralPath $Out) { Remove-Item -LiteralPath $Out -Recurse -Force }
New-Item -ItemType Directory -Path $Out | Out-Null
$LinuxOut = ToLinux $Out
& $Wsl -d $Distro -e bash -lc "cp -r $Work/out/sd/switch $(Quote $LinuxOut)/"
if ($LASTEXITCODE -ne 0) { Fail 'the SD card folder could not be copied out of Linux.' }
$GameDir = Join-Path $Out 'switch\SpiderMan2Xbox\game'
Write-Host '  Copying your game into it (a few minutes for a disc image)'

function CopyData([string]$Folder) {
    $data = Get-ChildItem -LiteralPath $Folder -Directory | Where-Object { $_.Name -ieq 'data' } | Select-Object -First 1
    if ($data) {
        Copy-Item -LiteralPath $data.FullName -Destination $GameDir -Recurse
    } else {
        Write-Host '  There is no data folder beside default.xbe: copy it into the game folder yourself.' -ForegroundColor Yellow
    }
}

$item = Get-Item -LiteralPath $Game
if ($item.PSIsContainer) {
    $xbe = Get-ChildItem -LiteralPath $Game -File | Where-Object { $_.Name -ieq 'default.xbe' } | Select-Object -First 1
    if ($xbe) {
        Copy-Item -LiteralPath $xbe.FullName -Destination (Join-Path $GameDir 'default.xbe')
        CopyData $Game
    } else {
        Get-ChildItem -LiteralPath $Game -File | Where-Object { $_.Extension -match '^\.(iso|xiso|cci|cso)$' } |
            ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $GameDir }
    }
} elseif ($item.Extension -ieq '.xbe') {
    Copy-Item -LiteralPath $item.FullName -Destination (Join-Path $GameDir 'default.xbe')
    CopyData $item.DirectoryName
} elseif ($item.Name -match '^(.*)\.1\.([A-Za-z]+)$') {
    # a split image: every part
    $pattern = '^' + [regex]::Escape($Matches[1]) + '\.\d+\.' + [regex]::Escape($Matches[2]) + '$'
    Get-ChildItem -LiteralPath $item.DirectoryName -File | Where-Object { $_.Name -match $pattern } |
        ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $GameDir }
} else {
    Copy-Item -LiteralPath $item.FullName -Destination $GameDir
}

Write-Host ''
Write-Host 'Done!' -ForegroundColor Green
Write-Host "  1. Copy the switch folder from `"$Out`" to the root of your SD card (merge it with the switch folder there)."
Write-Host '  2. On the Switch, hold R while you start any game, then choose "Spider-Man 2 (Xbox)" in the Homebrew Menu.'
Write-Host ''
Write-Host 'The .nro holds code made from your copy of the game: keep it for yourself, never share or upload it.'
Start-Process -FilePath 'explorer.exe' -ArgumentList "`"$Out`""

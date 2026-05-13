# Build pipeline:
#   1. PyInstaller --onefile -> dist\lanventory-agent.exe
#   2. Inno Setup compiles installer\installer.iss -> installer\Output\LanVentoryAgentSetup-*.exe
#
# Requires:
#   - Python 3.10+ on PATH
#   - Inno Setup 6 (iscc.exe) at the default path or via -Iscc

param(
    [string] $Python = "python",
    [string] $Iscc   = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
    [switch] $Clean,
    [switch] $SkipInstaller
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

function Invoke-Native {
    param(
        [Parameter(Mandatory)][string] $Exe,
        [Parameter(ValueFromRemainingArguments = $true)] $Args
    )
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $Exe @Args
    } finally {
        $ErrorActionPreference = $prev
    }
    if ($LASTEXITCODE -ne 0) {
        throw "$Exe exited with code $LASTEXITCODE"
    }
}

if ($Clean) {
    Remove-Item -Recurse -Force "$root\build", "$root\dist", "$root\installer\Output" `
        -ErrorAction SilentlyContinue
}

Push-Location $root
try {
    Invoke-Native $Python -m pip install --upgrade pip
    Invoke-Native $Python -m pip install -r requirements.txt pyinstaller

    Write-Host "Building lanventory-agent.exe..." -ForegroundColor Cyan
    Invoke-Native $Python -m PyInstaller `
        --noconfirm --onefile --console `
        --name lanventory-agent `
        -p . `
        agent\main.py

    if ($SkipInstaller) {
        Write-Host "Skipping Inno Setup (use without -SkipInstaller to ship)." -ForegroundColor Yellow
        return
    }

    if (-not (Test-Path $Iscc)) {
        throw @"
Inno Setup compiler not found at:
    $Iscc

Install Inno Setup 6 from https://jrsoftware.org/isinfo.php
(or pass -Iscc <path>). Use -SkipInstaller to stop after building the .exe.
"@
    }

    Write-Host "Compiling Inno Setup installer..." -ForegroundColor Cyan
    Invoke-Native $Iscc /Qp "$root\installer\installer.iss"

    Write-Host ""
    Write-Host "Build complete." -ForegroundColor Green
    Write-Host "  Agent:     $root\dist\lanventory-agent.exe"
    Get-ChildItem "$root\installer\Output\*.exe" -ErrorAction SilentlyContinue | ForEach-Object {
        Write-Host ("  Installer: {0}" -f $_.FullName) -ForegroundColor Green
    }
} finally {
    Pop-Location
}

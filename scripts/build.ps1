# Build pipeline for the LanVentory Windows agent.
#
# Order:
#   1. CMake + MSBuild on agent-cpp\ -> dist\lanventory-agent.exe
#   2. PyInstaller x 2 on gui\ -> dist\lanventory-configurator.exe + lanventory-manager.exe
#   3. Inno Setup on installer\installer.iss -> installer\Output\LanVentoryAgentSetup-*.exe
#
# Requires:
#   - Visual Studio 2022 Build Tools (or Community/Pro) with the C++
#     workload installed. cmake auto-detects via VS environment.
#   - CMake 3.20+ on PATH.
#   - Python 3.10+ on PATH.
#   - Inno Setup 6 (iscc.exe). Default install path is auto-detected.

param(
    [string] $Python = "python",
    [string] $Cmake  = "cmake",
    [string] $Iscc   = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
    [string] $Config = "Release",
    # Baked-in deployment values. Empty -> installer falls back to the
    # configurator (operator types these values post-install). Filled ->
    # installer auto-enrolls during setup, configurator is only kept on
    # Start Menu for later re-config.
    [string] $BackendUrl      = $env:LV_BACKEND_URL,
    [string] $EnrollmentToken = $env:LV_ENROLLMENT_TOKEN,
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
    Remove-Item -Recurse -Force "$root\build", "$root\dist", `
        "$root\agent-cpp\build", "$root\installer\Output" `
        -ErrorAction SilentlyContinue
}

if (-not (Test-Path "$root\dist")) {
    New-Item -ItemType Directory -Path "$root\dist" | Out-Null
}

Push-Location $root
try {
    # 1. Native C++ agent --------------------------------------------------
    Write-Host "Building C++ agent (CMake/MSBuild, $Config)..." -ForegroundColor Cyan
    $cppBuild = "$root\agent-cpp\build"
    Invoke-Native $Cmake -S "$root\agent-cpp" -B $cppBuild -A x64
    Invoke-Native $Cmake --build $cppBuild --config $Config --parallel
    Copy-Item -Force "$cppBuild\bin\$Config\lanventory-agent.exe" "$root\dist\lanventory-agent.exe"
    Write-Host "  -> $root\dist\lanventory-agent.exe" -ForegroundColor Green

    # 2. Python/Qt GUI tools -----------------------------------------------
    Invoke-Native $Python -m pip install --upgrade pip
    Invoke-Native $Python -m pip install -r requirements.txt pyinstaller

    Write-Host "Building lanventory-manager.exe..." -ForegroundColor Cyan
    Invoke-Native $Python -m PyInstaller `
        --noconfirm --onefile --windowed `
        --name lanventory-manager `
        --collect-all PySide6 `
        -p . `
        gui\manager.py

    Write-Host "Building lanventory-configurator.exe..." -ForegroundColor Cyan
    Invoke-Native $Python -m PyInstaller `
        --noconfirm --onefile --windowed --uac-admin `
        --name lanventory-configurator `
        --collect-all PySide6 `
        -p . `
        gui\configurator.py

    # Bake deployment-specific values into a config.json that the
    # installer drops verbatim into %ProgramData% during install. The
    # agent auto-upgrades the plaintext token to DPAPI on first read.
    $bakedDir = "$root\installer\baked"
    if (Test-Path $bakedDir) { Remove-Item -Recurse -Force $bakedDir }
    if ($BackendUrl -and $EnrollmentToken) {
        New-Item -ItemType Directory -Path $bakedDir | Out-Null
        $baked = [ordered]@{
            backend_url      = $BackendUrl.TrimEnd('/')
            enrollment_token = $EnrollmentToken
            interval_minutes = 60
            verify_tls       = $true
            ca_bundle        = $null
            log_path         = 'C:\ProgramData\LanVentory\agent\agent.log'
        }
        $baked | ConvertTo-Json -Depth 4 | Set-Content -Path "$bakedDir\config.json" -Encoding utf8
        Write-Host "Baked config -> $bakedDir\config.json (auto-enroll enabled)." -ForegroundColor Green
    } else {
        Write-Host "No LV_BACKEND_URL / LV_ENROLLMENT_TOKEN set -- installer will prompt via configurator." -ForegroundColor Yellow
    }

    if ($SkipInstaller) {
        Write-Host "Skipping Inno Setup (use without -SkipInstaller to ship)." -ForegroundColor Yellow
        return
    }

    # 3. Inno Setup --------------------------------------------------------
    if (-not (Test-Path $Iscc)) {
        throw @"
Inno Setup compiler not found at:
    $Iscc

Install Inno Setup 6 from https://jrsoftware.org/isinfo.php
(or pass -Iscc <path>). Use -SkipInstaller to stop after building exes.
"@
    }
    Write-Host "Compiling Inno Setup installer..." -ForegroundColor Cyan
    Invoke-Native $Iscc /Qp "$root\installer\installer.iss"

    Write-Host ""
    Write-Host "Build complete." -ForegroundColor Green
    Write-Host "  Binaries:    $root\dist\"
    Get-ChildItem "$root\installer\Output\*.exe" -ErrorAction SilentlyContinue | ForEach-Object {
        Write-Host ("  Installer:   {0}" -f $_.FullName) -ForegroundColor Green
    }
} finally {
    Pop-Location
}

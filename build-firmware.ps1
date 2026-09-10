# SuperGreenOS firmware build script (Windows/WSL wrapper)
# Builds firmware with all memory safety fixes

param(
    [switch]$SkipConfig,
    [switch]$GenOnly,
    [switch]$CleanBuild
)

$ErrorActionPreference = "Stop"

function Write-Status {
    Write-Host "==> $args" -ForegroundColor Cyan
}

function Write-Error-Exit {
    Write-Host "ERROR: $args" -ForegroundColor Red
    exit 1
}

Write-Host "SuperGreenOS Firmware Build" -ForegroundColor Green
Write-Host "Memory Safety Fixes:" -ForegroundColor Green
Write-Host "  ✅ auth.c: heap allocation instead of stack (2x 517B buffers)" -ForegroundColor Green
Write-Host "  ✅ mqtt.c: static buffer pool (6 functions, 1900+B → 200B per call)" -ForegroundColor Green
Write-Host "  ✅ httpd.c: malloc for /mqttdiag (1400B response)" -ForegroundColor Green
Write-Host "  ✅ cmd.c: snprintf instead of strcpy (buffer overflow fix)" -ForegroundColor Green
Write-Host ""

# Check if ESP-IDF is installed
$IdfPath = $env:IDF_PATH
if (-not $IdfPath) {
    $IdfPath = "$env:USERPROFILE\esp\esp-idf_release_3.3.1"
}

Write-Status "Checking environment..."

if (-not (Test-Path $IdfPath)) {
    Write-Host "ESP-IDF not found at: $IdfPath" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "To build firmware, you need:" -ForegroundColor Yellow
    Write-Host "1. ESP-IDF 3.3.1: https://github.com/espressif/esp-idf/releases/tag/v3.3.1"
    Write-Host "2. Set IDF_PATH environment variable or install to: $IdfPath"
    Write-Host "3. Ensure xtensa-esp32-elf toolchain is in PATH"
    Write-Host ""
    Write-Host "Installation steps (WSL):" -ForegroundColor Yellow
    Write-Host "  1. wsl apt-get install git wget flex bison gperf python3 python3-pip"
    Write-Host "  2. mkdir -p ~/esp && cd ~/esp"
    Write-Host "  3. git clone --branch v3.3.1 https://github.com/espressif/esp-idf.git esp-idf_release_3.3.1"
    Write-Host "  4. cd esp-idf_release_3.3.1 && ./install.sh"
    Write-Host ""
    Write-Host "Then retry: .\build-firmware.ps1"
    exit 1
}

Write-Status "ESP-IDF found at: $IdfPath"
Write-Status "Starting build process..."
Write-Host ""

# Set build arguments
$buildArgs = @()
if ($SkipConfig) {
    $buildArgs += "--skip-config"
}
if ($GenOnly) {
    $buildArgs += "--gen-only"
}

# Export IDF_PATH for bash script
$env:IDF_PATH = $IdfPath

# Call build.sh via WSL if available, otherwise direct make
Write-Status "Building firmware..."

try {
    # Try WSL first for consistent environment
    $wslCheck = wsl echo "WSL available" 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-Status "Using WSL for build..."
        wsl bash c:/Sources/SuperGreenOS/scripts/build.sh $buildArgs
    } else {
        Write-Error-Exit "WSL not available. Install WSL2: https://docs.microsoft.com/en-us/windows/wsl/install"
    }
} catch {
    Write-Error-Exit "Build failed: $_"
}

if ($LASTEXITCODE -ne 0) {
    Write-Error-Exit "Build script exited with code $LASTEXITCODE"
}

Write-Host ""
Write-Status "Build complete! ✅"
Write-Host ""
Write-Host "Firmware binary: build/firmware.bin" -ForegroundColor Green
Write-Host "Bootloader: build/bootloader/bootloader.bin" -ForegroundColor Green
Write-Host "Partition table: build/partitions.bin" -ForegroundColor Green
Write-Host ""
Write-Host "Flash to controller (192.168.1.104):" -ForegroundColor Yellow
Write-Host "  esptool.py -p /dev/ttyUSB0 -b 460800 write_flash 0x1000 build/bootloader/bootloader.bin 0x8000 build/partitions.bin 0x10000 build/firmware.bin"
Write-Host ""

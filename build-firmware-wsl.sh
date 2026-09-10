#!/usr/bin/env bash
# Build wrapper for WSL/Linux - handles ESP-IDF environment setup and compilation

set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd -P)"
cd "$ROOT"

echo "==> SuperGreenOS Firmware Build (WSL/Linux)"
echo "Memory Safety Fixes:"
echo "  ✅ auth.c: heap allocation instead of stack (2x 517B buffers)"
echo "  ✅ mqtt.c: static buffer pool (6 functions, 1900+B → 200B per call)"
echo "  ✅ httpd.c: malloc for /mqttdiag (1400B response)"
echo "  ✅ cmd.c: snprintf instead of strcpy (buffer overflow fix)"
echo ""

# Check ESP-IDF
IDF_PATH="${IDF_PATH:-$HOME/esp/esp-idf_release_3.3.1}"

if [ ! -d "$IDF_PATH" ]; then
    echo "ERROR: ESP-IDF not found at $IDF_PATH" >&2
    echo ""
    echo "Installation steps:" >&2
    echo "  1. sudo apt-get install git wget flex bison gperf python3 python3-pip" >&2
    echo "  2. mkdir -p ~/esp && cd ~/esp" >&2
    echo "  3. git clone --branch v3.3.1 https://github.com/espressif/esp-idf.git esp-idf_release_3.3.1" >&2
    echo "  4. cd esp-idf_release_3.3.1 && ./install.sh" >&2
    echo ""
    echo "Then set: export IDF_PATH=\$HOME/esp/esp-idf_release_3.3.1" >&2
    exit 1
fi

export IDF_PATH
echo "==> ESP-IDF found at: $IDF_PATH"

# Parse arguments
BUILD_ARGS=()
for arg in "$@"; do
    case "$arg" in
        --skip-config) BUILD_ARGS+=("--skip-config") ;;
        --gen-only) BUILD_ARGS+=("--gen-only") ;;
        -h|--help) echo "Usage: $0 [--skip-config] [--gen-only]"; exit 0 ;;
        *) echo "Unknown option: $arg" >&2; exit 1 ;;
    esac
done

echo "==> Starting build process..."
echo ""

# Call main build script
if bash scripts/build.sh "${BUILD_ARGS[@]}"; then
    echo ""
    echo "==> Build complete! ✅"
    echo ""
    echo "Firmware binary: build/firmware.bin"
    echo "Bootloader: build/bootloader/bootloader.bin"
    echo "Partition table: build/partitions.bin"
    echo ""
    echo "Flash to controller (192.168.1.104):"
    echo "  esptool.py -p /dev/ttyUSB0 -b 460800 write_flash 0x1000 build/bootloader/bootloader.bin 0x8000 build/partitions.bin 0x10000 build/firmware.bin"
    echo ""
else
    echo "Build failed!" >&2
    exit 1
fi

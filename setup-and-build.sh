#!/usr/bin/env bash
# Automatic ESP-IDF setup and firmware build for SuperGreenOS

set -euo pipefail

echo "==> SuperGreenOS Firmware Build Setup"
echo ""

# Check if running in WSL
if ! grep -qi microsoft /proc/version 2>/dev/null; then
    echo "ERROR: This script must run in WSL" >&2
    echo "Run: wsl bash setup-and-build.sh" >&2
    exit 1
fi

IDF_PATH="${IDF_PATH:-$HOME/esp/esp-idf_release_3.3.1}"

# Step 1: Install build tools if needed
echo "==> Step 1: Installing build tools..."
if ! command -v flex &> /dev/null; then
    echo "Installing dependencies (this may take 5-10 minutes)..."
    sudo apt-get update -qq
    sudo apt-get install -y -qq \
        git wget curl flex bison gperf \
        python3 python3-pip python3-setuptools \
        build-essential cmake \
        libffi-dev libssl-dev 2>&1 | grep -v "^Reading\|^Building\|^Setting up" || true
    echo "✅ Build tools installed"
else
    echo "✅ Build tools already installed"
fi

# Step 2: Install/verify ESP-IDF
echo ""
echo "==> Step 2: Installing ESP-IDF 3.3.1..."

if [ ! -d "$IDF_PATH" ]; then
    echo "Cloning ESP-IDF (this may take 2-3 minutes)..."
    mkdir -p "$(dirname "$IDF_PATH")"
    git clone --branch v3.3.1 --depth 1 \
        https://github.com/espressif/esp-idf.git \
        "$IDF_PATH" 2>&1 | grep -v "^Cloning\|^Receiving\|^Resolving" || true

    echo "Running ESP-IDF install.sh (this may take 5-10 minutes)..."
    cd "$IDF_PATH"
    ./install.sh >/dev/null 2>&1 &
    PID=$!

    # Show progress
    while kill -0 $PID 2>/dev/null; do
        echo -n "."
        sleep 2
    done
    echo ""

    cd - >/dev/null
    echo "✅ ESP-IDF installed"
else
    echo "✅ ESP-IDF already installed at $IDF_PATH"
fi

# Step 3: Source ESP-IDF environment
echo ""
echo "==> Step 3: Sourcing ESP-IDF environment..."
export IDF_PATH
source "$IDF_PATH/export.sh" >/dev/null 2>&1
echo "✅ Environment ready"

# Step 4: Build firmware
echo ""
echo "==> Step 4: Building firmware (this may take 3-5 minutes)..."
echo "Memory safety fixes being compiled:"
echo "  ✅ auth.c: heap allocation instead of stack (2x 517B)"
echo "  ✅ mqtt.c: static buffer pool (1900+B → 200B per call)"
echo "  ✅ httpd.c: malloc for /mqttdiag (1400B)"
echo "  ✅ cmd.c: snprintf instead of strcpy"
echo ""

cd /mnt/c/Sources/SuperGreenOS

if bash scripts/build.sh --skip-config; then
    echo ""
    echo "==> ✅ BUILD SUCCESSFUL!"
    echo ""
    echo "Firmware ready at:"
    echo "  build/firmware.bin (main firmware)"
    echo "  build/bootloader/bootloader.bin (bootloader)"
    echo "  build/partitions.bin (partition table)"
    echo ""
    echo "Next: Flash to controller 192.168.1.104"
    echo ""
else
    echo "ERROR: Build failed" >&2
    exit 1
fi

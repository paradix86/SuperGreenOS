#!/usr/bin/env bash
# Flash SuperGreenOS firmware to ESP32 controller via esptool.py

set -euo pipefail

CONTROLLER_IP="${1:-192.168.1.104}"
SERIAL_PORT="${2:-/dev/ttyUSB0}"
BAUD_RATE="${3:-460800}"

echo "==> SuperGreenOS Firmware Deploy"
echo ""
echo "Target: $CONTROLLER_IP"
echo "Port: $SERIAL_PORT"
echo "Baud: $BAUD_RATE"
echo ""

# Check build artifacts
if [ ! -f "build/firmware.bin" ]; then
    echo "ERROR: build/firmware.bin not found" >&2
    echo "Run: bash build-firmware-wsl.sh --skip-config" >&2
    exit 1
fi

if [ ! -f "build/bootloader/bootloader.bin" ]; then
    echo "ERROR: build/bootloader/bootloader.bin not found" >&2
    exit 1
fi

if [ ! -f "build/partitions.bin" ]; then
    echo "ERROR: build/partitions.bin not found" >&2
    exit 1
fi

echo "✅ Build artifacts found"
echo ""

# Check for serial port
if [ ! -e "$SERIAL_PORT" ]; then
    echo "WARNING: $SERIAL_PORT not found" >&2
    echo "Available ports:"
    ls -la /dev/ttyUSB* /dev/ttyACM* 2>/dev/null || echo "  (none found)"
    echo ""
    echo "Continuing - connection will be attempted..."
    echo ""
fi

# Check if esptool is installed
if ! command -v esptool.py &> /dev/null; then
    echo "Installing esptool.py..."
    pip3 install --quiet esptool
    echo "✅ esptool.py installed"
fi

echo ""
echo "==> Flashing firmware to $CONTROLLER_IP..."
echo ""
echo "Flash layout:"
echo "  0x1000  - bootloader/bootloader.bin"
echo "  0x8000  - partitions.bin"
echo "  0x10000 - firmware.bin"
echo ""

if esptool.py \
    -p "$SERIAL_PORT" \
    -b "$BAUD_RATE" \
    write_flash \
    0x1000 build/bootloader/bootloader.bin \
    0x8000 build/partitions.bin \
    0x10000 build/firmware.bin; then

    echo ""
    echo "==> ✅ FLASH SUCCESSFUL!"
    echo ""
    echo "Waiting for controller to boot (10 seconds)..."
    sleep 10

    echo ""
    echo "==> Verifying firmware..."
    echo ""

    # Try to read /mqttdiag to verify controller is responding
    if command -v curl &> /dev/null; then
        if curl -s --connect-timeout 5 "http://$CONTROLLER_IP/mqttdiag" > /dev/null 2>&1; then
            echo "✅ Controller responding at http://$CONTROLLER_IP/mqttdiag"
            echo ""
            echo "Heap status:"
            curl -s "http://$CONTROLLER_IP/mqttdiag" | jq '{heap_free, heap_min_free, mqtt_connected}' 2>/dev/null || true
            echo ""
            echo "🚀 Firmware deployed successfully!"
            echo ""
            echo "Monitor heap stability:"
            echo "  watch -n 5 'curl -s http://$CONTROLLER_IP/mqttdiag | jq .heap_min_free'"
            echo ""
        else
            echo "⚠️ Controller not responding at http://$CONTROLLER_IP"
            echo "This may be normal if the network needs a moment to recover."
            echo "Try: curl http://$CONTROLLER_IP/mqttdiag after 30 seconds"
            echo ""
        fi
    else
        echo "⚠️ curl not installed - cannot verify"
        echo "Check controller manually at: http://$CONTROLLER_IP/mqttdiag"
        echo ""
    fi

else
    echo "ERROR: Flash failed!" >&2
    exit 1
fi

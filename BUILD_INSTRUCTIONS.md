# SuperGreenOS Firmware Build - Memory Safety Fixes

This branch contains critical ESP32 memory safety fixes. See [`BUFFER_AUDIT.md`](BUFFER_AUDIT.md) for details.

## Quick Start

### Windows (via PowerShell)
```powershell
cd c:\Sources\SuperGreenOS
.\build-firmware.ps1
```

### WSL / Linux
```bash
cd ~/esp/SuperGreenOS
bash build-firmware-wsl.sh
```

## Prerequisites

### Option A: WSL2 (Recommended for Windows users)

1. **Install WSL2** (Windows Subsystem for Linux):
   ```powershell
   wsl --install Ubuntu-20.04
   ```

2. **Inside WSL, install build tools:**
   ```bash
   sudo apt-get update
   sudo apt-get install -y \
     git wget curl flex bison gperf \
     python3 python3-pip python3-setuptools \
     build-essential cmake
   ```

3. **Install ESP-IDF 3.3.1:**
   ```bash
   mkdir -p ~/esp && cd ~/esp
   git clone --branch v3.3.1 https://github.com/espressif/esp-idf.git esp-idf_release_3.3.1
   cd esp-idf_release_3.3.1
   ./install.sh
   ```

4. **Source the IDF environment (add to ~/.bashrc):**
   ```bash
   export IDF_PATH=$HOME/esp/esp-idf_release_3.3.1
   source $IDF_PATH/export.sh
   ```

### Option B: Native Linux

Follow steps 2-4 above (skip WSL install).

### Option C: Docker

```bash
docker run -v $(pwd):/workspace -w /workspace \
  espressif/idf:v3.3.1 \
  /bin/bash -c "idf.py build"
```

## Build Variants

### Full Build (default - regenerates config)
```bash
# Windows
.\build-firmware.ps1

# WSL/Linux
bash build-firmware-wsl.sh
```

Requires: `cue` (0.0.8), `ejs-cli` (npm)

### Quick Build (skip config generation)
```bash
# Windows
.\build-firmware.ps1 -SkipConfig

# WSL/Linux
bash build-firmware-wsl.sh --skip-config
```

**Use this if config hasn't changed** (much faster).

### Config Generation Only
```bash
# Windows
.\build-firmware.ps1 -GenOnly

# WSL/Linux
bash build-firmware-wsl.sh --gen-only
```

Useful for validating config without compiling.

## Output Files

After successful build:

| File | Purpose |
|------|---------|
| `build/firmware.bin` | Main firmware image |
| `build/bootloader/bootloader.bin` | ESP32 bootloader |
| `build/partitions.bin` | Partition table |

## Flashing to Controller

### Via esptool.py (Recommended)

```bash
# Install esptool
pip3 install esptool

# Flash to 192.168.1.104 controller
esptool.py -p /dev/ttyUSB0 -b 460800 write_flash \
  0x1000 build/bootloader/bootloader.bin \
  0x8000 build/partitions.bin \
  0x10000 build/firmware.bin
```

### Via UART Cable
- Bootloader: `0x1000`
- Partition table: `0x8000`
- Firmware: `0x10000`

## Verifying Memory Safety Fixes

### Before Flash
1. Check firmware size hasn't increased unexpectedly:
   ```bash
   ls -lh build/firmware.bin
   ```

2. Inspect commits (compare with main/master):
   ```bash
   git log --oneline origin/main..HEAD
   ```

### After Flash
1. Monitor `/mqttdiag` endpoint for heap stability:
   ```bash
   # Every 5 seconds, check heap_min_free
   watch -n 5 'curl -s http://192.168.1.104/mqttdiag | jq ".heap_min_free"'
   ```

2. Verify under polling stress (15s polling = normal app usage):
   - `heap_min_free` should stay >8KB
   - No heap_low_events during 24h operation
   - Check `BUFFER_AUDIT.md` for expected vs observed metrics

## Troubleshooting

### Build fails with "IDF_PATH not found"
Set environment variable before building:
```bash
export IDF_PATH=$HOME/esp/esp-idf_release_3.3.1  # Linux/WSL
set IDF_PATH=C:\Users\%USERNAME%\esp\esp-idf_release_3.3.1  # Windows cmd
```

### WSL not available (Windows)
Install WSL2: https://docs.microsoft.com/en-us/windows/wsl/install

### Missing xtensa toolchain
```bash
# Inside ESP-IDF
./install.sh
source export.sh
```

### Flash fails with "No port found"
- Check USB cable (data cable, not power-only)
- Try: `ls -la /dev/ttyUSB*` (Linux/WSL)
- Try: `Get-PnpDevice -Class Ports` (PowerShell)
- Use correct baud rate: `-b 460800`

## Memory Safety Improvements

| Component | Before | After | Fix |
|-----------|--------|-------|-----|
| auth_request() | 2x 517B stack | malloc/free | Eliminates root cause of 3160B heap dip |
| MQTT functions (6) | 1900+B stack | ~200B + pool | Reduces polling-induced fragmentation |
| /mqttdiag response | 1400B stack | malloc/free | No more stack thrashing on rapid reads |
| cmd.c string concat | strcpy() | snprintf() | Prevents buffer overflow |

**Impact:** Controllers will no longer crash silently from heap exhaustion during rapid polling or MQTT discovery.

## Questions?

- Check `BUFFER_AUDIT.md` for technical details
- Review commits: `8cdfb49`, `c149322`, `35c3f0f`, `39bca73`, `8dd8c61`
- Read source files: `main/core/httpd/auth.c`, `main/core/mqtt/mqtt.c`, etc.

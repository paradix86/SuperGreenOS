# SuperGreenOS Firmware Deployment Guide

## Prerequisites

1. **Firmware built:** `build/firmware.bin`, `build/bootloader/bootloader.bin`, `build/partitions.bin`
2. **USB/Serial cable:** Connected to ESP32 controller (TX/RX/GND pins)
3. **esptool.py installed:**
   ```bash
   pip3 install esptool
   ```
4. **Network access:** Controller at 192.168.1.104 (for verification)

## Flash Method 1: Via deploy script (Recommended)

```bash
# WSL/Linux
bash deploy.sh

# Or with custom serial port
bash deploy.sh 192.168.1.104 /dev/ttyUSB0 460800
```

## Flash Method 2: Manual esptool.py

```bash
esptool.py -p /dev/ttyUSB0 -b 460800 write_flash \
  0x1000 build/bootloader/bootloader.bin \
  0x8000 build/partitions.bin \
  0x10000 build/firmware.bin
```

## Flash Method 3: Via Arduino IDE

1. Install ESP32 core in Arduino IDE
2. Board: ESP32 Dev Module
3. Settings:
   - Flash Size: 4MB
   - Flash Freq: 40MHz
   - Upload Speed: 460800

## Verification After Flash

### Quick Check (3 seconds)
```bash
curl http://192.168.1.104/mqttdiag | jq '.heap_min_free'
```

Expected: `> 8000` (bytes)

### Full Health Check (5 seconds)
```bash
curl http://192.168.1.104/mqttdiag | jq '{
  heap_free,
  heap_min_free,
  mqtt_connected,
  uptime_s,
  state,
  n_restarts
}'
```

### Live Monitoring (continuous)
```bash
watch -n 5 'curl -s http://192.168.1.104/mqttdiag | jq .heap_min_free'
```

Monitor for 5-10 minutes. Expected: stays >8000 even with rapid polling

## Rollback (if needed)

```bash
# Flash previous firmware from git history
git show HEAD~2:build/firmware.bin > firmware-old.bin
esptool.py -p /dev/ttyUSB0 -b 460800 write_flash \
  0x10000 firmware-old.bin
```

## Troubleshooting

### "Serial port not found"
- Check USB cable is data-capable (not power-only)
- Check device appears: `ls -la /dev/ttyUSB*`
- Try different USB port
- Verify ESP32 can boot to bootloader mode (hold BOOT button while resetting)

### Flash succeeds but controller doesn't respond
- Wait 30 seconds (might be reconnecting to WiFi)
- Check WiFi network: SSID should appear as "WiFi_Antenna"
- Try: `ping 192.168.1.104`
- Check controller console logs if available

### "write_flash: ESP32 ROM code failed to execute command"
- Controller already running - needs power cycle
- Or try with different baud rate: `-b 230400` instead of 460800
- Or try with reset: add `--before=default_reset --after=hard_reset`

### Controller boots but heap_min_free is still low
- This is the OLD firmware with the bug
- Verify flash actually succeeded
- Check: `curl http://192.168.1.104/s | jq .heap_min_free` after 2min MQTT discovery
- Expected NEW behavior: stays >10000 after MQTT runs

## Memory Safety Verification

After 24-48 hours of normal operation:

### No Silent Crashes
```bash
curl http://192.168.1.104/mqttdiag | jq '.n_restarts'
```
Should be same as before flash (no unexpected reboots)

### Stable Heap
```bash
# Run for 10 minutes
for i in {1..120}; do
  curl -s http://192.168.1.104/mqttdiag | jq '.heap_min_free'
  sleep 5
done
```

Expected: minimum never dips below 8000 bytes

### No Heap Events
```bash
curl http://192.168.1.104/mqttdiag | jq '.heap_low_events'
```

Expected: 0 (or same as before if controller ran for days)

## Commit Info

Memory safety fixes deployed:
- `8cdfb49` - auth.c: heap allocation fix
- `c149322` + `35c3f0f` - mqtt.c: buffer pool
- `39bca73` - httpd.c: /mqttdiag malloc
- `8dd8c61` - cmd.c: strcpy→snprintf
- `03332d5` - build scripts

See `BUFFER_AUDIT.md` for technical details.

## Next Steps

1. Monitor `/mqttdiag` heap_min_free for 24 hours
2. If stable (>8KB), deployment successful ✅
3. If issues, rollback with previous firmware
4. Report findings to development team

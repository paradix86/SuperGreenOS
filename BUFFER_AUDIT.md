# ESP32 Stack Buffer Audit - Memory Safety Issues

**Date:** 2026-09-10  
**Status:** ALL CRITICAL ISSUES FIXED - flashed as OTA 1789024847 on 2026-09-10 (firmware 5dfe013): heap_min_free 23068 B after 60 s rapid polling (was 3160 B), heap_low_events 0; 24 h check pending
**Commits:** 8cdfb49 (auth), c149322+35c3f0f+5dfe013 (mqtt, incl. mqtt.c.template), 39bca73+5dfe013 (httpd), 8dd8c61 (cmd)

## Summary

ESP32 firmware has multiple large stack buffer allocations that cause heap fragmentation under rapid polling. Without serial debugging access, these crashes are invisible and irrecoverable.

## Critical Issues

### 1. auth_request() - FIXED (Commit 8cdfb49)
- **Files:** main/core/httpd/auth.c
- **Problem:** Allocated 2x 517-byte buffers on stack per HTTP request
- **Impact:** Rapid `/s` polling → stack exhaustion → heap dips to 3160B
- **Fix:** Changed to malloc/free dynamic allocation
- **Status:** ✅ IMPLEMENTED

### 2. mqtt.c - Large payloads - FIXED (Commits c149322, 35c3f0f)
- **Lines:** 527, 438, 321, 279, 239, 366, 195
- **Functions converted to buffer pool:**
  - mqtt_publish_ha_state_diag: payload[1400] → pool (CRITICAL)
  - mqtt_publish_ha_state: payload[1024] → pool
  - mqtt_publish_ha_number_config: payload[896] → pool
  - mqtt_publish_ha_switch_config: payload[800] → pool
  - mqtt_publish_ha_binary_config: payload[720] → pool
  - mqtt_publish_ha_config: payload[640] → pool
- **Pattern:** topic[160] + state/avail/command_topic + payload simultaneously
- **Impact:** Before: 1900+B per MQTT publish → After: ~200B (via buffer pool)
- **Fix:** Static mqtt_buffer_pool_t with FreeRTOS mutex protection (acquire/release)
- **Status:** ✅ IMPLEMENTED

### 3. httpd.c:345 - /mqttdiag response - FIXED (Commit 39bca73)
- **Size:** 1400 bytes
- **Impact:** Every /mqttdiag read (every 5-15s) allocates max response on stack
- **Fix:** malloc/free for response buffer
- **Status:** ✅ IMPLEMENTED

## Root Cause

No global buffer pool. Each handler independently allocates temporary buffers on stack, expecting them to be freed when function returns. Under concurrent requests, stack exhaustion forces heap compaction.

## Solutions Implemented

### Phase 1: Critical Endpoints (COMPLETE)
1. ✅ **auth_request()** - malloc/free for 2x 517B buffers
   - Commit: 8cdfb49
   - Impact: Fixes root cause of 3160B heap dip

2. ✅ **mqtt.c (6 functions)** - Static buffer pool with mutex
   - Commits: c149322, 35c3f0f
   - Pool size: ~6.5KB static allocation
   - Per-function stack reduction: 1900+B → 200B
   - Acquire/release pattern with 100ms timeout (non-blocking)

3. ✅ **httpd.c (/mqttdiag)** - malloc/free for 1400B response
   - Commit: 39bca73
   - Impact: Eliminates stack thrashing on rapid polling

### Phase 2: Production Testing (RECOMMENDED)
- Monitor `heap_min_free` during rapid `/s` polling → target: >8KB stable
- Monitor `/mqttdiag` responses for timeouts during concurrent requests
- Check MQTT discovery cycles for payload truncations
- Profile actual MQTT payload sizes (may be <640B in practice)

## Testing

- Monitor heap_min_free during rapid polling (target: >8KB stable)
- Check heap_min_ctx for any new URIs causing dips
- Verify MQTT payloads don't exceed buffer sizes

## Files Affected

- `main/core/httpd/auth.c` - FIXED
- `main/core/httpd/httpd.c` - Line 345 (1400B ret buffer)
- `main/core/mqtt/mqtt.c` - Lines 527, 321, 279, 239, 438

## Impact on Production

Without these fixes, controllers will experience silent heap exhaustion crashes when:
- Running with high polling frequency
- Under network stress (MQTT retransmits)
- With multiple concurrent HTTP clients
- Without serial port for debugging

## 2026-09-11 - 24 h verdict on OTA 1789024847 and follow-up OTA 1789110379

Log: `C:\tmp\heap_24h.log` (every 5 min, 09:30-16:55, then 23.5 h uptime read live).
No reboot, `/s` gone from the dip context, heap ~34 KB. One dip: 3252 B at
uptime 14305 s, last HTTP request (`/dash`, the app's 15 s poll) 4 s old,
largest free block 27.7 KB of 35.6 KB at capture -> many small allocations
outside HTTP, i.e. a Wi-Fi RX buffer burst (32 x 1.6 KB allowed) on a 34 KB
baseline. Not TLS (broker mqtt://, OTA idle).

Fix (commit 4f3c805, OTA 1789110379 flashed 09:21): WIFI_DYNAMIC_RX_BUFFER_NUM
32 -> 16, MQTT task stack 16384 -> 12288 (HWM 9764 B unused). After boot:
heap_free 37416, mqtt_stack_hwm 5668.

Next (proposed, not done): one mutex for the KV RAM cache instead of 291
(~25 KB of heap) in kv_helpers.c.template.

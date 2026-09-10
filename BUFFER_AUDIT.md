# ESP32 Stack Buffer Audit - Memory Safety Issues

**Date:** 2026-09-10  
**Status:** Critical issues identified, fix for auth_request implemented, broader refactor pending

## Summary

ESP32 firmware has multiple large stack buffer allocations that cause heap fragmentation under rapid polling. Without serial debugging access, these crashes are invisible and irrecoverable.

## Critical Issues

### 1. auth_request() - FIXED (Commit 8cdfb49)
- **Files:** main/core/httpd/auth.c
- **Problem:** Allocated 2x 517-byte buffers on stack per HTTP request
- **Impact:** Rapid `/s` polling → stack exhaustion → heap dips to 3160B
- **Fix:** Changed to malloc/free dynamic allocation
- **Status:** ✅ IMPLEMENTED - awaiting compile & test

### 2. mqtt.c - Multiple large payloads (PENDING)
- **Lines:** 527, 321, 279, 239, 438
- **Sizes:** 1400B (critical), 896B, 800B, 720B, 1024B
- **Pattern:** topic[160] + payload[640-1400] allocated simultaneously per function
- **Impact:** Typical MQTT publish allocates 800-1000B on stack
- **Fix:** Create buffer pool or use malloc() for >512B allocations

### 3. httpd.c:345 - /mqttdiag response (PENDING)
- **Size:** 1400 bytes
- **Impact:** Every /mqttdiag read allocates max response size on stack
- **Fix:** Use chunked response or buffer pool

## Root Cause

No global buffer pool. Each handler independently allocates temporary buffers on stack, expecting them to be freed when function returns. Under concurrent requests, stack exhaustion forces heap compaction.

## Recommended Solutions (Priority Order)

1. **Immediate:** Fix mqtt.c largest allocations (1400B, 1024B)
   - Use `malloc()` for buffers >512B
   - Free immediately after use
   - Profile actual max sizes in production

2. **Short-term:** Create MQTT buffer pool
   - Static `mqtt_buffer_t { topic[160], payload[1400] }` singleton
   - Protect with mutex for thread safety
   - Reuse across all mqtt_send_*() functions

3. **Medium-term:** Audit and reduce all stack allocations
   - Profile payload sizes (likely <640B in practice)
   - Reduce defaults from 1400 to 640 or less
   - Document ESP32 stack size constraints

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

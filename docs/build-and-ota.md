# Build And OTA

This document captures the workflow that was validated end-to-end in March 2026.

## Build firmware

```bash
cd /home/alan/sources/SuperGreenOS
./update_config.sh config_gen/config/SuperGreenOS/Controllers/Controller/v2.1 config.controller.json
bash ./update_templates.sh config.controller.json
bash ./update_htmlapp.sh config.controller.json

export IDF_PATH=$HOME/esp/esp-idf_release_3.3.1
. $IDF_PATH/export.sh

bash ./scripts/build_maintenance_ota.sh
```

Expected output:

- `releases/SuperGreenMaintenance/last_timestamp`
- `releases/SuperGreenMaintenance/<timestamp>/firmware.bin`

Verified timestamps:

- `1773751114`
- `1773757485`

## Serve OTA files

```bash
cd /home/alan/sources/SuperGreenOS/releases
python3 -m http.server 8091
```

## Example verified OTA setup

- Controller IP: `192.168.1.104`
- Build host IP: `192.168.1.151`
- OTA server port: `8091`
- Base dir: `/SuperGreenMaintenance`

Controller settings:

- `server_ip = 192.168.1.151`
- `server_hostname = 192.168.1.151`
- `server_port = 8091`
- `basedir = /SuperGreenMaintenance`

Trigger OTA by setting:

- `start = 1`

## What success looks like

The HTTP server should receive:

- `/SuperGreenMaintenance/last_timestamp`
- `/SuperGreenMaintenance/<timestamp>/firmware.bin`

After boot, the controller should:

- come back online
- show the new `ota.timestamp`
- reset `start` to `0`

The `1773757485` deployment was also used to verify the first live `sensor_health` backend.

## Important maintenance behavior

The maintenance OTA build forces a one-time SPIFFS format.

That means:

- firmware update can succeed
- web UI can disappear temporarily

This is expected for the maintenance workflow.

## Restore the UI after maintenance OTA

```bash
cd /home/alan/sources/SuperGreenOS
bash ./update_htmlapp.sh config.controller.json
bash ./upload_htmlapp.sh 192.168.1.104 ./spiffs_fs
```

Expected response:

- `@FS File uploaded successfully`

On the legacy SPIFFS controller, restore is most reliable in this order:

1. delete old `/fs/app.html`
2. delete old `/fs/config.json`
3. upload new `app.html`
4. upload new `config.json`

`upload_htmlapp.sh` now follows that sequence automatically because `config.json` first can leave `app.html` truncated even when upload returns `200`.

Important path detail:

- the UI path is `http://<controller-ip>/fs/app.html`
- the legacy firmware does not serve `/fs/app`

## Recovery from the fallback AP

If the controller comes back on its recovery access point instead of rejoining the LAN:

- SSID: `🤖🍁`
- password: `multipass`
- controller IP: `192.168.4.1`

This recovery does not require internet access on the laptop. It only requires joining the controller AP so the host can reach `192.168.4.1`.

Typical recovery flow:

```bash
cd /home/alan/sources/SuperGreenOS
bash ./update_htmlapp.sh config.controller.json
bash ./upload_htmlapp.sh 192.168.4.1 ./spiffs_fs
```

Then open:

- `http://192.168.4.1/fs/app.html`

If Wi-Fi settings were lost, re-enter them in the admin UI under:

- `System -> wifi`

Important nuance:

- changing `wifi_ssid` clears the stored `wifi_password`
- set `wifi_ssid` first, save it, then set `wifi_password`

## Wi-Fi file replacement after OTA

The legacy upload handler originally failed replacing existing SPIFFS files after OTA with:

- `500 Failed to create file`

The fix was applied in:

- `main/core/httpd/httpd_fs.c`

Behavioral fix:

- remove the old file with `unlink(filepath);` before opening the replacement file for write

## SPIFFS size budget

The `storage` partition is only `0x8000` (`32 KB`), so compressed UI size matters.

Practical consequences:

- a slightly larger `app.html.gz` can fail to upload or fit
- `upload_htmlapp.sh` now prefers `zopfli` when available
- fallback stays `gzip -9 -n`
- UI deduplication work can be the difference between success and failure

## Browser cache after UI upload

Another practical trap was browser caching.

Even after a successful UI upload, `/fs/app.html` could still appear unchanged until a hard refresh or cache-busting query string was used.

Mitigation now present in firmware:

- `main/core/httpd/httpd_fs.c` sends `Cache-Control: no-store, no-cache, must-revalidate, max-age=0`
- plus `Pragma: no-cache`
- plus `Expires: 0`

Legacy route nuance:

- `/fs/app.html?<anything>` is not supported and returns `404`
- use plain `/fs/app.html`
- if needed, prefer a hard refresh or incognito window

## Postmortem: reboot loop followed by config reset

One March 2026 maintenance OTA incident on a live controller showed the following behavior:

- firmware OTA succeeded
- the controller later reappeared on the default AP `🤖🍁`
- `/fs/app.html` was missing until re-uploaded
- saved Wi-Fi credentials had to be entered again

Confirmed facts from the firmware:

- maintenance OTA forces a one-time SPIFFS format in `main/core/httpd/httpd_fs.c`
- the reboot guard increments `N_SHORT_REBOOTS` at boot and only clears it after 10 seconds in `main/core/reboot/reboot.c`
- if `N_SHORT_REBOOTS >= 5`, the firmware erases NVS and restarts in `main/core/reboot/reboot.c`
- if Wi-Fi credentials are missing after NVS erase, the controller falls back to AP mode in `main/core/wifi/wifi.c`
- changing `WIFI_SSID` explicitly clears the stored `WIFI_PASSWORD` in `main/core/wifi/wifi.c`

Most likely sequence for that incident:

1. maintenance OTA completed successfully
2. first boot of the maintenance image formatted SPIFFS, so the UI disappeared
3. the controller then experienced several short reboots during early boot
4. the reboot guard erased NVS
5. the controller came back on the fallback AP and looked factory-reset

What is still unconfirmed:

- the reason for the first short reboot loop

No serial boot log was captured during the failing first boot, so the initial trigger could not be proven. It may have been a panic, watchdog event, brownout, or another early-boot fault.

Additional evidence collected after recovery:

- the recovered controller was observed publishing the extended Home Assistant MQTT payload with `box_1_*`, `box_2_*`, `sensor_health_status_text`, `sensor_health_problem`, and per-box problem flags
- that observed payload shape matched the local uncommitted `main/core/mqtt/mqtt.c` worktree at the time, not the smaller committed `f653f65` MQTT payload alone

This does not prove the first reboot trigger, but it strongly suggests the deployed maintenance image was built from a dirty worktree rather than from a clean committed snapshot.

## Practical recommendations after this incident

- treat maintenance OTA as a two-step procedure: firmware OTA, then UI restore
- keep a serial monitor attached during the first boot after OTA whenever possible
- prefer deploying from a clean commit or a known archived release artifact, not from a mixed dirty worktree
- if producing release artifacts locally, capture at least:
  - `git rev-parse HEAD`
  - `git status --short`
  - `git diff --stat`
- if a controller falls back to `🤖🍁`, collect state before changing too much if possible:
  - `N_RESTARTS`
  - `OTA_TIMESTAMP`
  - `WIFI_STATUS`
  - `WIFI_IP`
- if the controller is already recovered, verify at least:
  - `OTA_STATUS = 0`
  - `WIFI_STATUS = 3`
  - `/fs/app.html` loads successfully

Example recovered live state observed after this incident:

- `OTA_TIMESTAMP = 1773766796`
- `OTA_STATUS = 0`
- `WIFI_STATUS = 3`
- `WIFI_IP = 192.168.1.104`

## Hardening recommendation

The current reboot guard is operationally risky on a field device because 5 short reboots erase the full NVS store.

A safer future change would be:

- preserve Wi-Fi credentials and user parameters
- clear only the minimum transient state needed for recovery
- or move factory-reset behavior behind an explicit opt-in instead of an automatic short-reboot threshold

---

## MQTT Log Bridge — Known Crash and Patch

### Symptom

With a reachable MQTT broker and `BOX_0_ENABLED=1`, the controller enters a reboot loop with a ~9-second period: lights briefly on, blowers start, "beep", reset, repeat. N_RESTARTS increments on each cycle.

### Root cause (leading hypothesis — unconfirmed without serial logs)

`mqtt_logging_vprintf` (in `main/core/mqtt/mqtt.c`) is registered as the global log interceptor via `esp_log_set_vprintf`. For every matching log message it sends `CMD_MQTT_FORCE_FLUSH` to the mqtt_task command queue. With `BOX_0_ENABLED=1`, multiple tasks activate simultaneously and emit a high volume of log messages. Each send wakes `mqtt_task`, which drains `log_queue` by calling `esp_mqtt_client_publish` in a tight loop. The leading hypothesis is that the MQTT client's internal publish queue overflows, triggering a panic or heap fault → reset. The exact internal failure mode (heap corruption, MQTT client assertion, task stack overflow) is unconfirmed without serial logs.

Note: the old unpatched firmware also crashed with `BOX_0_ENABLED=0` and a reachable broker once MQTT connected. An earlier claim that "BOX_0_ENABLED=0 + real broker → stable" was a false positive from insufficient monitoring duration. The confirmed discriminating variable is the custom MQTT active path (reachable broker + MQTT_EVENT_CONNECTED firing), not LED/blower load alone.

### Discriminating test (confirmed 2026-03-18)

| Condition | Result |
|-----------|--------|
| `BOX_0_ENABLED=1` + real broker | Reboot loop every ~9s |
| `BOX_0_ENABLED=1` + broker set to `mqtt://127.0.0.1:1883` | Stable 60s, LED=100%, blower=15% |

The broker-disable test was decisive: same LED and blower load, only the MQTT active path removed. This isolated the crash to the custom MQTT code path.

### Patch

Removed the `CMD_MQTT_FORCE_FLUSH` `xQueueSend` call in `mqtt_logging_vprintf`. Log messages still accumulate in `log_queue` and are drained by `mqtt_task` on its natural 10-second cycle. HA state publishing (on connect + every 30s) and HA discovery are unaffected. Log forwarding degrades from real-time to batched (~10s delay).

### Build prerequisite: `remove_key` in KV templates

A pre-existing call to `remove_key()` in `main/core/reboot/reboot.c:51` (added in a prior NVS recovery session, unrelated to the MQTT fix) caused a compile error when building the patched firmware. The function was not defined in the KV system. It was added to the tracked template sources (`main/core/kv/kv.c.template` and `main/core/kv/kv.h.template`) so it survives future `update_templates.sh` regeneration. Adding it only to generated files would have been overwritten.

### OTA delivery: firmware-only path

`build_maintenance_ota.sh` was not used because it forces a one-time SPIFFS format on first boot. Instead: `update_config.sh` + `update_templates.sh` (no `update_htmlapp.sh`), then `make -j4` directly. The controller must be stabilized (broker set to `mqtt://127.0.0.1:1883`) before triggering OTA on the old unpatched firmware, because that firmware crashes within ~9s of MQTT connecting, aborting the firmware download mid-transfer.

Important: when capturing `make` exit code, do not pipe `make` output through another command — `$?` captures the last command's exit code, which would be the pipe's right side (e.g., `tail`), not `make`'s. Use `make ... 2>&1 >/tmp/build.log; echo $?` instead.

### Validated result (2026-03-18)

| Metric | Value |
|--------|-------|
| Patched firmware `OTA_TIMESTAMP` | `1773839011` |
| Broker | `mqtt://sink2.supergreenlab.com:1883` (real) |
| `BOX_0_ENABLED` | `1` |
| Duration | 66 seconds (22 samples × 3s) |
| `LED_0_DUTY` | `100` |
| `BOX_0_BLOWER_DUTY` | `15` |
| `N_RESTARTS` before/after | `98` / `98` |
| STATE timeouts | 0 / 22 |
| Result | **Stable — reboot loop gone** |

See `Agents.md` section "MQTT Reboot Loop — Root Cause, Patch, and Validation" for the isolation path, exact diff, and build notes.

### Hardening experiment: drain-cap — REJECTED (2026-03-18)

A follow-on attempt bounded the log drain loop in `mqtt_task` to 5 publishes per cycle (`MAX_LOG_DRAIN_PER_CYCLE 5`). This immediately reintroduced the reboot loop (~9s cycle, `N_RESTARTS` incremented 68 times in 10 minutes).

Bisect confirmed: reverting the drain cap and rebuilding produced a clean 10-minute soak with zero reboots (`N_RESTARTS` stable at 101/200 samples, `STATE` timeout 0/200, real broker, `BOX_0_ENABLED=1`).

**Do not re-apply `MAX_LOG_DRAIN_PER_CYCLE` in its current form.** Keeping `log_queue` at maximum capacity permanently stresses the drop path in `mqtt_logging_vprintf` (called from every task via the global log interceptor) and causes crashes. The exact failure mode is unconfirmed without serial logs.

Validated known-good state: `OTA_TIMESTAMP=1773865200`, unbounded drain, force-flush send removed.

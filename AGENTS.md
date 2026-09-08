# Agents Notes

This repository contains firmware for a legacy ESP32 controller built with ESP-IDF 3.3.1 and the old `make` build system.

This file is the operational source of truth for Codex/OpenAI agents working on this repo.

## Safety rules

These rules override any exploratory workflow:

- Do **not** touch the live controller unless the user explicitly opens a live maintenance step.
- Do **not** reboot the controller on your own initiative.
- Do **not** switch broker on your own initiative.
- Do **not** trigger OTA on your own initiative.
- Do **not** create extra worktrees or branches unless explicitly needed and clearly announced.
- `BOX_0` operation for the plants is the top runtime priority.

Canonical emergency command:

```bash
cd /home/alan/sources/SuperGreenOS
bash ./scripts/emergency_recover_controller.sh 192.168.1.104
```

If any live step becomes unstable or uncertain, stop immediately and use the command above.

## Current proven repo facts

- Build system: legacy ESP-IDF `3.3.1` + `make`
- Generation order is mandatory before a correct build:
  1. `./update_config.sh config_gen/config/SuperGreenOS/Controllers/Controller/v3 config.controller.json`
  2. `bash ./update_templates.sh config.controller.json`
  3. `bash ./update_htmlapp.sh config.controller.json`
- `scripts/build.sh` runs that flow plus `make defconfig && make` (verified 2026-09-06 on WSL Ubuntu 22.04, firmware.bin 1046560 bytes). `setup/setup_supergreenos_build_env.sh` bootstraps the toolchain on Ubuntu/WSL.
- `config.controller.json` is tracked but generated (target `Controller/v3` since 2026-09-06, board label `sgl rev 3.11`); keep it in sync with the CUE sources.
- A clean compile was verified only after the generation flow above plus:

```bash
source ~/esp/esp-idf_release_3.3.1/export.sh
make defconfig
make -j4
```

- `scripts/build_maintenance_ota.sh` mutates tracked files during packaging (`main/core/httpd/httpd_fs.c`, `main/core/ota/ota.h`), so embedded firmware provenance currently appears as `<commit>-dirty` during the packaging flow.
- For the validated maintenance candidate, the code baseline was commit `0b1eda8`, but packaging provenance is operationally described as:
  - source baseline = `0b1eda8`
  - packaged firmware metadata = `0b1eda8-dirty`

## Current strongest firmware findings

### MQTT startup fragility

Strongest current root-cause candidate for the local-broker instability:

- the connect-time MQTT bootstrap/discovery/state sequence was failure-oblivious
- discovery/state assumed a continuously valid connection
- discovery progress was not treated transactionally
- `first_connect` could be cleared too early

The candidate mitigation is the `mqttdiag`/discovery hardening line of work around `0b1eda8`.

### OTA trigger blocker

Strongest current root-cause candidate for OTA not starting:

- `OTA_START=1` is accepted through HTTP/KV
- OTA dispatch is edge-triggered via an OTA queue send
- queue-send failure is currently silent in the old code path
- therefore `OTA_START` can appear set without the OTA task ever beginning the first HTTP fetch

Planned hardening direction:

- shared `request_ota_start(int value)` helper in `ota.c/.h`
- HTTP/KV and MQTT OTA trigger paths must both use the same dispatch semantics
- failed dispatch must not leave a false-positive `OTA_START=1`

Implemented on this branch (build-verified, not yet hardware-tested):

- `OTA_START` is released to `0` by `ota_task` once the request is handled; read `OTA_STATUS` for the outcome (`0` idle/up-to-date, `1` in progress, `2` disabled, `3` failed)
- a new `OTA_START=1` while a previous request is still queued or running is logged and ignored (`OTA_START` stays `1`), so a double click or a Home Assistant retry cannot queue a second update
- `GET /dash` (2026-09-07) returns one chunked JSON with every box (`i`, `enabled`, `temp`, `humi`, `vpd` kPa×100, `co2`, `weight`, `led_dim`, `started_at`, `duration_days`, timer/fan/blower/watering fields guarded by their `MODULE_*`), the LEDs (`box`, `duty`, `dim`), `sensor_health` and `time`; the web dashboard uses it instead of ~40 `GET /i` per refresh (each open socket costs 3-4 KB heap) and falls back to key-by-key reads on a 404
- `GET /kv` (2026-09-08) returns every readable parameter in one chunked JSON, `{"i":{"NAME":int,...},"s":{"NAME":"str",...}}`, walking the generated `kv_mapping` tables (keys with `http.read`, `*_PASSWORD` strings omitted); the app's `fetchAllParams` uses it instead of ~290 `GET /i`/`/s` (40 s → under 5 s) and falls back to key-by-key reads on a 404/405
- `sensor_health` (2026-09-08) raises `box_N_sensor_stuck` only when temp, humi, vpd and co2 of that box all stayed identical for `SENSOR_HEALTH_STUCK_SAMPLES`; the earlier per-metric `box_N_temp_stuck` fired in a still room because integer °C/% do not move for an hour while VPD (0.01 kPa from the float readings) does
- `/mqttdiag` reports `mqtt_connected`, `ota_status`, `reset_reason` (esp_reset_reason_t of the current boot: 1 power-on, 3 software, 4 panic, 5/6/7 watchdogs, 9 brownout), `reset_history` (last 10 reset reasons, newest first), `heap_free`, `heap_min_free`, `heap_min_free_at` (uptime in s when the heap minimum was last lowered, sampled every 5 s by `heap_watch_task` in `reboot.c`), `heap_low_events` (times free heap dipped under 8 KB since boot), `uptime_s`, `fs_used`/`fs_total` (SPIFFS bytes, used by `upload_htmlapp.sh` to refuse a UI that would not fit), `nvs_used`/`nvs_free` (NVS entries on the 16 KB partition), `mqtt_stack_hwm` (bytes) and `time_valid` (clock past 2017); the same fields (minus `mqtt_disc_idx`/`wifi_status`/`reset_history`) are also published periodically over MQTT since the broker is a third-party service reached over the internet
- `try_ota()` verifies the download against `<basedir>/<ts>/firmware.bin.sha256` when the server publishes it (see `scripts/build_maintenance_ota.sh`) before calling `esp_ota_set_boot_partition`; a missing hash file is a warning, not a hard failure, a mismatch aborts the update
- `CONFIG_APP_ROLLBACK_ENABLE=y`: a freshly OTA'd image is confirmed valid ~30 s after boot (`ota.c`'s `confirm_valid_task`); a boot-loop before that point auto-reverts to the previous image on the next boot
- `request_ota_start()` backs off after consecutive failures (1/2/4/.../15 min) instead of accepting an immediate retry; `OTA_STATUS_FAILED` and the log line explain a rejected request
- all actuator loops (`mqtt_task`, `watering_task`, `motor_task`, `fan_task`, `blower_task`, `valve_task`) are registered with the ESP-IDF task watchdog; `sanity_check_config()` in `app_main.c` clamps every 0-100 PWM/percentage field at boot; `wifi.c` clamps the AP station counter at 0 and logs dropped queue commands (`@WIFI cmd queue full, dropped ...`); `is_ref_source_absent()` is shared via `main/core/ref_source.h` — 2026-09-06 second batch, build-verified, not yet hardware-tested

## Frozen runbooks

### 1. OTA-delivery-only runbook

Use this before any MQTT validation.

Goal:
- prove that a freshly built maintenance artifact is actually fetched and installed

Success conditions:
- local OTA server proven ready first
- controller fetches both:
  - `/SuperGreenMaintenance/last_timestamp`
  - `/SuperGreenMaintenance/<TS>/firmware.bin`
- both fetches return `200`
- controller becomes reachable again
- `OTA_TIMESTAMP == <TS>`

Only after those conditions are true does `/mqttdiag` validation become meaningful.

### 2. Emergency recovery runbook

See `docs/live-experiment-emergency-recovery.md`.

## Preferred Codex workflow

### Offline tasks Codex may do directly

- repo forensics
- code search
- patch preparation
- generation/build/package work
- docs updates
- script inspection

### Live tasks Codex may only do when explicitly told

- controller HTTP writes
- reboot
- OTA trigger
- broker change
- maintenance-window execution

## Codex repo customization

This repo is intended to use:

- `AGENTS.md` for persistent repo instructions
- `.codex/config.toml` for default model/sandbox/approval/subagent limits
- `.codex/agents/*.toml` for bounded specialist agents

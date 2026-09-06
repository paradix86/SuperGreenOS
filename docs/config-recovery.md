# Config Recovery After NVS Erase

This document covers the procedure and findings from a full config-loss recovery performed on `192.168.1.104` in March 2026, following a reboot loop that triggered NVS erase via the firmware's reboot guard.

See `docs/build-and-ota.md` for the OTA incident postmortem that preceded this recovery.

---

## Incident Context

After a maintenance OTA, the controller experienced a short reboot loop. The firmware's reboot guard (`main/core/reboot/reboot.c`) erased NVS after detecting 5 or more short reboots. This caused:

- all controller config reset to firmware defaults
- the controller appearing online and responding to HTTP, but producing zero output
- lights off, motors stopped, blower/fan idle

The controller was not broken — it was inert because its NVS config was gone.

---

## Why the Controller Looks Online but Inert After NVS Erase

Three independent blockers are active simultaneously after NVS erase. All three must be fixed before any output works.

### 1. `STATE = FIRST_RUN (0)`

`mixer_task()` in `main/mixer/mixer.c` checks:

```c
if (s != RUNNING) { vTaskDelay(5000); continue; }
```

With `STATE = 0` (default), the mixer sleeps continuously and never drives any LED.

Fix: `POST /i?k=STATE&v=2`

### 2. `BOX_N_TIMER_TYPE = TIMER_MANUAL (0)`

`mixer_task()` also checks:

```c
if (get_box_enabled(i) != 1 || get_box_timer_type(i) == TIMER_MANUAL) continue;
```

Even with STATE=RUNNING and the box enabled, a MANUAL timer type causes the mixer to skip the box.

Fix: `POST /i?k=BOX_0_TIMER_TYPE&v=1` (1 = ONOFF schedule)

### 3. `LED_N_BOX = -1` (default for all 6 channels)

`set_all_duty()` in `mixer.c` filters LEDs by box assignment:

```c
if (get_led_type(i) != type) continue;
if (get_led_box(i) != boxId) continue;
```

With `LED_N_BOX = -1`, every LED channel is skipped. `BOX_0_TIMER_OUTPUT` can be 100 and the mixer can be fully active, yet all LED duties remain 0.

Fix: `POST /i?k=LED_N_BOX&v=0` for all channels belonging to box 0 (N = 0..5 for this controller).

---

## Recovery Source: Exported JSON

The Android app (`com.supergreenlab.app2`) periodically caches all 291 controller keys in a local SQLite database. It can also export this as a JSON file.

Known export file for this controller:

```
/home/alan/Scaricati/supergreenos-config-2026-03-15T08-08-46-643Z.json
```

This file was used as the primary recovery source.

### Android app as a recovery path

The app DB is at `/data/data/com.supergreenlab.app2/app_flutter/db.sqlite` on the device. Extraction was investigated and ruled out:

- `android:allowBackup="false"` is set in the app manifest — ADB backup is blocked
- `adb shell run-as com.supergreenlab.app2` fails on a release build — package is not debuggable
- the app Settings page has no export/import/backup UI

The exported JSON became the recovery source. The app UI display was used as a secondary cross-check (the app still showed pre-erase cached values).

---

## Controller HTTP API Format

Confirmed via firmware code and live testing:

- `GET /i?k=KEY` returns **raw integer text only** — e.g. `2`, not `v=2`
- `POST /i?k=KEY&v=VALUE` returns `OK` on success
- HTTP 404 if key is not in `kv_mapping.c`
- returns `0` if key has `getter = NULL` in `kv_mapping.c` — this is an artifact, not an NVS value

Scripts must not parse `v=...` format.

---

## Key Classification

### Must not be replayed (read-only / computed)

These keys are computed at runtime and have no setter in `kv_mapping.c`. Writing them via POST will fail or be silently ignored. Do not include them in restore scripts.

| Category | Keys |
|----------|------|
| Sensor readings | `BOX_N_TEMP`, `BOX_N_HUMI`, `BOX_N_VPD`, `BOX_N_CO2`, `BOX_N_WEIGHT` |
| Computed timer | `BOX_N_TIMER_OUTPUT`, `*_UVA_TIMER_OUTPUT`, `*_DB_TIMER_OUTPUT`, etc. |
| Computed duties | `BOX_N_FAN_DUTY`, `BOX_N_BLOWER_DUTY`, `MOTOR_N_DUTY` |
| Timestamps | `started_at`, `watering_last`, `simulated_time` |
| Runtime/system | `wifi_*`, `ip_*`, `reboot`, `ota_*`, `time_*` |
| GPIO (artifact) | `LED_N_GPIO` — getter and setter both NULL; GET returns 0 |

### Safe to replay (Phase 1)

Confirmed writable. No physical risk.

| Group | Keys |
|-------|------|
| Core state | `STATE`, `BOX_0_ENABLED`, `BOX_0_TIMER_TYPE` |
| LED mapping | `LED_N_BOX`, `LED_N_TYPE` (N = 0..5) |
| Schedule | `BOX_0_ON_HOUR`, `BOX_0_ON_MIN`, `BOX_0_OFF_HOUR`, `BOX_0_OFF_MIN` |
| Fan tuning | `BOX_0_FAN_MIN/MAX`, `BOX_0_FAN_REF_MIN/MAX/SOURCE` |
| Blower tuning | `BOX_0_BLOWER_MIN/MAX`, `BOX_0_BLOWER_REF_MIN/MAX/SOURCE` |
| Motor bounds | `MOTOR_N_MIN`, `MOTOR_N_MAX` (N = 0..2) |

### Requires manual review (Phase 2)

These keys directly activate or change motor hardware behavior. Restore one at a time with physical observation.

| Key | Risk |
|-----|------|
| `MOTORS_CURVE` | Changes PWM calculation for all motors — restore before source changes |
| `MOTOR_N_SOURCE` | Routes motor to a duty signal and may physically start a stopped motor |

---

## Phase 1 Replay — Confirmed Results (2026-03-18)

Executed via `compare_and_restore.sh` (diff-only, grouped by purpose).

### Keys changed (17 total)

| Key | Was | Set to |
|-----|-----|--------|
| `BOX_0_ON_HOUR` | 3 | 5 |
| `BOX_0_OFF_HOUR` | 21 | 23 |
| `BOX_0_FAN_MIN` | 8 | 20 |
| `BOX_0_FAN_REF_MIN` | 21 | 0 |
| `BOX_0_FAN_REF_MAX` | 30 | 100 |
| `BOX_0_FAN_REF_SOURCE` | 1 | 8 |
| `BOX_0_BLOWER_MIN` | 9 | 15 |
| `BOX_0_BLOWER_MAX` | 30 | 60 |
| `BOX_0_BLOWER_REF_MIN` | 21 | 55 |
| `BOX_0_BLOWER_REF_MAX` | 30 | 70 |
| `BOX_0_BLOWER_REF_SOURCE` | 1 | 15 |
| `MOTOR_0_MIN` | 0 | 8 |
| `MOTOR_0_MAX` | 100 | 51 |
| `MOTOR_1_MIN` | 0 | 8 |
| `MOTOR_1_MAX` | 100 | 50 |
| `MOTOR_2_MIN` | 0 | 8 |
| `MOTOR_2_MAX` | 100 | 50 |

Core state and LED mapping were already correct from prior manual recovery (not changed by the script).

### Keys skipped (already correct)

`STATE`, `BOX_0_ENABLED`, `BOX_0_TIMER_TYPE`, `LED_0..5_BOX`, `LED_0..5_TYPE`, `BOX_0_FAN_MAX`, `BOX_0_ON_MIN`, `BOX_0_OFF_MIN`.

### Verification results

All 46 checks passed. After a 20-second wait (to allow `blower_task` to cycle):

| Key | Value |
|-----|-------|
| `BOX_0_TIMER_OUTPUT` | 100 |
| `LED_0..5_DUTY` | 100 |
| `BOX_0_BLOWER_DUTY` | 15 (was 9 before Phase 1) |
| `BOX_0_FAN_DUTY` | 20 (was 8 before Phase 1) |

---

## Output-Triggered Reboot Loop Investigation

### Observed symptoms

After Phase 1, re-enabling `BOX_0_ENABLED=1` with `MOTOR_0_SOURCE=1` (active) caused a boot loop:

- lights on briefly, blowers start, then full reset with a "beep" sound, repeat
- setting `BOX_0_ENABLED=0` immediately stopped the loop
- isolating `MOTOR_0_SOURCE=0` with `MOTOR_0_DUTY_TESTING=0` and then setting `BOX_0_ENABLED=1` kept the controller stable for >15 seconds

This confirmed: the boot loop is triggered by motor 0 drawing current, not by lighting or blower/fan outputs.

### Duty-testing ramp attempt

Attempted to find the minimum safe duty by ramping `MOTOR_0_DUTY_TESTING` in small steps (5 → 8 → 10 → 12 → 15).

Result: setting `DUTY_TESTING=5` caused a reboot within the next `motor_task` cycle (~10 seconds). The `motor_task` loop runs at 10-second intervals (`xQueueReceive` with a 10-second timeout in `main/motor/motor.c`). The step from 0% to 5% raw PWM was sufficient to trigger a reset.

This approach was abandoned. `source=0` with duty_testing bypasses both the motor curve and the MIN/MAX scaling — it is a diagnostic tool, not a valid soft-start path.

### Leading hypothesis

`CONFIG_BROWNOUT_DET=y` with `CONFIG_BROWNOUT_DET_LVL=0` is confirmed in `sdkconfig`. Level 0 corresponds to approximately 2.43V. The hypothesis is that motor 0 inrush current drops the VCC rail below this threshold, triggering reset.

**This is the leading hypothesis, not a confirmed fact.** No serial log or voltage probe data was captured during the loop.

### Why the original config may have avoided this

The exported JSON contains `MOTORS_CURVE=0`. The live controller has `MOTORS_CURVE=1` (not yet restored).

With `MOTORS_CURVE=1` and `MOTOR_0_SOURCE=1` (blower duty = 15):
```
PWM ≈ 8 * pow(1.025, 15) + 5 ≈ 22%
```

With `MOTORS_CURVE=0` and the same blower duty:
```
PWM = MOTOR_0_MIN + (MOTOR_0_MAX - MOTOR_0_MIN) * 15 / 100 = 8 + (51-8)*0.15 ≈ 14%
```

The original working config produced ~14% PWM. The current mismatched MOTORS_CURVE produces ~22%. The additional ~8% load may be what pushes the supply below the brownout threshold.

### Recommended next action (Phase 2)

1. Set `MOTORS_CURVE=0` first (reduces motor PWM to match original config)
2. Set `MOTOR_0_SOURCE=15` (fan, not blower — per JSON)
3. Observe physical behavior — motor should start and stay running
4. If stable, proceed to `MOTOR_1_SOURCE=15` and `MOTOR_2_SOURCE=1`

---

## Recovery Scripts

Two scripts are maintained in the repo root:

### `compare_and_restore.sh`

Diff-only Phase 1 restore. For each key in the safe set:
1. reads the live value from the controller
2. compares to the desired value
3. only POSTs if different

Grouped by: core state, lighting mapping, schedule, fan, blower, motor bounds.

Phase 2 keys (MOTORS_CURVE, MOTOR_N_SOURCE) are printed as a deferred reference block but never executed automatically.

### `verify_restore.sh`

Reads back all restored keys plus computed outputs:
- all Phase 1 config keys
- `BOX_0_TIMER_OUTPUT`, `LED_0..5_DUTY`, `BOX_0_BLOWER_DUTY`, `BOX_0_FAN_DUTY`
- Phase 2 keys (source, curve) as READ-only informational lines

Parses raw integer response (not `v=...`).

---

## Mandatory abort path for unstable live experiments

For unstable live test runs (reboot loop, broker-switch failure, uncertain runtime state), the canonical emergency abort path is now fixed and must be applied first:

1. `POST /i?k=BOX_0_ENABLED&v=1` (retry loop)
2. `POST /s?k=BROKER_URL&v=mqtt%3A%2F%2F192.168.1.1%3A9999` (retry loop)
3. `POST /s?k=BROKER_CLIENTID&v=304a4fd6eb4c` (retry loop)
4. `POST /i?k=REBOOT&v=1` (retry loop)
5. verify repeatedly:
   - `BROKER_URL = mqtt://192.168.1.1:9999`
   - `BROKER_CLIENTID = 304a4fd6eb4c`
   - `STATE = 2`
   - `WIFI_STATUS = 3`
   - `BOX_0_ENABLED = 1`
   - `N_RESTARTS` stable
   - `LED_0_DUTY` and `BOX_0_BLOWER_DUTY` are logged/reported (not hard-fail gates)

Reusable canonical script:

```bash
cd /home/alan/sources/SuperGreenOS
bash ./scripts/emergency_recover_controller.sh 192.168.1.104
```

Important: on unstable diagnostic firmware, do **not** use `sink2` as the immediate emergency target. Use the unreachable broker target first to stabilize, then decide the next runtime target separately.

---

## Firmware Reference

Key files for understanding the output chain:

| File | What it controls |
|------|-----------------|
| `main/mixer/mixer.c` | LED duty computation; STATE and TIMER_TYPE guards |
| `main/timer/timer.c` | TIMER_OUTPUT computation; ONOFF schedule |
| `main/blower/blower.c` | BOX_N_BLOWER_DUTY computation (10s cycle) |
| `main/motor/motor.c` | Motor PWM from source routing + curve (10s cycle) |
| `main/core/kv/kv.c` | NVS defaults for all keys |
| `main/core/kv/kv_mapping.c` | Getter/setter presence = writability |
| `main/core/kv/kv_helpers.c` | `get_motor_N_duty()` source routing switch |
| `main/core/httpd/httpd.c` | `/i` GET and POST handler |
| `sdkconfig` | `CONFIG_BROWNOUT_DET=y`, `CONFIG_BROWNOUT_DET_LVL=0` |

### NVS defaults that matter most

| Key | Default | Effect |
|-----|---------|--------|
| `STATE` | 0 (FIRST_RUN) | mixer_task sleeps, no LED output |
| `LED_N_BOX` | -1 | all LEDs skipped by set_all_duty |
| `BOX_N_TIMER_TYPE` | 0 (MANUAL) | mixer skips box regardless of STATE |
| `BOX_N_ENABLED` | 0 | mixer skips disabled box |
| `MOTOR_N_MIN` | 0 | no low-end clamping |
| `MOTOR_N_MAX` | 100 | no high-end clamping |
| `MOTORS_CURVE` | 1 | exponential PWM amplification active |

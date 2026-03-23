# Backend Roadmap

The recovered UI contains several automation-oriented panels that currently deserve real backend support.

## Recommended implementation order

1. `Sensor Health`
2. `Dynamic Climate Setpoint`
3. `Energy Optimization`
4. `History & KPI`

## Why this order

### 1. Sensor Health

Phase 1 is now implemented and deployed in a first working form.

What already exists:

- generated config in `config_gen/config/SuperGreenOS/Controllers/sensor_health.cue`
- backend module in `main/sensor_health/`
- live generated keys such as:
  - `sensor_health_enabled`
  - `sensor_health_period_s`
  - `sensor_health_warmup_samples`
  - `sensor_health_stuck_samples`
  - `sensor_health_status`
  - `sensor_health_last_alert`
- compact read-only UI bridge in `html_app/sensor-health.custom.js`

Verified live device state included:

- `SENSOR_HEALTH_STATUS = 3`
- `SENSOR_HEALTH_LAST_ALERT = box_0_co2_stuck`

Why it was the best first backend feature:

- is mostly diagnostic
- has low actuator risk
- produces useful persistent state for UI, HTTP, MQTT, and logs

Suggested outputs:

- sensor stuck detection
- missing data detection
- outlier warnings
- calibration reminders

### 2. Dynamic Climate Setpoint

Best first control-side policy because the firmware already has control modules that consume references.

This should update target ranges for:

- fan
- blower
- climate references

### 3. Energy Optimization

Good next step once climate policy exists.

This can apply caps and schedule-aware limits to:

- fan maximums
- blower maximums
- optional lighting limits

### 4. History & KPI

Start small.

Use aggregated counters and rolling summaries first instead of a heavy full-history design.

## Recommended next phase

Keep building:

- `climate_policy`

That extends the first backend win without spreading logic everywhere.

## Integration points already present in the repo

- `main/init.c`
- `main/core/app_main.c`
- `main/core/kv/`
- `main/core/stat_dump/`
- `main/fan/`
- `main/blower/`
- `main/watering/`
- `config_gen/config/`

## Boilerplate comparison shortlist

When looking for reusable upstream ideas, compare these areas against `SuperGreenOSBoilerplate` first:

- `config_gen/config/`
- `templates/`
- `main/core/`
- root generation scripts

Do not treat the boilerplate as an embedded dependency; use it as a selective upstream reference.

## MQTT / Home Assistant MVP

The first Home Assistant-oriented MQTT layer should stay intentionally small and additive.

Current implemented scope:

- retained availability topic: `supergreen/<clientid>/availability`
- retained state topic: `supergreen/<clientid>/state`
- retained MQTT discovery topics under `homeassistant/sensor/<clientid>/<object_id>/config`

Current exported entities:

- `box_0_temp`
- `box_0_humi`
- `box_0_vpd`
- `box_0_co2`
- `box_1_temp`
- `box_1_humi`
- `box_1_vpd`
- `box_1_co2`
- `box_2_temp`
- `box_2_humi`
- `box_2_vpd`
- `box_2_co2`
- `sensor_health_status`
- `sensor_health_status_text`
- `sensor_health_last_alert`
- `sensor_health_problem`
- `box_0_sensor_problem`
- `box_1_sensor_problem`
- `box_2_sensor_problem`

Current HA controls exported:

- `button.reboot`
- `button.ota_start`
- `switch.sensor_health_enabled`
- `number.sensor_health_period_s`

This deliberately does not replace the existing MQTT log stream. It adds a Home Assistant-friendly layer next to it.

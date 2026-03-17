# Backend Roadmap

The recovered UI contains several automation-oriented panels that currently deserve real backend support.

## Recommended implementation order

1. `Sensor Health`
2. `Dynamic Climate Setpoint`
3. `Energy Optimization`
4. `History & KPI`

## Why this order

### 1. Sensor Health

Best first backend feature because it:

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

## Recommended phase 1

Build only:

- `sensor_health`
- `climate_policy`

That gives a strong first backend PR without spreading logic everywhere.

## Integration points already present in the repo

- `main/init.c`
- `main/core/app_main.c`
- `main/core/kv/`
- `main/core/stat_dump/`
- `main/fan/`
- `main/blower/`
- `main/watering/`
- `config_gen/config/`

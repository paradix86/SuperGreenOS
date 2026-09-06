# Documentation

This folder is the operational documentation home for `SuperGreenOS`.

## Start here

- [Environment Setup](./environment-setup.md)
- [Build and OTA](./build-and-ota.md)
- [OTA Delivery Only](./ota-delivery-only.md)
- [Live Experiment Emergency Recovery](./live-experiment-emergency-recovery.md)
- [Config Recovery](./config-recovery.md)
- [UI Customization](./ui-customization.md)
- [Backend Roadmap](./backend-roadmap.md)

## What is currently proven

- ESP-IDF `3.3.1` legacy `make` build works on modern Ubuntu and on WSL2 Ubuntu 22.04 when generation order is respected (`scripts/build.sh` encodes it; `setup/setup_supergreenos_build_env.sh` bootstraps the toolchain).
- The required generation order is:
  1. `update_config.sh`
  2. `update_templates.sh`
  3. `update_htmlapp.sh`
- Clean compile requires:
  - `source ~/esp/esp-idf_release_3.3.1/export.sh`
  - `make defconfig`
  - `make -j4`
- Emergency stabilization of the live controller is standardized and documented.
- OTA delivery and MQTT/fix validation must be treated as **two separate phases**.

## Important operational split

### OTA-delivery-only phase

Goal:
- prove the new firmware is actually fetched and installed

Only after:
- controller fetches `last_timestamp`
- controller fetches `firmware.bin`
- controller comes back
- `OTA_TIMESTAMP` equals the newly built package timestamp

does MQTT validation become meaningful.

### MQTT validation phase

This is separate and must happen only after OTA delivery is proven.

## See also

- Repo-level guardrails and Codex instructions live in `../Agents.md`.

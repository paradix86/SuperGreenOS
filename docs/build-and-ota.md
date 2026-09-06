# Build and OTA

This document describes the **current validated build flow** and the **OTA packaging flow**.

## 1. Required generation/build flow

One-shot (runs the whole flow below, verified on WSL Ubuntu 22.04 in 59 s):

```bash
scripts/build.sh                 # Controller v3: CUE -> JSON -> templates -> UI -> make
scripts/build.sh --gen-only      # generation only (also works on Windows Git Bash)
scripts/build.sh --skip-config   # reuse the committed config.controller.json (no cue needed)
SGOS_BUILD_DIR=/root/sgos-build scripts/build.sh   # native build dir when the repo is on /mnt/c
```

The generation scripts now fail loudly: a missing `ejs-cli`/`cue` or a failed render no
longer leaves 0-byte generated sources behind.

Manual equivalent:

```bash
cd /home/alan/sources/SuperGreenOS
./update_config.sh config_gen/config/SuperGreenOS/Controllers/Controller/v3 config.controller.json
bash ./update_templates.sh config.controller.json
bash ./update_htmlapp.sh config.controller.json
```

Then build:

```bash
source ~/esp/esp-idf_release_3.3.1/export.sh
make defconfig
make -j4
```

If this order is skipped, the build can fail with missing generated scaffold symptoms such as `undefined reference to app_main`.

`config.controller.json` is committed for convenience (UI testing without `cue`), but it is a
generated file: regenerate and commit it whenever the CUE sources change. Before 2026-09-06 the
committed copy was a stale `Controller/v3` export without the `sensor_health` module, which
made `mqtt.c` fail to link unless `update_config.sh` was run first.

Profile note: `Controller/v2.1` and `Controller/v3` only differ in defaults
(`MOTOR_N_MIN` 0 vs 8, `MOTORS_CURVE` 1 vs 0, `OTA_BASEDIR` `/ControllerV2.1` vs `/ControllerV3`,
and whether `MOTOR_N_FREQUENCY` is an NVS key). Defaults only apply to keys missing from NVS.
Alan's board is labelled `sgl rev 3.11` and its NVS already holds the v3 values (curve 0, motor
min 8, 40 kHz), so since 2026-09-06 the default target is `Controller/v3`: after an NVS erase the
firmware then comes back with the right motor defaults for this hardware.

## 2. Packaging an OTA artifact

`scripts/build_maintenance_ota.sh` packages `releases/<basedir>/<TS>/firmware.bin`,
`last_timestamp`, `firmware.bin.sha256` and `git_commit.txt` from the current commit.
It requires a clean worktree and up-to-date generated sources (run `scripts/build.sh`
first), patches `OTA_BUILD_TIMESTAMP` (and optionally the one-time SPIFFS format flag)
in two tracked sources for the duration of the build, and restores them with
`git checkout` on exit. The firmware version string therefore reads `<commit>-dirty`;
`git_commit.txt` records the real baseline.

Environment knobs:

| Variable | Default | Meaning |
|----------|---------|---------|
| `SGOS_SPIFFS_FORMAT_ONCE` | `1` | `1`: maintenance build, formats SPIFFS once at boot (web UI must be re-uploaded). `0`: plain firmware update, SPIFFS untouched. |
| `SGOS_OTA_BASEDIR` | `SuperGreenMaintenance` | directory under `releases/`, must match the controller's `OTA_BASEDIR` |
| `SGOS_BUILD_DIR` | `./build` | `make BUILD_DIR_BASE`; use a native path on WSL (`/root/sgos-build`) |
| `JOBS` | `nproc` | parallel jobs |

Verified on WSL (2026-09-06, 24 s with a warm build dir):

```bash
export IDF_PATH=~/esp/esp-idf_release_3.3.1
. "$IDF_PATH/export.sh"
SGOS_BUILD_DIR=/root/sgos-build SGOS_SPIFFS_FORMAT_ONCE=0 JOBS=16 bash scripts/build_maintenance_ota.sh
```

## 3. Choosing the SPIFFS flag

Use `SGOS_SPIFFS_FORMAT_ONCE=0` for firmware-only validation: the controller keeps
`app.html`/`config.json` and comes back with its UI. Use the default `1` only when the
SPIFFS partition itself must be recreated (corrupted filesystem, failed uploads after
OTA); afterwards restore the UI with `update_htmlapp.sh` + `upload_htmlapp.sh`.

## 4. Packaging outputs that must exist before OTA

Before any live OTA step, these files must be present and fresh:

- `releases/SuperGreenMaintenance/<TS>/firmware.bin`
- `releases/SuperGreenMaintenance/last_timestamp`
- `releases/SuperGreenMaintenance/<TS>/firmware.bin.sha256`

## 5. Important rule

Do not mix packaging validation with live controller validation.

First:
- produce fresh package
- verify timestamp and SHA

Then separately follow the OTA-delivery-only runbook in `ota-delivery-only.md`.

# Build and OTA

This document describes the **current validated build flow** and the **practical maintenance packaging workaround**.

## 1. Required generation/build flow

One-shot (runs the whole flow below, verified on WSL Ubuntu 22.04 in 59 s):

```bash
scripts/build.sh                 # Controller v2.1: CUE -> JSON -> templates -> UI -> make
scripts/build.sh --gen-only      # generation only (also works on Windows Git Bash)
scripts/build.sh --skip-config   # reuse the committed config.controller.json (no cue needed)
SGOS_BUILD_DIR=/root/sgos-build scripts/build.sh   # native build dir when the repo is on /mnt/c
```

The generation scripts now fail loudly: a missing `ejs-cli`/`cue` or a failed render no
longer leaves 0-byte generated sources behind.

Manual equivalent:

```bash
cd /home/alan/sources/SuperGreenOS
./update_config.sh config_gen/config/SuperGreenOS/Controllers/Controller/v2.1 config.controller.json
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

## 2. Maintenance packaging reality

`scripts/build_maintenance_ota.sh` currently mutates tracked files during packaging, notably:

- `main/core/httpd/httpd_fs.c`
- `main/core/ota/ota.h`

As a result, the packaged firmware metadata appears as `<commit>-dirty` during packaging.

For the validated maintenance candidate flow, the practical provenance is:

- code baseline: target commit (for example `0b1eda8`)
- packaged firmware metadata: `<commit>-dirty`

## 3. Practical packaging workaround

Use a temporary detached worktree at the exact target commit.

```bash
ROOT=/home/alan/sources/SuperGreenOS
BASE=0b1eda8
WT=/tmp/SuperGreenOS-ota-pack-$(date +%s)

git -C "$ROOT" worktree add --detach "$WT" "$BASE"
cd "$WT"

./update_config.sh config_gen/config/SuperGreenOS/Controllers/Controller/v2.1 config.controller.json
bash ./update_templates.sh config.controller.json
bash ./update_htmlapp.sh config.controller.json

source ~/esp/esp-idf_release_3.3.1/export.sh
make defconfig

git update-index --assume-unchanged   config.controller.json   main/core/mqtt/mqtt.h   sdkconfig   spiffs_fs/app.html   spiffs_fs/config.json

bash ./scripts/build_maintenance_ota.sh

TS=$(cat releases/SuperGreenMaintenance/last_timestamp)
BIN="releases/SuperGreenMaintenance/$TS/firmware.bin"
sha256sum "$BIN" | tee /tmp/maintenance_${TS}.sha256

mkdir -p "$ROOT/releases/SuperGreenMaintenance/$TS"
cp "$BIN" "$ROOT/releases/SuperGreenMaintenance/$TS/firmware.bin"
echo "$TS" > "$ROOT/releases/SuperGreenMaintenance/last_timestamp"
cp /tmp/maintenance_${TS}.sha256 "$ROOT/releases/SuperGreenMaintenance/$TS/firmware.bin.sha256"

git update-index --no-assume-unchanged   config.controller.json   main/core/mqtt/mqtt.h   sdkconfig   spiffs_fs/app.html   spiffs_fs/config.json

cd "$ROOT"
git worktree remove --force "$WT"
git worktree prune
```

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

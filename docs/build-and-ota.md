# Build And OTA

This document captures the workflow that was validated end-to-end in March 2026.

## Build firmware

```bash
cd /home/alan/sources/SuperGreenOS
bash ./update_templates.sh config.controller.json
bash ./update_htmlapp.sh config.controller.json

export IDF_PATH=$HOME/esp/esp-idf_release_3.3.1
. $IDF_PATH/export.sh

bash ./scripts/build_maintenance_ota.sh
```

Expected output:

- `releases/SuperGreenMaintenance/last_timestamp`
- `releases/SuperGreenMaintenance/<timestamp>/firmware.bin`

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

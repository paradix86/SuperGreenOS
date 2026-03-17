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

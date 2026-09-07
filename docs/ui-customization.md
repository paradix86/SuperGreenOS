# UI Customization

This project has two different UI layers that are easy to confuse.

## Source of truth

Edit UI source here (all readable, no minified blobs are checked in):

- `html_app/index.html` — EJS shell, includes the CSS and JS below
- `html_app/utils.js` — request queue (max 2 requests in flight), `fetchConfig`/`fetchParam`/`updateParam`/`fetchJson`, global status banner
- `html_app/onload.js` — header, connection badge, config export/import, the generated parameter forms, the top tabs
- `html_app/dashboard.js` — the Dashboard tab (see below)
- `html_app/style.css`, `html_app/dashboard.css`, `html_app/normalize.min.css`

Do not treat `spiffs_fs/app.html` as the long-term source of truth.

## Dashboard tab (2026-09-06)

The default tab is a read-only overview built from one `GET /mqttdiag` plus the `/i` and `/s` keys of the enabled boxes:

- **Controller** card: Wi-Fi / MQTT / clock / OTA chips, uptime, free heap and heap minimum (`heap_min_free_at`, `heap_low_events` when the firmware reports them), restarts, last reset reason, broker, firmware `sensor_health_status` / `sensor_health_last_alert`
- **Sensor health (firmware)** form: the four `sensor_health_*` settings with an explicit Save (only changed keys are written)
- one card per enabled box (tick "show disabled boxes" for the others): temperature, RH, VPD (`box_N_vpd` is kPa × 100 and is shown in kPa), CO₂ when a sensor reports it, day/night from `timer_output`, timer type and on/off schedule, LED/fan/blower bars with the `*_ref_source` helper text, watering summary (`watering_period`/`watering_duration` are seconds), season day
- **LEDs** table: channel → box, duty, dim
- 3-hour sparklines for temperature, RH and VPD, sampled every 30 s **while the page is open** and kept in this browser's `localStorage` (`supergreen.dashboard.v1`); the controller has no history storage

Polling: one refresh every 30 s (~35 requests for one enabled box), slow-changing keys (schedule, watering config, ref sources, LED→box mapping) every 5 min.

## Removed: browser-side "Automation & Insights"

The recovered UI carried an `Automation & Insights` panel (Sensor Health, Energy Optimization, Dynamic Climate Setpoint, History & KPI) implemented in the minified `onload.custom.js`. It was removed on 2026-09-06 because it was unreliable and, in two cases, harmful:

- *Dynamic Climate Setpoint* wrote `box_N_fan_ref_min/max` and `blower_ref_min/max` as "target ± tolerance" without looking at `*_ref_source`; on a box whose fan follows the timer output (source 8) that breaks the fan, and its VPD numbers were 100× off (firmware VPD is kPa × 100)
- *Energy Optimization* only ever lowered `led_dim`/`fan_max`/`blower_max` to the current cap and never raised them back: after one night the LED stayed at the night cap
- the browser-side *Sensor Health* shared its inputs with the firmware bridge (`sensor-health.custom.js`), which relabelled them; saving from one corrupted the other's settings and both fought over the same summary element
- *History & KPI* only existed while a tab was open, so 24 h / 7 d figures were mostly empty

The firmware-side sensor health (`main/sensor_health/`) stays and is what the Dashboard shows.

## Generated output

`update_htmlapp.sh` renders the HTML app into:

- `spiffs_fs/app.html`
- `spiffs_fs/config.json`

Those files are upload payloads, not the preferred place for long-term editing.

## Important behavior discovered during OTA recovery

The controller had a customized UI in SPIFFS that was not fully represented by the repo before recovery.

After maintenance OTA:

- SPIFFS was formatted
- the previous UI disappeared
- uploading the current repo UI restored only what existed in source

This is why UI recovery was moved back into `html_app/`.

## Recovered custom UI areas

Recovered and preserved in the repo:

- runtime header
- retry banner
- connection badge
- config export/import

(the `Automation & Insights` panel was recovered too, then replaced by the Dashboard tab — see above)

## Legacy SPIFFS size budget matters

The controller SPIFFS partition is only `32 KB` (about 24 KB usable once SPIFFS keeps its two spare blocks), shared by `app.html` and `config.json`, so UI size is part of the design. `update_htmlapp.sh` prints the gzip sizes; as of 2026-09-06 they are ~12.6 KB + ~6.3 KB.

`update_htmlapp.sh` minifies the JS with `terser` (`npm install -g terser`) as one bundle with top-level mangling, and strips CSS whitespace, before rendering; without terser the page still renders but gzips ~3 KB larger. `SGOS_HTML_MINIFY=0` skips minification for debugging.

Measured limit (2026-09-07, config.json 6.3 KB gz): `app.html` at **12.6 KB gz fits, 13.5 KB does not** — the second upload still answered `200` but the file was silently truncated. Three guards now exist:

1. `update_htmlapp.sh` fails at render time when `app.html` + `config.json` exceed `SGOS_SPIFFS_BUDGET` (default 19000 bytes gzip)
2. `upload_htmlapp.sh` checks the same budget **before deleting anything** on the controller — or the real free space when the firmware reports `fs_used`/`fs_total` in `/mqttdiag` — and, after uploading, reads both files back and compares them byte for byte
3. firmware (`httpd_fs.c`, from commit after `3804e84`): the upload handler refuses a file that does not fit (`Not enough space on storage`, HTTP 500), writes unbuffered so a short `fwrite` is caught per chunk, and treats a failed `fclose` as a failed upload (file removed) instead of answering 200

The current build is ~11.9 KB gz. With the firmware from 2026-09-07 `/mqttdiag` reports the real figures: `fs_total` = 22 841 bytes usable, `fs_used` = 19 076 with the 11.9 KB + 6.3 KB payload (≈870 bytes of SPIFFS index overhead), i.e. ~3.7 KB free — hence the 19 000-byte budget.

Earlier discovery: `style.custom.css` once contained a full second copy of `normalize.css` next to `normalize.min.css`; removing that duplication was required to make the compressed app fit.

## Uploading the UI

Use:

```bash
bash ./upload_htmlapp.sh <controller-ip> ./spiffs_fs
```

The upload script uses gzip flags compatible with the legacy controller workflow.

Current behavior:

- it prefers `zopfli` when available
- otherwise it falls back to deterministic `gzip -9 -n`

This is not an optimization gimmick here; it directly affects whether the UI fits the controller.

## Local testing

`python mock_server.py --port 8080 --root spiffs_fs` serves the rendered UI with a fake box, `/mqttdiag` and writable `/i`/`/s` keys, so the Dashboard can be checked at `http://127.0.0.1:8080/app.html` without touching the controller (see `README_TEST.md`).

## Browser cache gotcha

When testing UI changes on the live controller, stale browser cache can masquerade as a failed upload.

This repo now mitigates that in firmware by serving SPIFFS files with no-cache headers, but if behavior still looks old:

- use a hard refresh
- or use a private/incognito window

Important legacy controller nuance:

- `/fs/app.html?<anything>` is not supported by the old file-serving route
- adding a query string can return `404`
- use plain `/fs/app.html`

## Rule for future changes

If a UI change matters, keep it in repo source under `html_app/` so it survives:

- OTA cycles
- SPIFFS wipes
- controller replacement
- team handoff

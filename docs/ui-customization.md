# UI Customization

This project has two different UI layers that are easy to confuse.

## Source of truth

Edit UI source here:

- `html_app/index.html`
- `html_app/onload.custom.js`
- `html_app/style.custom.css`

Do not treat `spiffs_fs/app.html` as the long-term source of truth.

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
- `Automation & Insights`
- `Sensor Health`
- `Energy Optimization`
- `Dynamic Climate Setpoint`
- `History & KPI`

## Legacy SPIFFS size budget matters

The controller SPIFFS partition is only `32 KB`, so UI size is part of the design.

Important discovery:

- `html_app/style.custom.css` accidentally contained a full second copy of `normalize.css`
- `html_app/index.html` already included `normalize.min.css`

Removing that duplication was required to make the compressed app fit again.

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

## Current sensor health UI bridge

The live UI includes a very small read-only debug bridge:

- `html_app/sensor-health.custom.js`

It updates `sensor_health_summary` by polling:

- `sensor_health_status`
- `sensor_health_last_alert`
- `sensor_health_period_s`
- `sensor_health_stuck_samples`

It is intentionally tiny so it stays deployable on the legacy SPIFFS layout.

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

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

## Uploading the UI

Use:

```bash
bash ./upload_htmlapp.sh <controller-ip> ./spiffs_fs
```

The upload script uses gzip flags compatible with the legacy controller workflow.

## Rule for future changes

If a UI change matters, keep it in repo source under `html_app/` so it survives:

- OTA cycles
- SPIFFS wipes
- controller replacement
- team handoff

# Documentation

This folder is the lightweight documentation home for `SuperGreenOS`.

It is intentionally plain Markdown so it works well:

- directly in GitHub
- inside the repository during development
- as a future source for a docs site such as Mintlify or MkDocs

## Start Here

- [Environment Setup](./environment-setup.md)
- [Build And OTA](./build-and-ota.md)
- [UI Customization](./ui-customization.md)
- [Backend Roadmap](./backend-roadmap.md)
- [Config Recovery](./config-recovery.md)

Current notable validated topics:

- modern Ubuntu build flow for ESP-IDF `3.3.1`
- canonical `update_config.sh -> update_templates.sh -> update_htmlapp.sh` generation order
- maintenance OTA plus mandatory UI restore
- fallback AP recovery after maintenance OTA
- reboot-loop postmortem and NVS reset behavior on the legacy controller
- SPIFFS size limits on the legacy controller
- first deployed backend module: `sensor_health`
- relationship between this repo and `SuperGreenOSBoilerplate`
- config recovery after NVS erase — exported JSON as recovery source, LED/timer chain, motor brownout investigation

## Suggested rule

Keep:

- short onboarding in the root `README.md`
- repo-specific operational notes in `Agents.md`
- longer task-oriented guides in this `docs/` folder

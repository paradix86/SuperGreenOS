# Docs and Codex config refresh summary

This bundle contains:

- `AGENTS.md` updated with current safety rules, build flow, frozen runbooks, and known blocker candidates
- `.codex/config.toml` with conservative defaults suitable for this repo
- `.codex/agents/*.toml` for OTA/MQTT/packaging/docs/fix specialist agents
- `docs/build-and-ota.md` updated to reflect the current packaging workaround and the required generation order
- `docs/ota-delivery-only.md` added to separate firmware-delivery proof from MQTT validation
- `docs/live-experiment-emergency-recovery.md` refreshed with the local-preflight caveat
- `docs/README.md` updated to point to the new operational split

These files are offline-only artifacts and do not touch the controller.

# Live Experiment Emergency Recovery (Canonical Abort Path)

This runbook is mandatory for any live controller experiment that causes:

- instability
- reboot loops
- broker-switch failure
- uncertain runtime state

Primary goals:

- keep `BOX_0` running for the plants
- stop MQTT-triggered reboot loops immediately
- restore a stable and reachable controller state

Important rule on unstable diagnostic firmware:

- use `mqtt://192.168.1.1:9999` first as the emergency broker target
- do **not** use `sink2` as the immediate emergency target

---

## Canonical emergency sequence

Apply all POST writes with retry loops:

1. `POST /i?k=BOX_0_ENABLED&v=1`
2. `POST /s?k=BROKER_URL&v=mqtt%3A%2F%2F192.168.1.1%3A9999`
3. `POST /s?k=BROKER_CLIENTID&v=304a4fd6eb4c`
4. `POST /i?k=REBOOT&v=1`
5. Verify repeatedly until stable:
   - `BROKER_URL = mqtt://192.168.1.1:9999`
   - `BROKER_CLIENTID = 304a4fd6eb4c`
   - `STATE = 2`
   - `WIFI_STATUS = 3`
   - `BOX_0_ENABLED = 1`
   - `N_RESTARTS` stable over repeated checks
   - `LED_0_DUTY` and `BOX_0_BLOWER_DUTY` are logged/reported (not hard-fail gates)

---

## Canonical script

```bash
cd /home/alan/sources/SuperGreenOS
bash ./scripts/emergency_recover_controller.sh 192.168.1.104
```

Behavior:

- applies the canonical emergency sequence with retries
- reboots as needed
- keeps verifying all required runtime values
- exits only after stability checks pass

---

## Next-step rule after emergency stabilization

After emergency recovery is successful, decide next steps separately.
Do not resume live experiment steps until stability is confirmed.

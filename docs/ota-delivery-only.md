# OTA Delivery Only

This runbook is intentionally limited to **proving firmware delivery**.

It does **not** validate MQTT behavior.

## Goal

Prove all of the following:

1. The OTA server is serving the freshly built package.
2. The controller fetches:
   - `/SuperGreenMaintenance/last_timestamp`
   - `/SuperGreenMaintenance/<TS>/firmware.bin`
3. Both fetches return `200`.
4. The controller comes back.
5. `OTA_TIMESTAMP == <TS>`.

Only after those facts are true does `/mqttdiag` become meaningful.

## Preflight

- Keep the controller in a stable state before starting.
- Do **not** change broker/client settings for this phase.
- Emergency command must be ready:

```bash
cd /home/alan/sources/SuperGreenOS
bash ./scripts/emergency_recover_controller.sh 192.168.1.104
```

## OTA server preflight

```bash
cd /home/alan/sources/SuperGreenOS
DIR=/tmp/ota-delivery-<TS>
mkdir -p "$DIR"
: > "$DIR/ota_http.log"

nohup python3 -m http.server 8091 --bind 0.0.0.0 --directory releases > "$DIR/ota_http.log" 2>&1 &
PID=$!
echo "$PID" > "$DIR/ota_http.pid"

READY=0
for i in $(seq 1 20); do
  if ps -p "$PID" >/dev/null 2>&1     && ss -ltnp | rg -q ':8091'     && curl -fsS "http://127.0.0.1:8091/SuperGreenMaintenance/last_timestamp" > "$DIR/served_last_timestamp.txt"     && curl -fsS -o /dev/null "http://127.0.0.1:8091/SuperGreenMaintenance/<TS>/firmware.bin"
  then
    READY=1
    break
  fi
  sleep 1
done

[ "$READY" -eq 1 ] || exit 1
```

## Controller OTA settings

```bash
CTRL=192.168.1.104
HOST=192.168.1.151

curl -fsS -X POST "http://$CTRL/s?k=OTA_SERVER_IP&v=$HOST"
curl -fsS -X POST "http://$CTRL/s?k=OTA_SERVER_HOSTNAME&v=$HOST"
curl -fsS -X POST "http://$CTRL/i?k=OTA_SERVER_PORT&v=8091"
curl -fsS -X POST "http://$CTRL/s?k=OTA_BASEDIR&v=%2FSuperGreenMaintenance"
```

## Trigger and proof

1. Trigger:

```bash
curl -fsS -X POST "http://$CTRL/i?k=OTA_START&v=0"
curl -fsS -X POST "http://$CTRL/i?k=OTA_START&v=1"
```

2. Require OTA server log evidence:

- `GET /SuperGreenMaintenance/last_timestamp ... 200`
- `GET /SuperGreenMaintenance/<TS>/firmware.bin ... 200`
- requests must come from `192.168.1.104`

3. Poll reachability up to 180s.
4. Read `OTA_TIMESTAMP`.

## Abort rules

Abort immediately if:
- server preflight fails
- no fetch of `last_timestamp` within 60s
- no fetch of `firmware.bin` within 60s after `last_timestamp`
- controller does not return within 180s
- `OTA_TIMESTAMP != <TS>` after return

Abort path:

```bash
cd /home/alan/sources/SuperGreenOS
bash ./scripts/emergency_recover_controller.sh 192.168.1.104
```

#!/usr/bin/env bash
# verify_restore.sh — Read back restored config + computed outputs
#
# ORIGIN: created during the 2026-03-15 BOX_0 NVS-erase recovery workflow.
# VALUES: controller-specific (192.168.1.104, expected values match the
#         2026-03-15 app export for this controller). Review expected values
#         before using on a different controller or after a config change.
# HOST:   declared at the top of this script — must be reviewed before use.
# PHASE 2 motor SOURCE keys: informational READ only (no expected value asserted).
#
# check KEY EXPECTED  → prints OK or FAIL
# check KEY           → prints READ (informational, no assertion)
#
# GET /i response format confirmed from httpd.c:141-144:
#   snprintf(ret, sizeof(ret)-1, "%d", v);
#   httpd_resp_send(req, ret, strlen(ret));
# Response body is a raw decimal integer ONLY. No "v=" prefix.
#
# Run at least 15s after restore (blower_task cycles every 10s).

set -euo pipefail

HOST="http://192.168.1.104"
FAIL=0

# check KEY [EXPECTED]
# With EXPECTED: prints OK or FAIL. Without: prints READ (informational only).
check() {
    local key="$1"
    local expected="${2:-}"
    local live

    live=$(curl -fsS "${HOST}/i?k=${key}" 2>&1) || {
        printf "FAIL  %-34s  HTTP error\n" "$key"
        FAIL=$((FAIL + 1))
        return
    }

    if [ -n "$expected" ]; then
        if [ "$live" = "$expected" ]; then
            printf "OK    %-34s  %s\n" "$key" "$live"
        else
            printf "FAIL  %-34s  live=%-6s  expected=%s\n" "$key" "$live" "$expected"
            FAIL=$((FAIL + 1))
        fi
    else
        printf "READ  %-34s  %s\n" "$key" "$live"
    fi
}

echo "=== SuperGreenOS Verify Restore ==="
echo "Target: ${HOST}"
echo "Time:   $(date)"
echo ""

echo "--- Core state ---"
check STATE            2
check BOX_0_ENABLED    1
check BOX_0_TIMER_TYPE 1

echo "--- Lighting mapping ---"
check LED_0_BOX  0
check LED_1_BOX  0
check LED_2_BOX  0
check LED_3_BOX  0
check LED_4_BOX  0
check LED_5_BOX  0
check LED_0_TYPE 0
check LED_1_TYPE 0
check LED_2_TYPE 0
check LED_3_TYPE 0
check LED_4_TYPE 0
check LED_5_TYPE 0

echo "--- Schedule ---"
check BOX_0_ON_HOUR  5
check BOX_0_ON_MIN   0
check BOX_0_OFF_HOUR 23
check BOX_0_OFF_MIN  0

echo "--- Fan config ---"
check BOX_0_FAN_MIN        20
check BOX_0_FAN_MAX        30
check BOX_0_FAN_REF_MIN    0
check BOX_0_FAN_REF_MAX    100
check BOX_0_FAN_REF_SOURCE 8

echo "--- Blower config ---"
check BOX_0_BLOWER_MIN        15
check BOX_0_BLOWER_MAX        60
check BOX_0_BLOWER_REF_MIN    55
check BOX_0_BLOWER_REF_MAX    70
check BOX_0_BLOWER_REF_SOURCE 15

echo "--- Motor MIN/MAX (Phase 1) ---"
check MOTOR_0_MIN 8
check MOTOR_0_MAX 51
check MOTOR_1_MIN 8
check MOTOR_1_MAX 50
check MOTOR_2_MIN 8
check MOTOR_2_MAX 50

echo "--- Motor SOURCE + CURVE (READ only — Phase 2 deferred) ---"
# Firmware defaults after NVS erase: MOTORS_CURVE=1, MOTOR_0_SOURCE=1,
# MOTOR_1_SOURCE=2, MOTOR_2_SOURCE=3
check MOTORS_CURVE
check MOTOR_0_SOURCE
check MOTOR_1_SOURCE
check MOTOR_2_SOURCE

echo ""
echo "--- Computed outputs (READ only — informational) ---"
# BOX_0_TIMER_OUTPUT: 100 if current time is 05:00-23:00; 0 otherwise
check BOX_0_TIMER_OUTPUT
# LED duties: 100 during lit hours if LED_N_BOX=0 and TIMER_OUTPUT=100
check LED_0_DUTY
check LED_1_DUTY
check LED_2_DUTY
check LED_3_DUTY
check LED_4_DUTY
check LED_5_DUTY
# Blower/fan: non-zero expected after blower_task cycles (wait >=15s)
check BOX_0_BLOWER_DUTY
check BOX_0_FAN_DUTY

echo ""
if [ "$FAIL" -eq 0 ]; then
    echo "=== Verification passed ==="
else
    echo "=== ${FAIL} check(s) failed ==="
    exit 1
fi

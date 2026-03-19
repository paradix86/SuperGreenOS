#!/usr/bin/env bash
# compare_and_restore.sh — Diff-only config replay for SuperGreenOS
#
# ORIGIN: created during the 2026-03-15 BOX_0 NVS-erase recovery workflow.
# VALUES: controller-specific (192.168.1.104, 18h photoperiod, fan/blower/motor
#         bounds from the 2026-03-15 app export). Review ALL hardcoded values
#         before using on a different controller or after a config change.
# HOST:   declared at the top of this script — must be reviewed before use.
# PHASE 2 (motor SOURCE + MOTORS_CURVE): printed as manual commands only.
#         Do NOT execute Phase 2 without physical verification of each motor.
#
# Confirmed: GET /i returns raw decimal integer (httpd.c:141-144), e.g. "2"
# Confirmed: POST /i returns "OK" on success, HTTP 404 if key is not writable
#
# PHASE 1 only — motor SOURCE and MOTORS_CURVE are deferred.
# Read the PHASE 2 block at the bottom before running manually.

set -euo pipefail

HOST="http://192.168.1.104"
CHANGED=0
ERRORS=0

# Returns raw integer body from GET /i, e.g.: 2  or  -1  or  100
get_live() {
    curl -fsS "${HOST}/i?k=${1}"
}

# Reads live value; POSTs only if it differs from desired.
set_if_changed() {
    local key="$1"
    local desired="$2"
    local live resp

    live=$(get_live "$key") || {
        echo "ERROR  GET ${key} failed"
        ERRORS=$((ERRORS + 1))
        return
    }

    if [ "$live" = "$desired" ]; then
        echo "SKIP   ${key}=${live}"
        return
    fi

    resp=$(curl -fsS -X POST "${HOST}/i?k=${key}&v=${desired}" 2>&1) || {
        echo "ERROR  POST ${key}=${desired}  (live=${live})  resp=${resp}"
        ERRORS=$((ERRORS + 1))
        return
    }

    echo "SET    ${key}  ${live} -> ${desired}  [${resp}]"
    CHANGED=$((CHANGED + 1))
}

echo "=== SuperGreenOS Diff-Only Restore — Phase 1 ==="
echo "Target: ${HOST}"
echo "Time:   $(date)"
echo ""

# ── Core state ────────────────────────────────────────────────────────────────
# All three already confirmed live. Expected to SKIP.
echo "--- Core state ---"
set_if_changed STATE            2
set_if_changed BOX_0_ENABLED    1
set_if_changed BOX_0_TIMER_TYPE 1

# ── Lighting mapping ──────────────────────────────────────────────────────────
# LED_N_BOX defaults to -1 after NVS erase (kv.c:448).
# Confirmed: mixer.c skips LEDs where get_led_box(i) != boxId.
# Already restored for 0-3 in previous session; 4-5 may still be at default.
echo "--- Lighting mapping ---"
set_if_changed LED_0_BOX  0
set_if_changed LED_1_BOX  0
set_if_changed LED_2_BOX  0
set_if_changed LED_3_BOX  0
set_if_changed LED_4_BOX  0
set_if_changed LED_5_BOX  0
set_if_changed LED_0_TYPE 0
set_if_changed LED_1_TYPE 0
set_if_changed LED_2_TYPE 0
set_if_changed LED_3_TYPE 0
set_if_changed LED_4_TYPE 0
set_if_changed LED_5_TYPE 0

# ── Schedule ──────────────────────────────────────────────────────────────────
# Lights on 05:00, off 23:00 (18h photoperiod from original config).
echo "--- Schedule ---"
set_if_changed BOX_0_ON_HOUR  5
set_if_changed BOX_0_ON_MIN   0
set_if_changed BOX_0_OFF_HOUR 23
set_if_changed BOX_0_OFF_MIN  0

# ── Fan ───────────────────────────────────────────────────────────────────────
# BOX_0_FAN_DUTY is read-only (setter=NULL, kv_mapping.c:1434). NOT replayed.
# BOX_0_FAN_REF  is read-only (setter=NULL, kv_mapping.c:1599). NOT replayed.
echo "--- Fan ---"
set_if_changed BOX_0_FAN_MIN        20
set_if_changed BOX_0_FAN_MAX        30
set_if_changed BOX_0_FAN_REF_MIN    0
set_if_changed BOX_0_FAN_REF_MAX    100
set_if_changed BOX_0_FAN_REF_SOURCE 8

# ── Blower ────────────────────────────────────────────────────────────────────
# BOX_0_BLOWER_DUTY is read-only (setter=NULL). NOT replayed.
# BOX_0_BLOWER_REF is read-only (setter=NULL, kv_mapping.c:1368). NOT replayed.
echo "--- Blower ---"
set_if_changed BOX_0_BLOWER_MIN        15
set_if_changed BOX_0_BLOWER_MAX        60
set_if_changed BOX_0_BLOWER_REF_MIN    55
set_if_changed BOX_0_BLOWER_REF_MAX    70
set_if_changed BOX_0_BLOWER_REF_SOURCE 15

# ── Motor MIN/MAX ─────────────────────────────────────────────────────────────
# Safe to replay: narrowing [0,100] to [8,51]/[8,50]/[8,50] only tightens bounds.
# MOTORS_CURVE and MOTOR_N_SOURCE are deferred to Phase 2 (see below).
echo "--- Motor MIN/MAX (Phase 1 only) ---"
set_if_changed MOTOR_0_MIN 8
set_if_changed MOTOR_0_MAX 51
set_if_changed MOTOR_1_MIN 8
set_if_changed MOTOR_1_MAX 50
set_if_changed MOTOR_2_MIN 8
set_if_changed MOTOR_2_MAX 50

echo ""
echo "=== Phase 1 complete — ${CHANGED} key(s) changed, ${ERRORS} error(s) ==="
echo ""
echo "─────────────────────────────────────────────────────────────────────────"
echo "PHASE 2 — Deferred motor keys (DO NOT RUN without physical verification)"
echo "─────────────────────────────────────────────────────────────────────────"
echo ""
echo "  MOTORS_CURVE=0  [live default=1, kv.c:828]"
echo "  With CURVE=1: duty = 8*pow(1.025, motor_duty)+5  (~15% at blower=9)"
echo "  With CURVE=0: duty applied directly               (8% at fan=8)"
echo "  Verify motors spin reliably at 8% duty before applying."
echo "  Command: curl -fsS -X POST '${HOST}/i?k=MOTORS_CURVE&v=0'"
echo ""
echo "  MOTOR_0_SOURCE=15  [live=1=blower -> JSON=15=fan]"
echo "  Command: curl -fsS -X POST '${HOST}/i?k=MOTOR_0_SOURCE&v=15'"
echo ""
echo "  MOTOR_1_SOURCE=15  [live=2=box1_blower(disabled=0%) -> ACTIVATES motor 1]"
echo "  Verify motor 1 is physically wired before applying."
echo "  Command: curl -fsS -X POST '${HOST}/i?k=MOTOR_1_SOURCE&v=15'"
echo ""
echo "  MOTOR_2_SOURCE=1   [live=3=box2_blower(disabled=0%) -> ACTIVATES motor 2]"
echo "  Verify motor 2 is physically wired before applying."
echo "  Command: curl -fsS -X POST '${HOST}/i?k=MOTOR_2_SOURCE&v=1'"

[ "$ERRORS" -gt 0 ] && exit 1
exit 0

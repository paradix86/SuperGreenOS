#!/usr/bin/env bash
set -euo pipefail

# Canonical emergency recovery script for unstable live experiments.
# Goal: keep BOX_0 active and stop MQTT-triggered reboot loops quickly.

CTRL_IP="${1:-192.168.1.104}"

TARGET_BROKER_URL="mqtt://192.168.1.1:9999"
TARGET_BROKER_URL_ENC="mqtt%3A%2F%2F192.168.1.1%3A9999"
TARGET_BROKER_CLIENTID="304a4fd6eb4c"
TARGET_STATE="2"
TARGET_BOX0_ENABLED="1"

MAX_SET_RETRIES="${MAX_SET_RETRIES:-20}"
MAX_READY_POLLS="${MAX_READY_POLLS:-30}"
MAX_STABILITY_ROUNDS="${MAX_STABILITY_ROUNDS:-30}"
STABLE_WINDOWS_REQUIRED="${STABLE_WINDOWS_REQUIRED:-2}"

log() {
  printf '[%s] %s\n' "$(date -Iseconds)" "$*"
}

get_i() {
  curl -s --max-time 4 "http://${CTRL_IP}/i?k=$1" || true
}

get_s() {
  curl -s --max-time 4 "http://${CTRL_IP}/s?k=$1" || true
}

post_best_effort() {
  local url="$1"
  curl -sS --max-time 5 -X POST "$url" >/dev/null || true
}

set_i_verify() {
  local key="$1"
  local val="$2"
  local url="http://${CTRL_IP}/i?k=${key}&v=${val}"
  local current=""

  for i in $(seq 1 "$MAX_SET_RETRIES"); do
    post_best_effort "$url"
    sleep 1
    current="$(get_i "$key")"
    log "VERIFY i:${key} attempt=${i} value='${current}' expected='${val}'"
    if [ "$current" = "$val" ]; then
      return 0
    fi
  done
  return 1
}

set_s_verify() {
  local key="$1"
  local post_val="$2"
  local expected="$3"
  local url="http://${CTRL_IP}/s?k=${key}&v=${post_val}"
  local current=""

  for i in $(seq 1 "$MAX_SET_RETRIES"); do
    post_best_effort "$url"
    sleep 1
    current="$(get_s "$key")"
    log "VERIFY s:${key} attempt=${i} value='${current}' expected='${expected}'"
    if [ "$current" = "$expected" ]; then
      return 0
    fi
  done
  return 1
}

reboot_retry() {
  local url="http://${CTRL_IP}/i?k=REBOOT&v=1"
  for i in $(seq 1 "$MAX_SET_RETRIES"); do
    if curl -sS --max-time 5 -X POST "$url" >/dev/null; then
      log "POST reboot attempt=${i} status=ok"
      return 0
    fi
    log "POST reboot attempt=${i} status=failed"
    sleep 1
  done
  return 1
}

check_required_values() {
  local state wifi box0 led0 blower0 cid broker
  state="$(get_i STATE)"
  wifi="$(get_i WIFI_STATUS)"
  box0="$(get_i BOX_0_ENABLED)"
  led0="$(get_i LED_0_DUTY)"
  blower0="$(get_i BOX_0_BLOWER_DUTY)"
  cid="$(get_s BROKER_CLIENTID)"
  broker="$(get_s BROKER_URL)"

  log "VALUES STATE=${state} WIFI_STATUS=${wifi} BROKER_CLIENTID=${cid} BROKER_URL=${broker} BOX_0_ENABLED=${box0} LED_0_DUTY=${led0} BOX_0_BLOWER_DUTY=${blower0}"

  [ "$state" = "$TARGET_STATE" ] \
    && [ "$wifi" = "3" ] \
    && [ "$cid" = "$TARGET_BROKER_CLIENTID" ] \
    && [ "$broker" = "$TARGET_BROKER_URL" ] \
    && [ "$box0" = "$TARGET_BOX0_ENABLED" ]
}

wait_until_ready() {
  for i in $(seq 1 "$MAX_READY_POLLS"); do
    if check_required_values; then
      log "READY check passed on poll ${i}"
      return 0
    fi
    sleep 2
  done
  return 1
}

check_nrestarts_stability() {
  local stable_windows=0
  local a b d

  for i in $(seq 1 "$MAX_STABILITY_ROUNDS"); do
    a="$(get_i N_RESTARTS)"
    sleep 20
    b="$(get_i N_RESTARTS)"

    if [[ "$a" =~ ^[0-9]+$ ]] && [[ "$b" =~ ^[0-9]+$ ]]; then
      d=$((b - a))
      log "N_RESTARTS window=${i} A=${a} B=${b} delta=${d}"
      if [ "$d" -eq 0 ]; then
        stable_windows=$((stable_windows + 1))
      else
        stable_windows=0
      fi
    else
      log "N_RESTARTS window=${i} A='${a}' B='${b}' (non-numeric)"
      stable_windows=0
    fi

    if ! check_required_values; then
      log "Required values changed during stability check."
      return 1
    fi

    if [ "$stable_windows" -ge "$STABLE_WINDOWS_REQUIRED" ]; then
      log "N_RESTARTS stability confirmed (${stable_windows} consecutive windows)."
      return 0
    fi
  done

  return 1
}

main() {
  local attempt=0

  log "Starting canonical emergency recovery for controller ${CTRL_IP}"
  log "Target broker=${TARGET_BROKER_URL}, client_id=${TARGET_BROKER_CLIENTID}, BOX_0_ENABLED=${TARGET_BOX0_ENABLED}"

  while true; do
    attempt=$((attempt + 1))
    log "=== RECOVERY ATTEMPT ${attempt} ==="

    if ! set_i_verify "BOX_0_ENABLED" "$TARGET_BOX0_ENABLED"; then
      log "BOX_0_ENABLED write/verify failed; retrying full recovery sequence."
      sleep 2
      continue
    fi

    if ! set_s_verify "BROKER_URL" "$TARGET_BROKER_URL_ENC" "$TARGET_BROKER_URL"; then
      log "BROKER_URL write/verify failed; retrying full recovery sequence."
      sleep 2
      continue
    fi

    if ! set_s_verify "BROKER_CLIENTID" "$TARGET_BROKER_CLIENTID" "$TARGET_BROKER_CLIENTID"; then
      log "BROKER_CLIENTID write/verify failed; retrying full recovery sequence."
      sleep 2
      continue
    fi

    if ! reboot_retry; then
      log "REBOOT write failed; retrying full recovery sequence."
      sleep 2
      continue
    fi

    # Give the controller time to reboot and reconnect.
    sleep 25

    if ! wait_until_ready; then
      log "Controller not ready yet; retrying full recovery sequence."
      sleep 5
      continue
    fi

    if check_nrestarts_stability; then
      log "EMERGENCY RECOVERY SUCCESS"
      exit 0
    fi

    log "Controller still unstable; retrying full recovery sequence."
    sleep 5
  done
}

main "$@"

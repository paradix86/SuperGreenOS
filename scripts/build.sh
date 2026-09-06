#!/usr/bin/env bash
# One-shot reproducible firmware build for SuperGreenOS (ESP-IDF 3.3.x, legacy make).
#
# Encodes the mandatory generation order:
#   1. CUE  -> config.<variant>.json      (update_config.sh, needs cue 0.0.8)
#   2. JSON -> generated C / mk / Kconfig (update_templates.sh, needs ejs-cli)
#   3. JSON -> spiffs_fs/app.html + config.json (update_htmlapp.sh)
#   4. make defconfig && make -jN
#
# Usage:
#   scripts/build.sh                 # Controller v2.1, full regeneration
#   scripts/build.sh --skip-config   # reuse the committed config JSON (no cue needed)
#   scripts/build.sh --gen-only      # steps 1-3 only, no compilation
#
# Environment overrides:
#   IDF_PATH          ESP-IDF checkout            (default: $HOME/esp/esp-idf_release_3.3.1)
#   SGOS_TARGET       CUE target dir              (default: Controller/v2.1)
#   SGOS_CONFIG_JSON  output/input config JSON    (default: config.controller.json)
#   SGOS_BUILD_DIR    make BUILD_DIR_BASE         (default: ./build; use a native path on WSL for speed)
#   JOBS              parallel jobs               (default: nproc)

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd -P)"
cd "$ROOT"

SKIP_CONFIG=0
GEN_ONLY=0
for arg in "$@"; do
  case "$arg" in
    --skip-config) SKIP_CONFIG=1 ;;
    --gen-only) GEN_ONLY=1 ;;
    -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
    *) echo "Unknown option: $arg" >&2; exit 2 ;;
  esac
done

IDF_PATH="${IDF_PATH:-$HOME/esp/esp-idf_release_3.3.1}"
SGOS_TARGET="${SGOS_TARGET:-Controller/v2.1}"
SGOS_CONFIG_JSON="${SGOS_CONFIG_JSON:-config.controller.json}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
CUE_DIR="config_gen/config/SuperGreenOS/Controllers/$SGOS_TARGET"

# cue installed via `go install` lands in $HOME/go/bin
export PATH="$PATH:$HOME/go/bin:/usr/local/bin:$HOME/esp/cue"

log() { printf '\n==> %s\n' "$*"; }

if [ "$SKIP_CONFIG" -eq 0 ]; then
  log "1/4 CUE -> $SGOS_CONFIG_JSON ($CUE_DIR)"
  bash ./update_config.sh "$CUE_DIR" "$SGOS_CONFIG_JSON"
else
  log "1/4 skipped (using committed $SGOS_CONFIG_JSON)"
fi

log "2/4 templates -> generated sources"
bash ./update_templates.sh "$SGOS_CONFIG_JSON"

log "3/4 html_app -> spiffs_fs"
bash ./update_htmlapp.sh "$SGOS_CONFIG_JSON"

for f in main/init.c main/component.mk main/core/modules.h main/core/kv/kv.c main/core/kv/kv_helpers.c; do
  if [ ! -s "$f" ]; then
    echo "ERROR: generated file missing or empty: $f" >&2
    exit 1
  fi
done

if [ "$GEN_ONLY" -eq 1 ]; then
  log "generation done (--gen-only)"
  exit 0
fi

if [ ! -f "$IDF_PATH/export.sh" ]; then
  echo "ERROR: ESP-IDF not found at IDF_PATH=$IDF_PATH (run setup/setup_supergreenos_build_env.sh)" >&2
  exit 1
fi

log "4/4 make (IDF_PATH=$IDF_PATH, jobs=$JOBS)"
export IDF_PATH
set +u
# shellcheck disable=SC1090
source "$IDF_PATH/export.sh" >/dev/null
set -u

MAKE_ARGS=()
if [ -n "${SGOS_BUILD_DIR:-}" ]; then
  mkdir -p "$SGOS_BUILD_DIR"
  MAKE_ARGS+=("BUILD_DIR_BASE=$SGOS_BUILD_DIR")
fi

make "${MAKE_ARGS[@]}" defconfig
make "${MAKE_ARGS[@]}" -j"$JOBS" all

BIN="${SGOS_BUILD_DIR:-build}/firmware.bin"
log "done: $BIN ($(wc -c < "$BIN") bytes), source $(git describe --always --dirty 2>/dev/null || echo unknown)"

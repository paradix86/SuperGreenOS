#!/usr/bin/env bash
set -euo pipefail

# Package an OTA artifact: releases/<basedir>/<TS>/firmware.bin + last_timestamp.
#
# Environment:
#   SGOS_SPIFFS_FORMAT_ONCE  1 (default): maintenance build, formats SPIFFS once at
#                            boot (the web UI must be re-uploaded afterwards).
#                            0: plain firmware update, SPIFFS and UI untouched.
#   SGOS_OTA_BASEDIR         directory under releases/ (default: SuperGreenMaintenance)
#   SGOS_BUILD_DIR           make BUILD_DIR_BASE (default: ./build; use a native
#                            path on WSL for speed, same as scripts/build.sh)
#   JOBS                     parallel jobs (default: nproc)
#
# The generated sources must already be up to date (run scripts/build.sh first).

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd -P)"
cd "$ROOT_DIR"

SPIFFS_FORMAT_ONCE="${SGOS_SPIFFS_FORMAT_ONCE:-1}"
OTA_BASEDIR="${SGOS_OTA_BASEDIR:-SuperGreenMaintenance}"
BUILD_DIR="${SGOS_BUILD_DIR:-$ROOT_DIR/build}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

echo "Checking repository state..."
git rev-parse HEAD
git status --short
git diff --stat

if [[ -n $(git status --porcelain) ]]; then
  echo ""
  echo "ERROR: Git worktree is dirty. Commit or stash changes before building."
  exit 1
fi

HTTPD_FS_FILE="main/core/httpd/httpd_fs.c"
OTA_H_FILE="main/core/ota/ota.h"

restore_sources() {
  git checkout --quiet -- "$HTTPD_FS_FILE" "$OTA_H_FILE" || true
}
trap restore_sources EXIT

case "$SPIFFS_FORMAT_ONCE" in
  0|1) ;;
  *) echo "ERROR: SGOS_SPIFFS_FORMAT_ONCE must be 0 or 1"; exit 1 ;;
esac

TS="$(date +%s)"
sed -i -E "s/^#define SGL_FORCE_SPIFFS_FORMAT_ONCE .*/#define SGL_FORCE_SPIFFS_FORMAT_ONCE ${SPIFFS_FORMAT_ONCE}/" "$HTTPD_FS_FILE"
sed -i -E "s/^#define OTA_BUILD_TIMESTAMP .*/#define OTA_BUILD_TIMESTAMP ${TS}/" "$OTA_H_FILE"
grep -q "^#define SGL_FORCE_SPIFFS_FORMAT_ONCE ${SPIFFS_FORMAT_ONCE}$" "$HTTPD_FS_FILE"
grep -q "^#define OTA_BUILD_TIMESTAMP ${TS}$" "$OTA_H_FILE"

echo "Building OTA_BUILD_TIMESTAMP=$TS SPIFFS_FORMAT_ONCE=$SPIFFS_FORMAT_ONCE (BUILD_DIR_BASE=$BUILD_DIR, jobs=$JOBS)"
make BUILD_DIR_BASE="$BUILD_DIR" -j"$JOBS" all

DEST="releases/${OTA_BASEDIR}/${TS}"
mkdir -p "$DEST"
cp "$BUILD_DIR/firmware.bin" "$DEST/firmware.bin"
echo "$TS" > "releases/${OTA_BASEDIR}/last_timestamp"
git rev-parse HEAD > "$DEST/git_commit.txt"
(cd "$DEST" && sha256sum firmware.bin > firmware.bin.sha256)

echo "OTA firmware ready:"
echo "  TS=$TS"
echo "  BIN=$DEST/firmware.bin ($(stat -c %s "$DEST/firmware.bin") bytes)"
echo "  SHA256=$(cut -d' ' -f1 "$DEST/firmware.bin.sha256")"
echo "  LAST_TS=releases/${OTA_BASEDIR}/last_timestamp"
echo "  SPIFFS_FORMAT_ONCE=$SPIFFS_FORMAT_ONCE"

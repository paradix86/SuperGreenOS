#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd -P)"
cd "$ROOT_DIR"

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
  sed -i -E 's/^#define SGL_FORCE_SPIFFS_FORMAT_ONCE .*/#define SGL_FORCE_SPIFFS_FORMAT_ONCE 0/' "$HTTPD_FS_FILE" || true
  sed -i -E 's/^#define OTA_BUILD_TIMESTAMP .*/#define OTA_BUILD_TIMESTAMP 0/' "$OTA_H_FILE" || true
}
trap restore_sources EXIT

sed -i -E 's/^#define SGL_FORCE_SPIFFS_FORMAT_ONCE .*/#define SGL_FORCE_SPIFFS_FORMAT_ONCE 1/' "$HTTPD_FS_FILE"
TS="$(date +%s)"
sed -i -E "s/^#define OTA_BUILD_TIMESTAMP .*/#define OTA_BUILD_TIMESTAMP ${TS}/" "$OTA_H_FILE"

make -j4

DEST="releases/SuperGreenMaintenance/${TS}"
mkdir -p "$DEST"
cp build/firmware.bin "$DEST/firmware.bin"
echo "$TS" > releases/SuperGreenMaintenance/last_timestamp

echo "Maintenance firmware ready:"
echo "  TS=$TS"
echo "  BIN=$DEST/firmware.bin"
echo "  LAST_TS=releases/SuperGreenMaintenance/last_timestamp"

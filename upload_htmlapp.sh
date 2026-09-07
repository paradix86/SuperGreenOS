#!/bin/bash
set -euo pipefail

NAME=''
HTML_APP_DIR=''

if [ "$#" -eq 2 ]; then
  NAME=$1
  HTML_APP_DIR=$2
else
  echo "Usage: $(basename $BASH_SOURCE) controller.local path/to/html_app"
  exit 1
fi

CONFIG_FILE="$HTML_APP_DIR/config.json"
APP_FILE="$HTML_APP_DIR/app.html"

if [ ! -f "$CONFIG_FILE" ] || [ ! -f "$APP_FILE" ]; then
  echo "Missing input files in $HTML_APP_DIR (expected config.json and app.html)"
  exit 1
fi

TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

compress_file() {
  local input=$1
  local output=$2
  local zopfli_python="${SGL_GZIP_PYTHON:-}"

  if [ -z "$zopfli_python" ] && [ -x /tmp/zopfli-venv/bin/python ]; then
    zopfli_python=/tmp/zopfli-venv/bin/python
  fi

  if [ -n "$zopfli_python" ] && "$zopfli_python" - <<'PY' >/dev/null 2>&1
import zopfli.gzip
PY
  then
    "$zopfli_python" - "$input" "$output" <<'PY'
import pathlib
import sys
import zopfli.gzip

src = pathlib.Path(sys.argv[1])
dst = pathlib.Path(sys.argv[2])
dst.write_bytes(zopfli.gzip.compress(src.read_bytes()))
PY
  else
    # Use the tightest deterministic gzip stream we can to stay within the
    # controller's legacy upload and SPIFFS limits.
    gzip -9 -n -c "$input" > "$output"
  fi
}

compress_file "$CONFIG_FILE" "$TMP_DIR/config.json"
compress_file "$APP_FILE" "$TMP_DIR/app.html"

# Size guard, before anything on the controller is touched. The 32 KB SPIFFS
# partition holds about 24 KB of pages minus per-file index pages and the
# blocks SPIFFS keeps for garbage collection; measured on 2026-09-07:
# 12.6 KB + 6.3 KB fits, 13.5 KB + 6.3 KB does not (config.json came back
# truncated with a 200). Newer firmwares report fs_used/fs_total in /mqttdiag,
# in which case the real free space (after deleting the old files) is used.
BUDGET="${SGOS_SPIFFS_BUDGET:-19000}"
SLACK=2048
app_gz=$(wc -c < "$TMP_DIR/app.html")
cfg_gz=$(wc -c < "$TMP_DIR/config.json")
need=$((app_gz + cfg_gz))
diag="$(curl -sS --max-time 10 "http://$NAME/mqttdiag" 2>/dev/null || true)"
fs_total="$(printf '%s' "$diag" | sed -n 's/.*"fs_total":\([0-9]*\).*/\1/p')"
fs_used="$(printf '%s' "$diag" | sed -n 's/.*"fs_used":\([0-9]*\).*/\1/p')"
old_app="$(curl -sS --max-time 10 -o /dev/null -w '%{size_download}' "http://$NAME/fs/app.html" 2>/dev/null || echo 0)"
old_cfg="$(curl -sS --max-time 10 -o /dev/null -w '%{size_download}' "http://$NAME/fs/config.json" 2>/dev/null || echo 0)"
if [ -n "$fs_total" ] && [ "$fs_total" -gt 0 ]; then
  free_after=$((fs_total - fs_used + old_app + old_cfg))
  echo "SPIFFS: total=$fs_total used=$fs_used, ~$free_after free after deleting the old files; need $need + $SLACK slack"
  if [ $((need + SLACK)) -gt "$free_after" ]; then
    echo "ERROR: app.html ($app_gz) + config.json ($cfg_gz) do not fit, nothing uploaded" >&2
    exit 1
  fi
else
  echo "Payload: app.html $app_gz + config.json $cfg_gz = $need bytes gz (budget $BUDGET)"
  if [ "$need" -gt "$BUDGET" ]; then
    echo "ERROR: payload exceeds the SPIFFS budget of $BUDGET bytes, nothing uploaded" >&2
    echo "       shrink app.html (docs/ui-customization.md) or override SGOS_SPIFFS_BUDGET at your own risk" >&2
    exit 1
  fi
fi

# On the legacy controller SPIFFS, restore is most reliable if we clear the
# old payload first and then upload app.html before config.json.
curl -sS -X DELETE "http://$NAME/fs/app.html" >/dev/null || true
curl -sS -X DELETE "http://$NAME/fs/config.json" >/dev/null || true

curl --fail -XPOST --upload-file "$TMP_DIR/app.html" -vvv "http://$NAME/fs/app.html"
curl --fail -XPOST --upload-file "$TMP_DIR/config.json" -vvv "http://$NAME/fs/config.json"

# SPIFFS reports 200 even when it ran out of space halfway through a file
# (seen 2026-09-07 with a 13.5 KB app.html: config.json came back truncated).
# Read both files back and make sure the gzip streams are complete.
for f in app.html config.json; do
  curl -sS --fail -o "$TMP_DIR/$f.check" "http://$NAME/fs/$f"
  if ! cmp -s "$TMP_DIR/$f" "$TMP_DIR/$f.check"; then
    echo "ERROR: $f read back from the controller differs from the upload (SPIFFS full?)" >&2
    echo "       shrink app.html (see docs/ui-customization.md) and upload again" >&2
    exit 1
  fi
done
echo "Verified: app.html and config.json read back intact from $NAME"

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

curl --fail -XPOST --upload-file "$TMP_DIR/config.json" -vvv "http://$NAME/fs/config.json"
curl --fail -XPOST --upload-file "$TMP_DIR/app.html" -vvv "http://$NAME/fs/app.html"

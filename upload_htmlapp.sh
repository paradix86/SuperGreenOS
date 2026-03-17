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

# Use the tightest deterministic gzip stream we can to stay within the
# controller's legacy upload and SPIFFS limits.
gzip -9 -n -c "$CONFIG_FILE" > "$TMP_DIR/config.json"
gzip -9 -n -c "$APP_FILE" > "$TMP_DIR/app.html"

curl --fail -XPOST --upload-file "$TMP_DIR/config.json" -vvv "http://$NAME/fs/config.json"
curl --fail -XPOST --upload-file "$TMP_DIR/app.html" -vvv "http://$NAME/fs/app.html"

#!/bin/bash

# Copyright (C) 2019  SuperGreenLab <towelie@supergreenlab.com>
# Author: Constantin Clauzel <constantin.clauzel@gmail.com>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

# Renders html_app/ into spiffs_fs/app.html and spiffs_fs/config.json.
# The SPIFFS partition is 32 KB, so the gzip sizes printed at the end are the
# numbers that matter for deployment.

set -euo pipefail

DATA="${1:-}"

if [ -z "$DATA" ] || [ ! -f "$DATA" ]; then
  echo "USAGE: $0 /path/to/config.json" >&2
  exit 1
fi

if ! command -v ejs-cli >/dev/null 2>&1; then
  echo "ERROR: ejs-cli not found in PATH (install with: npm install -g ejs-cli)" >&2
  exit 1
fi

ROOT="$(cd "$(dirname "$0")" && pwd)"
DATA_ABS="$(cd "$(dirname "$DATA")" && pwd)/$(basename "$DATA")"
OUT_DIR="$ROOT/spiffs_fs"
mkdir -p "$OUT_DIR"

# html_app/ holds readable sources; the *.js files are minified with terser
# (npm install -g terser) into a scratch copy before rendering, because the
# gzip size decides whether app.html still fits next to config.json on the
# 32 KB SPIFFS partition. SGOS_HTML_MINIFY=0 renders the sources verbatim.
SRC_DIR="$ROOT/html_app"
if [ "${SGOS_HTML_MINIFY:-1}" != "0" ] && command -v terser >/dev/null 2>&1; then
  SRC_DIR="$(mktemp -d)"
  trap 'rm -rf "$SRC_DIR"' EXIT
  cp "$ROOT"/html_app/* "$SRC_DIR/"
  # The three scripts share one global scope in the page, so minify them as
  # one bundle (top-level names mangled too): index.html keeps including
  # utils.js / onload.js / dashboard.js, the last two are just empty here.
  cat "$SRC_DIR/utils.js" "$SRC_DIR/onload.js" "$SRC_DIR/dashboard.js" > "$SRC_DIR/bundle.js"
  terser "$SRC_DIR/bundle.js" --compress passes=2 --mangle --toplevel --output "$SRC_DIR/utils.js"
  : > "$SRC_DIR/onload.js"
  : > "$SRC_DIR/dashboard.js"
  rm -f "$SRC_DIR/bundle.js"
  # CSS: drop comments and collapse whitespace (no selector/value rewriting)
  for css in "$SRC_DIR"/style.css "$SRC_DIR"/dashboard.css; do
    python - "$css" <<'PY'
import re, sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
s = re.sub(r"/\*.*?\*/", "", s, flags=re.S)
s = re.sub(r"\s+", " ", s)
s = re.sub(r"\s*([{}:;,>])\s*", r"\1", s)
s = s.replace(";}", "}")
open(p, "w", encoding="utf-8").write(s.strip())
PY
  done
elif [ "${SGOS_HTML_MINIFY:-1}" != "0" ]; then
  echo "WARNING: terser not found, app.html will embed unminified JS (npm install -g terser)" >&2
fi

render() {
  local template="$1"
  local out="$2"
  local tmp
  tmp="$(mktemp "${out}.tmp.XXXXXX")"
  # ejs include() paths are resolved relative to the current directory.
  if (cd "$SRC_DIR" && ejs-cli -O "$DATA_ABS" -f "$template") > "$tmp" && [ -s "$tmp" ]; then
    mv -f "$tmp" "$out"
  else
    rm -f "$tmp"
    echo "ERROR: rendering $template failed (existing output left untouched)" >&2
    exit 1
  fi
}

render config.json "$OUT_DIR/config.json"
render index.html "$OUT_DIR/app.html"

for f in app.html config.json; do
  raw=$(wc -c < "$OUT_DIR/$f")
  gz=$(gzip -9 -n -c "$OUT_DIR/$f" | wc -c)
  printf "%-12s %7d bytes raw, %6d bytes gzip\n" "$f" "$raw" "$gz"
done

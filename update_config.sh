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

# Exports the CUE configuration tree at CONFIG_PATH into CONFIG_FILE (JSON).
# Requires cue 0.0.8: the .cue files use the legacy path-shorthand syntax
# ("modules ota fields basedir default: ...") that newer cue versions reject.

set -euo pipefail

CONFIG_PATH="${1:-}"
CONFIG_FILE="${2:-}"

if [ -z "$CONFIG_PATH" ] || [ -z "$CONFIG_FILE" ]; then
  echo "USAGE: $0 config_path config.json" >&2
  exit 1
fi

if [ ! -d "$CONFIG_PATH" ]; then
  echo "ERROR: config path not found: $CONFIG_PATH" >&2
  exit 1
fi

# cue 0.0.8 lives in $HOME/go/bin (go install), /usr/local/bin (prebuilt, WSL) or $HOME/esp/cue (Windows)
export PATH="$PATH:$HOME/go/bin:/usr/local/bin:$HOME/esp/cue"

if ! command -v cue >/dev/null 2>&1; then
  echo "ERROR: cue not found in PATH (this repo needs cue v0.0.8, e.g. 'go install cuelang.org/go/cmd/cue@v0.0.8')" >&2
  exit 1
fi

case "$CONFIG_FILE" in
  /*) OUT="$CONFIG_FILE" ;;
  *) OUT="$(pwd)/$CONFIG_FILE" ;;
esac
TMP="$(mktemp "${OUT}.tmp.XXXXXX")"
trap 'rm -f "$TMP"' EXIT

echo "cue: $(cue version 2>/dev/null | head -1)"
(cd "$CONFIG_PATH" && cue export ./...) > "$TMP"

if [ ! -s "$TMP" ]; then
  echo "ERROR: cue export produced no output" >&2
  exit 1
fi

if command -v python3 >/dev/null 2>&1; then
  python3 -c 'import json,sys; json.load(open(sys.argv[1], encoding="utf-8"))' "$TMP"
fi

mv -f "$TMP" "$OUT"
trap - EXIT
echo "Wrote $OUT ($(wc -c < "$OUT") bytes)"

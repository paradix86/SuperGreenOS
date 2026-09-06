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

# Renders every *.template under DIR (default: main) with ejs-cli, using the
# generated config JSON as data model. Output is written to a temp file and
# moved into place only on success, so a missing/failing ejs-cli can never
# leave 0-byte generated sources behind.

set -euo pipefail

DATA="${1:-}"
DIR="${2:-main}"

if [ -z "$DATA" ] || [ ! -f "$DATA" ]; then
  echo "USAGE: $0 /path/to/config.json [dir]" >&2
  exit 1
fi

if ! command -v ejs-cli >/dev/null 2>&1; then
  echo "ERROR: ejs-cli not found in PATH (install with: npm install -g ejs-cli)" >&2
  exit 1
fi

GREEN="\033[0;32m"
RED="\033[0;31m"
NC="\033[0m"
status=0

while IFS= read -r -d '' template; do
  out="${template%.template}"
  tmp="$(mktemp "${out}.tmp.XXXXXX")"
  if ejs-cli -O "$DATA" -f "$template" > "$tmp" && [ -s "$tmp" ]; then
    mv -f "$tmp" "$out"
    echo -e "Processing $template: ${GREEN}Done${NC}"
  else
    rm -f "$tmp"
    echo -e "Processing $template: ${RED}FAILED${NC} (existing output left untouched)" >&2
    status=1
  fi
done < <(find "$DIR" -name '*.template' -print0 | sort -z)

exit $status

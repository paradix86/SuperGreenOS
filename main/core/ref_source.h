/*
 * Copyright (C) 2026  SuperGreenLab <towelie@supergreenlab.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef CORE_REF_SOURCE_H_
#define CORE_REF_SOURCE_H_

#include <stdbool.h>
#include "kv/kv.h"

// The indirect sensor source for *_REF encodes both sensor kind and i2c port
// (see fan.cue/blower.cue/valve.cue and the generated indir helpers); box
// timer output (8/9/10) isn't sensor-backed and is always considered fresh.
// Shared by fan.c, blower.c and valve.c, which previously each carried an
// identical copy of this mapping.
static inline bool is_ref_source_absent(int source) {
  if (source >= 1 && source <= 3)   return !get_sht21_present(source - 1);
  if (source >= 15 && source <= 17) return !get_sht21_present(source - 15);
  if (source >= 23 && source <= 25) return !get_sht21_present(source - 23);
  if (source >= 30 && source <= 32) return !get_scd30_present(source - 30);
  if (source >= 37 && source <= 39) return !get_scd30_present(source - 37);
  if (source >= 44 && source <= 46) return !get_scd30_present(source - 44);
  if (source >= 50 && source <= 52) return !get_scd30_present(source - 50);
  return false;  // box timer output (8/9/10) or an unmapped source
}

#endif

/*
 * Copyright (C) 2018  SuperGreenLab <towelie@supergreenlab.com>
 * Author: Constantin Clauzel <constantin.clauzel@gmail.com>
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

#include "manual.h"

#include "../core/kv/kv.h"
#include "../core/log/log.h"
#include "../led/led.h"
#include "../timer/timer.h"

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))

void start_manual(int boxId) {
  ESP_LOGI(SGO_LOG_NOSEND, "@MANUAL_%d start_manual", boxId);
  manual_task(boxId);
}

void stop_manual(int boxId) {
  ESP_LOGI(SGO_LOG_NOSEND, "@MANUAL_%d stop_manual", boxId);
  set_box_timer_output(boxId, 0);
}

// Manual mode has no schedule of its own: the LED output is whatever was last
// written to BOX_N_TIMER_MANUAL_OUTPUT (0 until the app boosts it).
void manual_task(int boxId) {
  set_box_timer_output(boxId, get_box_timer_manual_output(boxId));
}

int on_set_box_timer_manual_output(int boxId, int value) {
  value = min(100, max(value, 0));
  set_box_timer_manual_output(boxId, value);
  if (get_box_timer_type(boxId) == TIMER_MANUAL) {
    manual_task(boxId);
    refresh_led(boxId, -1);
  }
  return value;
}

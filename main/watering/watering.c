/*
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

#include <stdlib.h>
#include "watering.h"

#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "../core/kv/kv.h"
#include "../core/log/log.h"
#include "../core/modules.h"

#ifdef MODULE_MOTOR
#include "../motor/motor.h"
#endif

// Below this epoch (2017-07-14) the clock was never set: after an NVS erase
// time() starts near 0 until NTP syncs, and every "now - last" test is garbage.
#define MIN_VALID_EPOCH 1500000000

#define max(x, y) (((x) > (y)) ? (x) : (y))
#define min(x, y) (((x) < (y)) ? (x) : (y))

static QueueHandle_t cmd;
static bool was_watering[N_BOX] = { false };

typedef enum {
  CMD_NO_ACTION,
  CMD_REFRESH,
} watering_cmd;

static void watering_task(void *param);

void init_watering() {
  ESP_LOGI(SGO_LOG_NOSEND, "@WATERING Initializing watering module\n");

  cmd = xQueueCreate(10, sizeof(watering_cmd));
  if (cmd == NULL) {
    ESP_LOGE(SGO_LOG_NOSEND, "@WATERING Unable to create watering queue");
  }

  xTaskCreatePinnedToCore(watering_task, "WATERING", 4096, NULL, 10, NULL, 1);
}

static void watering_task(void *param) {
  watering_cmd c = CMD_NO_ACTION;
  while (true) {
    time_t now;
    time(&now);
    const bool clock_valid = now >= MIN_VALID_EPOCH;
    for (int i = 0; i < N_BOX; ++i) {
      if (get_box_enabled(i) != 1) continue;
      int left = get_box_watering_left(i);
      if (left == 0) continue;

      if (!clock_valid) {
        if (get_box_watering_duty(i) != 0) {
          ESP_LOGW(SGO_LOG_NOSEND, "@WATERING Clock not set, keeping box %d pump off", i);
          set_box_watering_duty(i, 0);
        }
        was_watering[i] = false;
        continue;
      }

      const int last = get_box_watering_last(i);
      const int period = get_box_watering_period(i);
      const int duration = get_box_watering_duration(i);
      const int power = get_box_watering_power(i);

      bool watering = now >= last && now - last < duration;
      if (watering) {
        set_box_watering_duty(i, power);
      } else {
        set_box_watering_duty(i, 0);
        // one watering just finished: consume one credit (only once, not every second)
        if (was_watering[i] && left > 0) {
          --left;
          set_box_watering_left(i, left);
        }
        if (left != 0 && now - last > period * 60) {
          set_box_watering_last(i, now);
          set_box_watering_duty(i, power);
          watering = true;
        }
      }
      was_watering[i] = watering;
    }
    if (c == CMD_REFRESH) {
#if defined(MODULE_MOTOR)
      refresh_motors();
#endif
      c = CMD_NO_ACTION;
    }
    if (xQueueReceive(cmd, &c, 1000 / portTICK_PERIOD_MS) == pdTRUE) {
    }
  }
}

void refresh_watering() {
  watering_cmd c = CMD_REFRESH;
  xQueueSend(cmd, &c, 0);
}

// KV callbacks

int on_set_box_watering_period(int boxId, int value) {
  set_box_watering_period(boxId, value);
  refresh_watering();
  return value;
}

int on_set_box_watering_duration(int boxId, int value) {
  set_box_watering_duration(boxId, value);
  refresh_watering();
  return value;
}

int on_set_box_watering_last(int boxId, int value) {
  set_box_watering_last(boxId, value);
  refresh_watering();
  return value;
}

int on_set_box_watering_power(int boxId, int value) {
  value = min(100, max(value, 0));
  set_box_watering_power(boxId, value);
  refresh_watering();
  return value;
}

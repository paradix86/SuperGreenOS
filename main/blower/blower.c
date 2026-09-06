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

#include "blower.h"

#include "../core/modules.h"

#include "math.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "../core/log/log.h"
#include "../core/kv/kv.h"
#include "../motor/motor.h"
#include "../core/ref_source.h"
#include "esp_task_wdt.h"

#define max(x, y) (((x) > (y)) ? (x) : (y))
#define min(x, y) (((x) < (y)) ? (x) : (y))


// is_ref_source_absent() lives in ../core/ref_source.h (shared with
// fan.c/valve.c). Default here favors ventilation (max duty) since trapped
// heat/humidity is generally the bigger risk for a grow box than a bit of
// extra airflow; flip this if that assumption doesn't hold for your setup.

// Percentage points per loop iteration (~10s steady state, faster if a
// setting change triggers an immediate refresh): an instant 20->100 jump on
// a fan/blower stresses the motor and can pressure-shock a sealed grow box.
#define MAX_DUTY_STEP 20

static int step_towards(int current, int target, int max_step) {
  int diff = target - current;
  if (diff > max_step) diff = max_step;
  if (diff < -max_step) diff = -max_step;
  return current + diff;
}

static QueueHandle_t cmd;

typedef enum {
  CMD_NO_ACTION,
  CMD_REFRESH,
} blower_cmd;

static void blower_task(void *param) {
  blower_cmd c = CMD_NO_ACTION;
  esp_task_wdt_add(NULL);
  while (1) {
    esp_task_wdt_reset();
    for (int i = 0; i < N_BOX; ++i) {
      if (get_box_enabled(i) != 1) continue;
      int vmin = get_box_blower_min(i);
      int vmax = get_box_blower_max(i);
      int rmin = get_box_blower_ref_min(i);
      int rmax = get_box_blower_ref_max(i);

      int refOutput = 50;
      bool source_absent = is_ref_source_absent(get_box_blower_ref_source(i));
      if (source_absent) {
        refOutput = 100;
      } else if (rmin != rmax) {
        int ref = get_box_blower_ref(i);
        refOutput = (float)(ref - rmin) / (float)(rmax - rmin) * 100.0f;
        refOutput = min(100, max(refOutput, 0));
      }
      int target = (float)vmin + ((vmax - vmin) * (float)refOutput / 100.0f);
      int v = source_absent ? target : step_towards(get_box_blower_duty(i), target, MAX_DUTY_STEP);
      set_box_blower_duty(i, v);
    }
    if (c == CMD_REFRESH) {
#if defined(MODULE_MOTOR)
      refresh_motors();
#endif
      c = CMD_NO_ACTION;
    }

    if (xQueueReceive(cmd, &c, 10000 / portTICK_PERIOD_MS) == pdTRUE) {
    }
  }
}

void refresh_blower() {
  blower_cmd c = CMD_REFRESH;
  xQueueSend(cmd, &c, 0);
}

void init_blower() {
  ESP_LOGI(SGO_LOG_NOSEND, "@BLOWER Initializing blower task");

  cmd = xQueueCreate(10, sizeof(blower_cmd));
  if (cmd == NULL) {
    ESP_LOGE(SGO_LOG_NOSEND, "@BLOWER Unable to create blower queue");
  }

  BaseType_t ret = xTaskCreatePinnedToCore(blower_task, "BLOWER", 4096, NULL, 10, NULL, 1);
  if (ret != pdPASS) {
    ESP_LOGE(SGO_LOG_NOSEND, "@BLOWER Failed to create task");
  }
}

/* KV Callbacks */

int on_set_box_blower_ref_source(int motorId, int value) {
  set_box_blower_ref_source(motorId, value);
  refresh_blower();
  return value;
}

int on_set_box_blower_min(int boxId, int value) {
  value = min(100, max(value, 0));
  set_box_blower_min(boxId, value);
  refresh_blower();
  return value;
}

int on_set_box_blower_max(int boxId, int value) {
  value = min(100, max(value, 0));
  set_box_blower_max(boxId, value);
  refresh_blower();
  return value;
}

int on_set_box_blower_ref_min(int boxId, int value) {
  value = min(100, max(value, 0));
  set_box_blower_ref_min(boxId, value);
  refresh_blower();
  return value;
}

int on_set_box_blower_ref_max(int boxId, int value) {
  value = min(100, max(value, 0));
  set_box_blower_ref_max(boxId, value);
  refresh_blower();
  return value;
}



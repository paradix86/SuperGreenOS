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

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sensor_health.h"
#include "../core/log/log.h"
#include "../core/kv/kv.h"

#define SENSOR_HEALTH_STATUS_UNKNOWN 0
#define SENSOR_HEALTH_STATUS_OK 1
#define SENSOR_HEALTH_STATUS_WARN 3

#define SENSOR_HEALTH_BOX_COUNT 3

typedef struct {
  int32_t temp;
  int32_t humi;
  int32_t vpd;
  int32_t co2;
  int8_t temp_source;
  int8_t humi_source;
  int8_t vpd_source;
  int8_t co2_source;
  uint8_t temp_same_count;
  uint8_t humi_same_count;
  uint8_t vpd_same_count;
  uint8_t co2_same_count;
  uint8_t warmup_remaining;
  bool initialized;
} sensor_health_box_state_t;

static sensor_health_box_state_t g_box_state[SENSOR_HEALTH_BOX_COUNT];
static int8_t g_last_status = SENSOR_HEALTH_STATUS_UNKNOWN;
static char g_last_alert[64];

static int8_t get_box_enabled_by_index(int box) {
  switch (box) {
    case 0:
      return get_box_0_enabled();
    case 1:
      return get_box_1_enabled();
    case 2:
      return get_box_2_enabled();
    default:
      return 0;
  }
}

static int32_t get_box_temp_by_index(int box) {
  switch (box) {
    case 0:
      return get_box_0_temp();
    case 1:
      return get_box_1_temp();
    case 2:
      return get_box_2_temp();
    default:
      return 0;
  }
}

static int32_t get_box_humi_by_index(int box) {
  switch (box) {
    case 0:
      return get_box_0_humi();
    case 1:
      return get_box_1_humi();
    case 2:
      return get_box_2_humi();
    default:
      return 0;
  }
}

static int32_t get_box_vpd_by_index(int box) {
  switch (box) {
    case 0:
      return get_box_0_vpd();
    case 1:
      return get_box_1_vpd();
    case 2:
      return get_box_2_vpd();
    default:
      return 0;
  }
}

static int32_t get_box_co2_by_index(int box) {
  switch (box) {
    case 0:
      return get_box_0_co2();
    case 1:
      return get_box_1_co2();
    case 2:
      return get_box_2_co2();
    default:
      return 0;
  }
}

static int8_t get_box_temp_source_by_index(int box) {
  switch (box) {
    case 0:
      return get_box_0_temp_source();
    case 1:
      return get_box_1_temp_source();
    case 2:
      return get_box_2_temp_source();
    default:
      return 0;
  }
}

static int8_t get_box_humi_source_by_index(int box) {
  switch (box) {
    case 0:
      return get_box_0_humi_source();
    case 1:
      return get_box_1_humi_source();
    case 2:
      return get_box_2_humi_source();
    default:
      return 0;
  }
}

static int8_t get_box_vpd_source_by_index(int box) {
  switch (box) {
    case 0:
      return get_box_0_vpd_source();
    case 1:
      return get_box_1_vpd_source();
    case 2:
      return get_box_2_vpd_source();
    default:
      return 0;
  }
}

static int8_t get_box_co2_source_by_index(int box) {
  switch (box) {
    case 0:
      return get_box_0_co2_source();
    case 1:
      return get_box_1_co2_source();
    case 2:
      return get_box_2_co2_source();
    default:
      return 0;
  }
}

static void reset_box_health_state(sensor_health_box_state_t *state) {
  memset(state, 0, sizeof(*state));
  state->warmup_remaining = get_sensor_health_warmup_samples();
}

static void update_same_count(int32_t current, int32_t *previous, uint8_t *count, bool initialized) {
  if (!initialized) {
    *previous = current;
    *count = 0;
    return;
  }

  if (current == *previous) {
    if (*count < 255) {
      (*count)++;
    }
  } else {
    *previous = current;
    *count = 0;
  }
}

static bool update_box_health(int box, char *alert, size_t alert_len) {
  sensor_health_box_state_t *state = &g_box_state[box];
  int8_t temp_source = get_box_temp_source_by_index(box);
  int8_t humi_source = get_box_humi_source_by_index(box);
  int8_t vpd_source = get_box_vpd_source_by_index(box);
  int8_t co2_source = get_box_co2_source_by_index(box);
  uint8_t stuck_samples = get_sensor_health_stuck_samples();

  if (!get_box_enabled_by_index(box)) {
    reset_box_health_state(state);
    return false;
  }

  if (state->initialized &&
      (state->temp_source != temp_source ||
       state->humi_source != humi_source ||
       state->vpd_source != vpd_source ||
       state->co2_source != co2_source)) {
    reset_box_health_state(state);
  }

  state->temp_source = temp_source;
  state->humi_source = humi_source;
  state->vpd_source = vpd_source;
  state->co2_source = co2_source;

  if (temp_source > 0) {
    update_same_count(get_box_temp_by_index(box), &state->temp, &state->temp_same_count, state->initialized);
  }
  if (humi_source > 0) {
    update_same_count(get_box_humi_by_index(box), &state->humi, &state->humi_same_count, state->initialized);
  }
  if (vpd_source > 0) {
    update_same_count(get_box_vpd_by_index(box), &state->vpd, &state->vpd_same_count, state->initialized);
  }
  if (co2_source > 0) {
    update_same_count(get_box_co2_by_index(box), &state->co2, &state->co2_same_count, state->initialized);
  }
  state->initialized = true;

  if (state->warmup_remaining > 0) {
    state->warmup_remaining--;
    snprintf(alert, alert_len, "box_%d_warmup", box);
    return false;
  }

  if (temp_source > 0 && state->temp_same_count >= stuck_samples) {
    snprintf(alert, alert_len, "box_%d_temp_stuck", box);
    return true;
  }
  if (humi_source > 0 && state->humi_same_count >= stuck_samples) {
    snprintf(alert, alert_len, "box_%d_humi_stuck", box);
    return true;
  }
  if (vpd_source > 0 && state->vpd_same_count >= stuck_samples) {
    snprintf(alert, alert_len, "box_%d_vpd_stuck", box);
    return true;
  }
  if (co2_source > 0 && state->co2_same_count >= stuck_samples) {
    snprintf(alert, alert_len, "box_%d_co2_stuck", box);
    return true;
  }

  return false;
}

static uint32_t get_sensor_health_period_ms() {
  uint16_t period_s = get_sensor_health_period_s();
  if (period_s == 0) {
    period_s = 60;
  }

  return (uint32_t)period_s * 1000;
}

static void publish_health_state(int8_t status, const char *alert) {
  if (status != g_last_status || strcmp(alert, g_last_alert) != 0) {
    ESP_LOGI(SGO_LOG_NOSEND, "@SENSOR_HEALTH status=%d alert=%s", status, alert);
    g_last_status = status;
    strncpy(g_last_alert, alert, sizeof(g_last_alert) - 1);
    g_last_alert[sizeof(g_last_alert) - 1] = 0;
  }

  set_sensor_health_status(status);
  set_sensor_health_last_alert(alert);
}

static void sensor_health_task(void *param) {
  char alert[64];

  while (1) {
    if (!get_sensor_health_enabled()) {
      publish_health_state(SENSOR_HEALTH_STATUS_UNKNOWN, "disabled");
      vTaskDelay(get_sensor_health_period_ms() / portTICK_PERIOD_MS);
      continue;
    }

    alert[0] = 0;
    int8_t status = SENSOR_HEALTH_STATUS_OK;

    for (int box = 0; box < SENSOR_HEALTH_BOX_COUNT; box++) {
      if (update_box_health(box, alert, sizeof(alert))) {
        status = SENSOR_HEALTH_STATUS_WARN;
        break;
      }
    }

    if (status == SENSOR_HEALTH_STATUS_OK) {
      strncpy(alert, "ok", sizeof(alert) - 1);
      alert[sizeof(alert) - 1] = 0;
    }

    publish_health_state(status, alert);
    vTaskDelay(get_sensor_health_period_ms() / portTICK_PERIOD_MS);
  }
}

void init_sensor_health() {
  ESP_LOGI(SGO_LOG_NOSEND, "@SENSOR_HEALTH Initializing sensor_health module");

  // No defaults here: is_*_undefined() is a RAM flag that is true on every
  // boot, so seeding through it overwrote the NVS-stored settings at each
  // reboot (stuck_samples went 15 -> 5 after an OTA on 2026-09-07). kv.c
  // already applies the CUE defaults when a key is missing from NVS.
  set_sensor_health_status(SENSOR_HEALTH_STATUS_UNKNOWN);
  set_sensor_health_last_alert("");

  BaseType_t ret = xTaskCreatePinnedToCore(sensor_health_task, "SENSOR_HEALTH", 4096, NULL, 5, NULL, 1);
  if (ret != pdPASS) {
    ESP_LOGE(SGO_LOG_NOSEND, "@SENSOR_HEALTH Failed to create task");
  }
}

/*
 * Copyright (C) 2019  SuperGreenLab <towelie@supergreenlab.com>
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

#include "reboot.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "../log/log.h"
#include "../kv/kv.h"
#include "../ota/ota.h"

#define MAX_SHORT_REBOOTS 5
#define N_SHORT_REBOOTS "NSHRBTS"

// esp_get_minimum_free_heap_size() says how low the heap ever got, not when:
// sample it periodically so /mqttdiag can also report the uptime at which the
// minimum was reached and how many times free heap dipped below the floor.
#define HEAP_WATCH_PERIOD_MS 5000
#define HEAP_LOW_FLOOR_BYTES 8192

static volatile long heap_min_free_at_s = 0;
static volatile int heap_low_events = 0;

static void heap_watch_task(void *args) {
  unsigned int last_min = esp_get_minimum_free_heap_size();
  bool was_low = false;
  while (true) {
    unsigned int min_free = esp_get_minimum_free_heap_size();
    unsigned int free_now = esp_get_free_heap_size();
    long uptime_s = (long)(esp_timer_get_time() / 1000000LL);
    if (min_free < last_min) {
      last_min = min_free;
      heap_min_free_at_s = uptime_s;
      // NOSEND on purpose: publishing over MQTT allocates, and we are here
      // precisely because memory is scarce.
      ESP_LOGW(SGO_LOG_NOSEND, "@HEAP new minimum %u bytes at uptime %lds (free now %u)", min_free, uptime_s, free_now);
    }
    bool is_low = free_now < HEAP_LOW_FLOOR_BYTES;
    if (is_low && !was_low) {
      ++heap_low_events;
    }
    was_low = is_low;
    vTaskDelay(HEAP_WATCH_PERIOD_MS / portTICK_PERIOD_MS);
  }
}

long get_heap_min_free_at() {
  return heap_min_free_at_s;
}

int get_heap_low_events() {
  return heap_low_events;
}

// Comma-separated esp_reset_reason_t values, newest first, so a crash-loop
// pattern is visible the next time the device is reachable, not just the
// single latest reason exposed live by /mqttdiag.
#define RESET_HISTORY_KEY "RST_HIST"
#define RESET_HISTORY_MAX_ENTRIES 10

static QueueHandle_t cmd;

static void autoreboot_task();
static void reboot_task();

static void record_reset_reason() {
  char history[128] = {0};
  getstr(RESET_HISTORY_KEY, history, sizeof(history) - 1);

  char entry[8];
  snprintf(entry, sizeof(entry), "%d", (int)esp_reset_reason());

  char new_history[128];
  if (history[0] == 0) {
    snprintf(new_history, sizeof(new_history), "%s", entry);
  } else {
    snprintf(new_history, sizeof(new_history), "%s,%s", entry, history);
  }

  // keep only the newest RESET_HISTORY_MAX_ENTRIES comma-separated entries
  size_t len = strlen(new_history);
  int commas = 0;
  for (size_t i = 0; i < len; ++i) {
    if (new_history[i] == ',') {
      if (++commas == RESET_HISTORY_MAX_ENTRIES) {
        new_history[i] = 0;
        break;
      }
    }
  }

  setstr(RESET_HISTORY_KEY, new_history);
}

void get_reset_history(char *dest, size_t len) {
  getstr(RESET_HISTORY_KEY, dest, len);
}

void reset_on_next_reboot() {
  seti8(N_SHORT_REBOOTS, MAX_SHORT_REBOOTS);
}

void init_reboot() {
  record_reset_reason();

  if (hasi32(N_SHORT_REBOOTS)) { // detect old version
    ESP_LOGW(SGO_LOG_NOSEND, "@REBOOT Migrating counter type");
    remove_key(N_SHORT_REBOOTS);
    seti8(N_SHORT_REBOOTS, 0);
  }
  defaulti8(N_SHORT_REBOOTS, 0);
  int n = geti8(N_SHORT_REBOOTS);
  if (n >= MAX_SHORT_REBOOTS) {
    ESP_LOGE(SGO_LOG_NOSEND, "@REBOOT Critical: %d short reboots. Preserving NVS and resetting counter.", n);
    seti8(N_SHORT_REBOOTS, 0);
    n = 0;
  }
  ESP_LOGI(SGO_LOG_EVENT, "@REBOOT N_SHORT_REBOOTS=%d", n);
  seti8(N_SHORT_REBOOTS, ++n);

  cmd = xQueueCreate(10, sizeof(unsigned char));
  if (cmd == NULL) {
    ESP_LOGE(SGO_LOG_NOSEND, "@REBOOT Unable to create reboot queue");
  }

  BaseType_t ret = xTaskCreatePinnedToCore(autoreboot_task, "AUTOREBOOT", 2048, NULL, 10, NULL, 1);
  if (ret != pdPASS) {
    ESP_LOGE(SGO_LOG_NOSEND, "@REBOOT Failed to create task");
  }

  BaseType_t ret2 = xTaskCreatePinnedToCore(reboot_task, "REBOOT", 2048, NULL, 10, NULL, 1);
  if (ret2 != pdPASS) {
    ESP_LOGE(SGO_LOG_NOSEND, "@REBOOT Failed to create task");
  }

  BaseType_t ret3 = xTaskCreatePinnedToCore(heap_watch_task, "HEAPWATCH", 2048, NULL, tskIDLE_PRIORITY + 1, NULL, 1);
  if (ret3 != pdPASS) {
    ESP_LOGE(SGO_LOG_NOSEND, "@REBOOT Failed to create heap watch task");
  }
}

static void autoreboot_task(void *args) {
  // reset n_short_reboots to zero
  vTaskDelay(10 * 1000 / portTICK_PERIOD_MS);
  ESP_LOGI(SGO_LOG_EVENT, "@REBOOT N_SHORT_REBOOTS=0");
  seti8(N_SHORT_REBOOTS, 0);

  vTaskDelete(NULL);
}

static void reboot_task(void *args) {
  unsigned char c;
  while (true) {
    if (xQueueReceive(cmd, &c, 10000 / portTICK_PERIOD_MS)) {
      // little wait to allow http response to go through
      vTaskDelay(500 / portTICK_PERIOD_MS);
      esp_restart();
    }
  }
}

void reboot_esp() {
  unsigned char c = 1;
  xQueueSend(cmd, &c, 0);
}

/*
 * http callback
 */

int on_set_reboot(int value) {
  reboot_esp();
  return value;
}

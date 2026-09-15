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

#include <stdlib.h>
#include "timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"

#include "../core/kv/kv.h"
#include "../core/log/log.h"
#include "../led/led.h"
#include "../mixer/mixer.h"
#include "../box/box.h"

#include "../manual/manual.h"
#include "../onoff/onoff.h"
#include "../season/season.h"
#include "../state/state.h"

typedef enum {
  CMD_NO_ACTION,
  CMD_REFRESH,
} timer_cmd;

/* Longest boost accepted, matching the 60 min the app offers. A cap is what
 * stops a bogus or hand-written value from pinning the light to full for days. */
#define BOX_TIMER_BOOST_MAX_S 3600

static QueueHandle_t cmd;

/* Deadline of each box's boost, in microseconds since boot (0 = no boost).
 * Kept here rather than derived from the KV value so the countdown follows real
 * time instead of however often this task happens to wake up. Like
 * BOX_N_TIMER_BOOST_S itself this lives in RAM only: a reboot drops the boost
 * and the box goes back to its schedule. */
static int64_t boost_until[N_BOX] = {0};

static void timer_task(void *param);
static void stop(int boxId, enum timer t);
static void start(int boxId, enum timer t);

void init_timer() {
  ESP_LOGI(SGO_LOG_NOSEND, "@TIMER Initializing timer task");
  cmd = xQueueCreate(10, sizeof(timer_cmd));
  if (cmd == NULL) {
    ESP_LOGE(SGO_LOG_NOSEND, "@TIMER Unable to create timer queue");
  }

  for (int i = 0; i < N_BOX; ++i) {
    if (get_box_enabled(i) != 1) continue;
    start(i, get_box_timer_type(i));
  }

  BaseType_t ret = xTaskCreatePinnedToCore(timer_task, "TIMER", 4096, NULL, tskIDLE_PRIORITY, NULL, 1);
  if (ret != pdPASS) {
    ESP_LOGE(SGO_LOG_NOSEND, "@TIMER Failed to create task");
  }
}

static void stop(int boxId, enum timer t) {
  switch(t) {
    case TIMER_MANUAL:
      stop_manual(boxId);
      break;
    case TIMER_ONOFF:
      stop_onoff(boxId);
      break;
    case TIMER_SEASON:
      stop_season(boxId);
      break;
  }
}

static void start(int boxId, enum timer t) {
  switch(t) {
    case TIMER_MANUAL:
      start_manual(boxId);
      break;
    case TIMER_ONOFF:
      start_onoff(boxId);
      break;
    case TIMER_SEASON:
      start_season(boxId);
      break;
  }
}

static void timer_task(void *param) {
  timer_cmd c = CMD_NO_ACTION;

  /* This task decides every box's light output once a second. If it ever stops
   * feeding the watchdog the lights are frozen wherever they happen to be, with
   * nothing else to notice - so let the watchdog reboot us instead. One pass is
   * at most the 1 s queue wait plus a handful of KV reads, far under the 30 s
   * CONFIG_TASK_WDT_TIMEOUT_S. */
  esp_task_wdt_add(NULL);

  while (1) {
    esp_task_wdt_reset();
    for (int i = 0; i < N_BOX; ++i) {
      if (get_box_enabled(i) != 1) {
        set_box_timer_output(i, 0);
        set_box_uva_timer_output(i, 0);
        set_box_db_timer_output(i, 0);
        set_box_dr_timer_output(i, 0);
        set_box_fr_timer_output(i, 0);
        continue;
      }
      enum timer t = get_box_timer_type(i);

      switch(t) {
        case TIMER_MANUAL:
          manual_task(i);
          break;
        case TIMER_ONOFF:
          onoff_task(i);
          break;
        case TIMER_SEASON:
          season_task(i);
          break;
      }

      /* A boost overrides the output the mode task just computed, and nothing
       * else: the box stays in its own timer type, so when the boost runs out
       * the next tick recomputes the real output on its own - there is no
       * saved state to restore and no way to end up stuck. */
      if (boost_until[i] > 0) {
        int64_t left_us = boost_until[i] - esp_timer_get_time();
        if (left_us > 0) {
          set_box_timer_output(i, 100);
          set_box_timer_boost_s(i, (left_us + 999999) / 1000000);
        } else {
          boost_until[i] = 0;
          set_box_timer_boost_s(i, 0);
          ESP_LOGI(SGO_LOG_NOSEND, "@TIMER_%d boost expired", i);
        }
      }
    }
    if (c == CMD_REFRESH) {
      refresh_led(-1, -1);
      c = CMD_NO_ACTION;
    }
    if (xQueueReceive(cmd, &c, 1000 / portTICK_PERIOD_MS) == pdTRUE) {
    }
  }
}

void refresh_timer() {
  timer_cmd c = CMD_REFRESH;
  xQueueSend(cmd, &c, 0);
}

// KV Callbacks

int on_set_box_timer_type(int boxId, int value) {
  int old = get_box_timer_type(boxId);
  if (old == value) return value;

  set_box_timer_type(boxId, value);
  stop(boxId, old);
  start(boxId, value);

  if (value == 0) {
    set_all_duty(boxId, 0, -1);
  }
  refresh_led(boxId, -1);
  return value;
}

int on_set_box_timer_boost_s(int boxId, int value) {
  if (value < 0) value = 0;
  if (value > BOX_TIMER_BOOST_MAX_S) value = BOX_TIMER_BOOST_MAX_S;

  set_box_timer_boost_s(boxId, value);
  boost_until[boxId] = value > 0 ? esp_timer_get_time() + (int64_t)value * 1000000 : 0;
  ESP_LOGI(SGO_LOG_NOSEND, "@TIMER_%d boost set to %ds", boxId, value);

  /* Wake the task so the light follows immediately instead of on its next
   * second, and so writing 0 ends the boost right away. */
  refresh_timer();
  return value;
}

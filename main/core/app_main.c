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

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"

#include "log/log.h"
#include "kv/kv.h"
#include "tester/tester.h"
#include "cmd/cmd.h"
#include "term/term.h"
#include "reboot/reboot.h"
#include "mqtt/mqtt.h"
#include "wifi/wifi.h"
#include "time/time.h"
#include "i2c/i2c.h"
#include "ota/ota.h"
#include "stat_dump/stat_dump.h"
#include "httpd/httpd.h"
#include "modules.h"

void preinit_app();
void init_app(bool tester);

// A handful of NVS-backed fields drive raw time-of-day math or PWM ranges with
// no runtime validation elsewhere; a corrupted/partial NVS write (power loss
// mid-write, a bad manual edit) could leave one out of range forever. Clamp
// the highest-impact ones back to a safe default once at boot.
static void sanity_check_config() {
#ifdef MODULE_ONOFF
  for (int i = 0; i < N_BOX; ++i) {
    int8_t on_hour = get_box_on_hour(i);
    if (on_hour < 0 || on_hour > 23) {
      ESP_LOGW(SGO_LOG_NOSEND, "@MAIN Box %d on_hour=%d out of range, resetting to 3", i, on_hour);
      set_box_on_hour(i, 3);
    }
    int8_t off_hour = get_box_off_hour(i);
    if (off_hour < 0 || off_hour > 23) {
      ESP_LOGW(SGO_LOG_NOSEND, "@MAIN Box %d off_hour=%d out of range, resetting to 21", i, off_hour);
      set_box_off_hour(i, 21);
    }
    int8_t on_min = get_box_on_min(i);
    if (on_min < 0 || on_min > 59) {
      ESP_LOGW(SGO_LOG_NOSEND, "@MAIN Box %d on_min=%d out of range, resetting to 0", i, on_min);
      set_box_on_min(i, 0);
    }
    int8_t off_min = get_box_off_min(i);
    if (off_min < 0 || off_min > 59) {
      ESP_LOGW(SGO_LOG_NOSEND, "@MAIN Box %d off_min=%d out of range, resetting to 0", i, off_min);
      set_box_off_min(i, 0);
    }
  }
#endif
#ifdef MODULE_MOTOR
  for (int i = 0; i < N_MOTOR; ++i) {
    int8_t motor_min = get_motor_min(i);
    if (motor_min < 0 || motor_min > 100) {
      ESP_LOGW(SGO_LOG_NOSEND, "@MAIN Motor %d min=%d out of range, resetting to 0", i, motor_min);
      set_motor_min(i, 0);
    }
    int8_t motor_max = get_motor_max(i);
    if (motor_max < 0 || motor_max > 100) {
      ESP_LOGW(SGO_LOG_NOSEND, "@MAIN Motor %d max=%d out of range, resetting to 100", i, motor_max);
      set_motor_max(i, 100);
    }
  }
#endif
#ifdef MODULE_MOTORS
  int8_t curve = get_motors_curve();
  if (curve != 0 && curve != 1) {
    ESP_LOGW(SGO_LOG_NOSEND, "@MAIN motors_curve=%d out of range, resetting to 0", curve);
    set_motors_curve(0);
  }
#endif
#ifdef MODULE_FAN
  for (int i = 0; i < N_BOX; ++i) {
    int8_t fan_min = get_box_fan_min(i);
    if (fan_min < 0 || fan_min > 100) {
      ESP_LOGW(SGO_LOG_NOSEND, "@MAIN Box %d fan_min=%d out of range, resetting to 0", i, fan_min);
      set_box_fan_min(i, 0);
    }
    int8_t fan_max = get_box_fan_max(i);
    if (fan_max < 0 || fan_max > 100) {
      ESP_LOGW(SGO_LOG_NOSEND, "@MAIN Box %d fan_max=%d out of range, resetting to 100", i, fan_max);
      set_box_fan_max(i, 100);
    }
    int8_t fan_ref_min = get_box_fan_ref_min(i);
    if (fan_ref_min < 0 || fan_ref_min > 100) {
      ESP_LOGW(SGO_LOG_NOSEND, "@MAIN Box %d fan_ref_min=%d out of range, resetting to 0", i, fan_ref_min);
      set_box_fan_ref_min(i, 0);
    }
    int8_t fan_ref_max = get_box_fan_ref_max(i);
    if (fan_ref_max < 0 || fan_ref_max > 100) {
      ESP_LOGW(SGO_LOG_NOSEND, "@MAIN Box %d fan_ref_max=%d out of range, resetting to 100", i, fan_ref_max);
      set_box_fan_ref_max(i, 100);
    }
  }
#endif
#ifdef MODULE_BLOWER
  for (int i = 0; i < N_BOX; ++i) {
    int8_t blower_min = get_box_blower_min(i);
    if (blower_min < 0 || blower_min > 100) {
      ESP_LOGW(SGO_LOG_NOSEND, "@MAIN Box %d blower_min=%d out of range, resetting to 0", i, blower_min);
      set_box_blower_min(i, 0);
    }
    int8_t blower_max = get_box_blower_max(i);
    if (blower_max < 0 || blower_max > 100) {
      ESP_LOGW(SGO_LOG_NOSEND, "@MAIN Box %d blower_max=%d out of range, resetting to 100", i, blower_max);
      set_box_blower_max(i, 100);
    }
    int8_t blower_ref_min = get_box_blower_ref_min(i);
    if (blower_ref_min < 0 || blower_ref_min > 100) {
      ESP_LOGW(SGO_LOG_NOSEND, "@MAIN Box %d blower_ref_min=%d out of range, resetting to 0", i, blower_ref_min);
      set_box_blower_ref_min(i, 0);
    }
    int8_t blower_ref_max = get_box_blower_ref_max(i);
    if (blower_ref_max < 0 || blower_ref_max > 100) {
      ESP_LOGW(SGO_LOG_NOSEND, "@MAIN Box %d blower_ref_max=%d out of range, resetting to 100", i, blower_ref_max);
      set_box_blower_ref_max(i, 100);
    }
  }
#endif
#ifdef MODULE_WATERING
  for (int i = 0; i < N_BOX; ++i) {
    int8_t watering_power = get_box_watering_power(i);
    if (watering_power < 0 || watering_power > 100) {
      ESP_LOGW(SGO_LOG_NOSEND, "@MAIN Box %d watering_power=%d out of range, resetting to 100", i, watering_power);
      set_box_watering_power(i, 100);
    }
  }
#endif
}

void app_main() {
  ESP_LOGI(SGO_LOG_NOSEND, "@MAIN Welcome to SuperGreenOS version=%s\n", CONFIG_VERSION);

  open_kv();
  init_reboot();

  mqtt_intercept_log();

  init_kv();
  set_n_restarts(get_n_restarts()+1);
  ESP_LOGI(SGO_LOG_EVENT, "@APP Boot reset_reason=%d n_restarts=%d heap_free=%u",
      (int)esp_reset_reason(), get_n_restarts(), (unsigned int)esp_get_free_heap_size());

  sanity_check_config();

  preinit_app();

  init_cmd();
  init_term();

  init_spiffs();

#ifdef MODULE_TESTER
  init_tester();
#endif

  // Actuator modules (LED, motor, fan, blower, watering, ...) are configured
  // and zeroed here, before WiFi/MQTT/OTA come up: a remote command or a
  // stray retained MQTT message must never reach an actuator that hasn't
  // been put in a defined state yet.
#ifdef MODULE_TESTER
  bool tester_enabled = get_tester_enabled() != 0;
  init_app(tester_enabled);
  if (tester_enabled) {
    reset_on_next_reboot();
  }
#else
  init_app(false);
#endif

  init_wifi();

  init_mqtt();
  init_ota();
  init_time();

#ifdef MODULE_I2C
  init_i2c();
#endif

  init_stat_dump();

  init_httpd();

  fflush(stdout);
}

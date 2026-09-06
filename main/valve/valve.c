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
#include "valve.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "../core/kv/kv.h"
#include "../core/log/log.h"

static void valve_task(void *param);

// See fan.c/blower.c for the same helper: the indirect sensor source encodes
// both sensor kind and i2c port; box timer output (8/9/10) isn't sensor-backed.
// Unlike fan/blower (favor ventilation), a valve fails CLOSED when its sensor
// is absent: an unmonitored solenoid left open can flood or over-dose CO2.
static bool is_ref_source_absent(int source) {
  if (source >= 1 && source <= 3)   return !get_sht21_present(source - 1);
  if (source >= 15 && source <= 17) return !get_sht21_present(source - 15);
  if (source >= 23 && source <= 25) return !get_sht21_present(source - 23);
  if (source >= 30 && source <= 32) return !get_scd30_present(source - 30);
  if (source >= 37 && source <= 39) return !get_scd30_present(source - 37);
  if (source >= 44 && source <= 46) return !get_scd30_present(source - 44);
  if (source >= 50 && source <= 52) return !get_scd30_present(source - 50);
  return false;  // box timer output (8/9/10) or an unmapped source
}

static bool is_on() {
  int src = get_valve_ref_on_source();
  if (src == 0) {
    return true;
  }
  if (is_ref_source_absent(src)) {
    return false;
  }
  int ref = get_valve_ref_on();
  int minRef = get_valve_ref_on_min();
  int maxRef = get_valve_ref_on_max();
  return ref >= minRef && ref <= maxRef;
}

void init_valve() {
  ESP_LOGI(SGO_LOG_NOSEND, "@VALVE Initializing valve module");

  gpio_config_t io_conf;
  io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
  io_conf.mode = GPIO_MODE_OUTPUT;
  io_conf.pin_bit_mask = (1ULL<<get_valve_gpio());
  io_conf.pull_down_en = 1;
  io_conf.pull_up_en = 0;
  gpio_config(&io_conf);

  xTaskCreatePinnedToCore(valve_task, "VALVE", 4096, NULL, 10, NULL, 1);
}

static void valve_task(void *param) {
  int i = 0;
  while (true) {
    enum valve_mode mode = get_valve_mode();
    int ref_source = get_valve_ref_source();
    bool enabled = mode != VALVE_DISABLED && ref_source != 0 && !is_ref_source_absent(ref_source);
    if (enabled == false || !is_on()) {
      set_valve_open(0);
      gpio_set_level(get_valve_gpio(), 0);
      vTaskDelay(10 * 1000 / portTICK_PERIOD_MS);
      continue;
    }

    int ref = get_valve_ref();
    if (mode == VALVE_KEEP_BETWEEN) {
      if (!get_valve_open() && ref < get_valve_ref_min()) {
        set_valve_open(1);
        i = 0;
      } else if (get_valve_open() && ref > get_valve_ref_max()) {
        set_valve_open(0);
      }
    } else if (mode == VALVE_KEEP_OUT) {
      if (!get_valve_open() && ref >= get_valve_ref_min() && ref <= get_valve_ref_max()) {
        set_valve_open(1);
        i = 0;
      } else if (get_valve_open() && (ref < get_valve_ref_min() || ref > get_valve_ref_max())) {
        set_valve_open(0);
      }
    }
    // VALVE_CYCLE_DIV / VALVE_CYCLE_DIV_DURATION are HTTP-writable with no clamp:
    // 0 would divide by zero, respectively spin this task without yielding.
    int cycle_div = get_valve_cycle_div();
    if (cycle_div < 1) {
      cycle_div = 1;
    }
    TickType_t cycle_ticks = pdMS_TO_TICKS(get_valve_cycle_div_duration());
    if (cycle_ticks < 1) {
      cycle_ticks = 1;
    }
    gpio_set_level(get_valve_gpio(), get_valve_open() && !(i % cycle_div));
    vTaskDelay(cycle_ticks);
    ++i;
  }
}

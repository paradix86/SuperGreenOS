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

#include <math.h>
#include "mqtt.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "sodium/utils.h"
#include "mbedtls/sha256.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "nvs.h"
#include <time.h>

#include "../log/log.h"
#include "../kv/kv.h"
#include "../wifi/wifi.h"
#include "../ota/ota.h"
#include "../reboot/reboot.h"



// written by the esp-mqtt client task, read by mqtt_task and HTTP handlers
static volatile bool connected = false;

bool get_mqtt_connected() {
  return connected;
}


#include "../cmd/cmd.h"




#define MAX_REMOTE_CMD_LENGTH MAX_CMD_LENGTH-10 // keeps some space for the -r true parameter

static esp_mqtt_client_handle_t client;

static QueueHandle_t cmd;

// MQTT buffer pool: reduces stack allocation from 1900+ bytes per function to ~8KB static heap
typedef struct {
  char topic[MAX_KVALUE_SIZE];      // 517 bytes
  char payload[1400];               // 1400 bytes
  char state_topic[MAX_KVALUE_SIZE];    // 517 bytes
  char availability_topic[MAX_KVALUE_SIZE];  // 517 bytes
  char command_topic[MAX_KVALUE_SIZE];       // 517 bytes
  char device_name[64];
  char unit_part[64];
  char class_part[64];
  char alert[MAX_KVALUE_SIZE];      // 517 bytes
} mqtt_buffer_pool_t;

static mqtt_buffer_pool_t mqtt_buffers = {0};
static SemaphoreHandle_t mqtt_buffers_mutex = NULL;

static int CMD_MQTT_CONNECTED = 1;
static int CMD_MQTT_PUBLISH_STATE = 4;  // 1..3 are taken (3 = CMD_MQTT_CHANGE_SCR_CHANNEL when hasScr)


#define HA_TOPIC_PREFIX "supergreen"
#define HA_DISCOVERY_PREFIX "homeassistant"
#define HA_STATE_PUBLISH_PERIOD_MS (30 * 1000)
#define HA_DISCOVERY_PUBLISH_DELAY_MS 75
#define MQTT_DISCOVERY_CONNECT_COOLDOWN_MS 2000
#define DIAG_PUBLISH_PERIOD_MS (5 * 60 * 1000)

#define MQTT_DIAG_KEY_STAGE "MQTT_STG"
#define MQTT_DIAG_KEY_DISC_IDX "MQTT_DIDX"

// Stack high-water mark of mqtt_task, RAM only: it used to be committed to NVS
// on every loop iteration (every 10 s, ~8k flash writes a day) and never read.
static volatile int32_t mqtt_stack_hwm = -1;

int32_t get_mqtt_stack_hwm() {
  return mqtt_stack_hwm;
}

typedef enum {
  MQTT_STATE_DIAG_NONE = 0,
  MQTT_STATE_DIAG_S4A = 1,
  MQTT_STATE_DIAG_S4B = 2,
  MQTT_STATE_DIAG_S4C = 3,
  MQTT_STATE_DIAG_S4D = 4,
} mqtt_state_diag_mode_t;

typedef enum {
  MQTT_DIAG_STAGE_IDLE = 0,
  MQTT_DIAG_STAGE_CONNECTED = 1,
  MQTT_DIAG_STAGE_SUBSCRIBED = 2,
  MQTT_DIAG_STAGE_AVAILABILITY = 3,
  MQTT_DIAG_STAGE_DISCOVERY_START = 4,
  MQTT_DIAG_STAGE_DISCOVERY_DONE = 5,
  MQTT_DIAG_STAGE_STATE_ON_CONNECT = 6,
  MQTT_DIAG_STAGE_DISCONNECTED = 7,
  MQTT_DIAG_STAGE_DISCOVERY_FAIL = 8,
} mqtt_diag_stage_t;

static bool mqtt_diag_stage_initialized = false;
static bool mqtt_diag_disc_idx_initialized = false;
static int32_t mqtt_diag_last_stage = 0;
static int32_t mqtt_diag_last_disc_idx = 0;

static void mqtt_diag_set_stage(mqtt_diag_stage_t stage) {
  int32_t value = (int32_t)stage;
  if (mqtt_diag_stage_initialized && mqtt_diag_last_stage == value) {
    return;
  }
  seti32(MQTT_DIAG_KEY_STAGE, value);
  mqtt_diag_last_stage = value;
  mqtt_diag_stage_initialized = true;
}

static void mqtt_diag_set_discovery_idx(int32_t idx) {
  if (mqtt_diag_disc_idx_initialized && mqtt_diag_last_disc_idx == idx) {
    return;
  }
  seti32(MQTT_DIAG_KEY_DISC_IDX, idx);
  mqtt_diag_last_disc_idx = idx;
  mqtt_diag_disc_idx_initialized = true;
}

static void build_ha_device_name(char *dest, size_t len, const char *client_id) {
  snprintf(dest, len, "SuperGreen %s", client_id);
}

static void build_ha_state_topic(char *dest, size_t len, const char *client_id) {
  snprintf(dest, len, "%s/%s/state", HA_TOPIC_PREFIX, client_id);
}

static void build_ha_availability_topic(char *dest, size_t len, const char *client_id) {
  snprintf(dest, len, "%s/%s/availability", HA_TOPIC_PREFIX, client_id);
}

static void build_ha_command_topic(char *dest, size_t len, const char *client_id, const char *command) {
  snprintf(dest, len, "%s/%s/command/%s", HA_TOPIC_PREFIX, client_id, command);
}

static mqtt_buffer_pool_t *mqtt_buffers_acquire() {
  if (mqtt_buffers_mutex == NULL) {
    mqtt_buffers_mutex = xSemaphoreCreateMutex();
  }
  if (xSemaphoreTake(mqtt_buffers_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    return &mqtt_buffers;
  }
  return NULL;
}

static void mqtt_buffers_release() {
  if (mqtt_buffers_mutex != NULL) {
    xSemaphoreGive(mqtt_buffers_mutex);
  }
}

static int mqtt_publish_message(const char *topic, const char *payload, int retain) {
  if (!connected || client == NULL || strlen(topic) == 0) {
    return -1;
  }
  return esp_mqtt_client_publish(client, topic, payload, 0, 0, retain);
}

static void mqtt_discovery_pause() {
  vTaskDelay(pdMS_TO_TICKS(HA_DISCOVERY_PUBLISH_DELAY_MS));
}

static void mqtt_publish_ha_availability(const char *client_id, const char *state) {
  char topic[MAX_KVALUE_SIZE] = {0};
  build_ha_availability_topic(topic, sizeof(topic), client_id);
  mqtt_publish_message(topic, state, 1);
}

static const char *sensor_health_status_to_text(int status) {
  switch (status) {
    case 1:
      return "ok";
    case 2:
      return "warmup";
    case 3:
      return "warn";
    case 4:
      return "alert";
    default:
      return "unknown";
  }
}

static const char *sensor_health_problem_to_text(int status) {
  return status >= 3 ? "ON" : "OFF";
}

static const char *box_sensor_problem_to_text(const char *alert, int box) {
  char prefix[16] = {0};
  snprintf(prefix, sizeof(prefix), "box_%d_", box);
  if (alert != NULL && strstr(alert, prefix) == alert) {
    return "ON";
  }
  return "OFF";
}

static int mqtt_publish_ha_config(
    const char *client_id,
    const char *object_id,
    const char *name,
    const char *value_template,
    const char *unit,
    const char *device_class) {
  mqtt_buffer_pool_t *buf = mqtt_buffers_acquire();
  if (!buf) return -1;

  build_ha_state_topic(buf->state_topic, sizeof(buf->state_topic), client_id);
  build_ha_availability_topic(buf->availability_topic, sizeof(buf->availability_topic), client_id);
  build_ha_device_name(buf->device_name, sizeof(buf->device_name), client_id);
  snprintf(buf->topic, sizeof(buf->topic), "%s/sensor/%s/%s/config", HA_DISCOVERY_PREFIX, client_id, object_id);

  memset(buf->unit_part, 0, sizeof(buf->unit_part));
  memset(buf->class_part, 0, sizeof(buf->class_part));
  if (unit && strlen(unit) != 0) {
    snprintf(buf->unit_part, sizeof(buf->unit_part), ",\"unit_of_measurement\":\"%s\"", unit);
  }
  if (device_class && strlen(device_class) != 0) {
    snprintf(buf->class_part, sizeof(buf->class_part), ",\"device_class\":\"%s\"", device_class);
  }

  snprintf(buf->payload, sizeof(buf->payload),
      "{\"name\":\"%s\",\"unique_id\":\"%s_%s\",\"state_topic\":\"%s\","
      "\"availability_topic\":\"%s\",\"value_template\":\"%s\"%s%s,"
      "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\","
      "\"manufacturer\":\"SuperGreenLab\",\"model\":\"Automation Controller V3\"}}",
      name,
      client_id,
      object_id,
      buf->state_topic,
      buf->availability_topic,
      value_template,
      buf->unit_part,
      buf->class_part,
      client_id,
      buf->device_name);
  int result = mqtt_publish_message(buf->topic, buf->payload, 1);
  mqtt_buffers_release();
  return result;
}

static int mqtt_publish_ha_binary_config(
    const char *client_id,
    const char *object_id,
    const char *name,
    const char *value_template,
    const char *device_class) {
  mqtt_buffer_pool_t *buf = mqtt_buffers_acquire();
  if (!buf) return -1;

  build_ha_state_topic(buf->state_topic, sizeof(buf->state_topic), client_id);
  build_ha_availability_topic(buf->availability_topic, sizeof(buf->availability_topic), client_id);
  build_ha_device_name(buf->device_name, sizeof(buf->device_name), client_id);
  snprintf(buf->topic, sizeof(buf->topic), "%s/binary_sensor/%s/%s/config", HA_DISCOVERY_PREFIX, client_id, object_id);

  memset(buf->class_part, 0, sizeof(buf->class_part));
  if (device_class && strlen(device_class) != 0) {
    snprintf(buf->class_part, sizeof(buf->class_part), ",\"device_class\":\"%s\"", device_class);
  }

  snprintf(buf->payload, sizeof(buf->payload),
      "{\"name\":\"%s\",\"unique_id\":\"%s_%s\",\"state_topic\":\"%s\","
      "\"availability_topic\":\"%s\",\"value_template\":\"%s\","
      "\"payload_on\":\"ON\",\"payload_off\":\"OFF\"%s,"
      "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\","
      "\"manufacturer\":\"SuperGreenLab\",\"model\":\"Automation Controller V3\"}}",
      name,
      client_id,
      object_id,
      buf->state_topic,
      buf->availability_topic,
      value_template,
      buf->class_part,
      client_id,
      buf->device_name);
  int result = mqtt_publish_message(buf->topic, buf->payload, 1);
  mqtt_buffers_release();
  return result;
}

static int mqtt_publish_ha_switch_config(
    const char *client_id,
    const char *object_id,
    const char *name,
    const char *command,
    const char *value_template) {
  mqtt_buffer_pool_t *buf = mqtt_buffers_acquire();
  if (!buf) return -1;

  build_ha_state_topic(buf->state_topic, sizeof(buf->state_topic), client_id);
  build_ha_availability_topic(buf->availability_topic, sizeof(buf->availability_topic), client_id);
  build_ha_command_topic(buf->command_topic, sizeof(buf->command_topic), client_id, command);
  build_ha_device_name(buf->device_name, sizeof(buf->device_name), client_id);
  snprintf(buf->topic, sizeof(buf->topic), "%s/switch/%s/%s/config", HA_DISCOVERY_PREFIX, client_id, object_id);

  snprintf(buf->payload, sizeof(buf->payload),
      "{\"name\":\"%s\",\"unique_id\":\"%s_%s\",\"state_topic\":\"%s\","
      "\"availability_topic\":\"%s\",\"command_topic\":\"%s\","
      "\"value_template\":\"%s\",\"payload_on\":\"ON\",\"payload_off\":\"OFF\","
      "\"state_on\":\"ON\",\"state_off\":\"OFF\","
      "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\","
      "\"manufacturer\":\"SuperGreenLab\",\"model\":\"Automation Controller V3\"}}",
      name,
      client_id,
      object_id,
      buf->state_topic,
      buf->availability_topic,
      buf->command_topic,
      value_template,
      client_id,
      buf->device_name);
  int result = mqtt_publish_message(buf->topic, buf->payload, 1);
  mqtt_buffers_release();
  return result;
}

static int mqtt_publish_ha_number_config(
    const char *client_id,
    const char *object_id,
    const char *name,
    const char *command,
    const char *value_template,
    int min_value,
    int max_value,
    int step,
    const char *unit) {
  mqtt_buffer_pool_t *buf = mqtt_buffers_acquire();
  if (!buf) return -1;

  build_ha_state_topic(buf->state_topic, sizeof(buf->state_topic), client_id);
  build_ha_availability_topic(buf->availability_topic, sizeof(buf->availability_topic), client_id);
  build_ha_command_topic(buf->command_topic, sizeof(buf->command_topic), client_id, command);
  build_ha_device_name(buf->device_name, sizeof(buf->device_name), client_id);
  snprintf(buf->topic, sizeof(buf->topic), "%s/number/%s/%s/config", HA_DISCOVERY_PREFIX, client_id, object_id);

  memset(buf->unit_part, 0, sizeof(buf->unit_part));
  if (unit && strlen(unit) != 0) {
    snprintf(buf->unit_part, sizeof(buf->unit_part), ",\"unit_of_measurement\":\"%s\"", unit);
  }

  snprintf(buf->payload, sizeof(buf->payload),
      "{\"name\":\"%s\",\"unique_id\":\"%s_%s\",\"state_topic\":\"%s\","
      "\"availability_topic\":\"%s\",\"command_topic\":\"%s\","
      "\"value_template\":\"%s\",\"min\":%d,\"max\":%d,\"step\":%d,"
      "\"mode\":\"box\"%s,"
      "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\","
      "\"manufacturer\":\"SuperGreenLab\",\"model\":\"Automation Controller V3\"}}",
      name,
      client_id,
      object_id,
      buf->state_topic,
      buf->availability_topic,
      buf->command_topic,
      value_template,
      min_value,
      max_value,
      step,
      buf->unit_part,
      client_id,
      buf->device_name);
  int result = mqtt_publish_message(buf->topic, buf->payload, 1);
  mqtt_buffers_release();
  return result;
}

static int mqtt_publish_ha_button_config(
    const char *client_id,
    const char *object_id,
    const char *name,
    const char *command) {
  char topic[160] = {0};
  char payload[640] = {0};
  char availability_topic[MAX_KVALUE_SIZE] = {0};
  char command_topic[MAX_KVALUE_SIZE] = {0};
  char device_name[64] = {0};

  build_ha_availability_topic(availability_topic, sizeof(availability_topic), client_id);
  build_ha_command_topic(command_topic, sizeof(command_topic), client_id, command);
  build_ha_device_name(device_name, sizeof(device_name), client_id);
  snprintf(topic, sizeof(topic), "%s/button/%s/%s/config", HA_DISCOVERY_PREFIX, client_id, object_id);

  snprintf(payload, sizeof(payload),
      "{\"name\":\"%s\",\"unique_id\":\"%s_%s\",\"availability_topic\":\"%s\","
      "\"command_topic\":\"%s\",\"payload_press\":\"PRESS\","
      "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\","
      "\"manufacturer\":\"SuperGreenLab\",\"model\":\"Automation Controller V3\"}}",
      name,
      client_id,
      object_id,
      availability_topic,
      command_topic,
      client_id,
      device_name);
  return mqtt_publish_message(topic, payload, 1);
}

static bool mqtt_discovery_step(int32_t idx, int msg_id, bool do_pause) {
  mqtt_diag_set_discovery_idx(idx);
  if (!connected || client == NULL || msg_id < 0) {
    return false;
  }
  if (do_pause) {
    mqtt_discovery_pause();
  }
  return true;
}

static bool mqtt_publish_ha_discovery(const char *client_id) {
  mqtt_diag_set_stage(MQTT_DIAG_STAGE_DISCOVERY_START);
  mqtt_diag_set_discovery_idx(0);

  if (!mqtt_discovery_step(1, mqtt_publish_ha_config(client_id, "box_0_temp", "Box 1 Temperature", "{{ value_json.box_0_temp }}", "°C", "temperature"), true)) return false;
  if (!mqtt_discovery_step(2, mqtt_publish_ha_config(client_id, "box_0_humi", "Box 1 Humidity", "{{ value_json.box_0_humi }}", "%", "humidity"), true)) return false;
  if (!mqtt_discovery_step(3, mqtt_publish_ha_config(client_id, "box_0_vpd", "Box 1 VPD", "{{ value_json.box_0_vpd }}", "kPa", ""), true)) return false;
  if (!mqtt_discovery_step(4, mqtt_publish_ha_config(client_id, "box_0_co2", "Box 1 CO2", "{{ value_json.box_0_co2 }}", "ppm", "carbon_dioxide"), true)) return false;
  if (!mqtt_discovery_step(5, mqtt_publish_ha_config(client_id, "box_1_temp", "Box 2 Temperature", "{{ value_json.box_1_temp }}", "°C", "temperature"), true)) return false;
  if (!mqtt_discovery_step(6, mqtt_publish_ha_config(client_id, "box_1_humi", "Box 2 Humidity", "{{ value_json.box_1_humi }}", "%", "humidity"), true)) return false;
  if (!mqtt_discovery_step(7, mqtt_publish_ha_config(client_id, "box_1_vpd", "Box 2 VPD", "{{ value_json.box_1_vpd }}", "kPa", ""), true)) return false;
  if (!mqtt_discovery_step(8, mqtt_publish_ha_config(client_id, "box_1_co2", "Box 2 CO2", "{{ value_json.box_1_co2 }}", "ppm", "carbon_dioxide"), true)) return false;
  if (!mqtt_discovery_step(9, mqtt_publish_ha_config(client_id, "box_2_temp", "Box 3 Temperature", "{{ value_json.box_2_temp }}", "°C", "temperature"), true)) return false;
  if (!mqtt_discovery_step(10, mqtt_publish_ha_config(client_id, "box_2_humi", "Box 3 Humidity", "{{ value_json.box_2_humi }}", "%", "humidity"), true)) return false;
  if (!mqtt_discovery_step(11, mqtt_publish_ha_config(client_id, "box_2_vpd", "Box 3 VPD", "{{ value_json.box_2_vpd }}", "kPa", ""), true)) return false;
  if (!mqtt_discovery_step(12, mqtt_publish_ha_config(client_id, "box_2_co2", "Box 3 CO2", "{{ value_json.box_2_co2 }}", "ppm", "carbon_dioxide"), true)) return false;
  if (!mqtt_discovery_step(13, mqtt_publish_ha_config(client_id, "sensor_health_status", "Sensor Health Status", "{{ value_json.sensor_health_status }}", "", ""), true)) return false;
  if (!mqtt_discovery_step(14, mqtt_publish_ha_config(client_id, "sensor_health_status_text", "Sensor Health Status Text", "{{ value_json.sensor_health_status_text }}", "", ""), true)) return false;
  if (!mqtt_discovery_step(15, mqtt_publish_ha_config(client_id, "sensor_health_last_alert", "Sensor Health Last Alert", "{{ value_json.sensor_health_last_alert }}", "", ""), true)) return false;
  if (!mqtt_discovery_step(16, mqtt_publish_ha_binary_config(client_id, "sensor_health_problem", "Sensor Health Problem", "{{ value_json.sensor_health_problem }}", "problem"), true)) return false;
  if (!mqtt_discovery_step(17, mqtt_publish_ha_binary_config(client_id, "box_0_sensor_problem", "Box 1 Sensor Problem", "{{ value_json.box_0_sensor_problem }}", "problem"), true)) return false;
  if (!mqtt_discovery_step(18, mqtt_publish_ha_binary_config(client_id, "box_1_sensor_problem", "Box 2 Sensor Problem", "{{ value_json.box_1_sensor_problem }}", "problem"), true)) return false;
  if (!mqtt_discovery_step(19, mqtt_publish_ha_binary_config(client_id, "box_2_sensor_problem", "Box 3 Sensor Problem", "{{ value_json.box_2_sensor_problem }}", "problem"), true)) return false;
  if (!mqtt_discovery_step(20, mqtt_publish_ha_switch_config(client_id, "sensor_health_enabled", "Sensor Health Enabled", "sensor_health_enabled", "{{ value_json.sensor_health_enabled }}"), true)) return false;
  if (!mqtt_discovery_step(21, mqtt_publish_ha_number_config(client_id, "sensor_health_period_s", "Sensor Health Period", "sensor_health_period_s", "{{ value_json.sensor_health_period_s }}", 5, 3600, 1, "s"), true)) return false;
  if (!mqtt_discovery_step(22, mqtt_publish_ha_button_config(client_id, "reboot", "Reboot", "reboot"), true)) return false;
  if (!mqtt_discovery_step(23, mqtt_publish_ha_button_config(client_id, "ota_start", "Start OTA", "ota_start"), false)) return false;

  mqtt_diag_set_stage(MQTT_DIAG_STAGE_DISCOVERY_DONE);
  return true;
}

void mqtt_publish_ha_state() {
  char client_id[MAX_KVALUE_SIZE] = {0};
  mqtt_buffer_pool_t *buf = mqtt_buffers_acquire();
  if (!buf) return;

  int sensor_health_status = 0;
  const char *sensor_health_status_text = NULL;
  const char *sensor_health_problem = NULL;
  const char *box_0_sensor_problem = NULL;
  const char *box_1_sensor_problem = NULL;
  const char *box_2_sensor_problem = NULL;

  if (!connected || client == NULL) {
    mqtt_buffers_release();
    return;
  }

  get_broker_clientid(client_id, sizeof(client_id) - 1);
  if (strlen(client_id) == 0) {
    mqtt_buffers_release();
    return;
  }

  get_sensor_health_last_alert(buf->alert, sizeof(buf->alert) - 1);
  sensor_health_status = get_sensor_health_status();
  sensor_health_status_text = sensor_health_status_to_text(sensor_health_status);
  sensor_health_problem = sensor_health_problem_to_text(sensor_health_status);
  box_0_sensor_problem = box_sensor_problem_to_text(buf->alert, 0);
  box_1_sensor_problem = box_sensor_problem_to_text(buf->alert, 1);
  box_2_sensor_problem = box_sensor_problem_to_text(buf->alert, 2);
  build_ha_state_topic(buf->topic, sizeof(buf->topic), client_id);
  snprintf(buf->payload, sizeof(buf->payload),
      "{\"box_0_temp\":%d,\"box_0_humi\":%d,\"box_0_vpd\":%.2f,"
      "\"box_0_co2\":%ld,\"box_1_temp\":%d,\"box_1_humi\":%d,"
      "\"box_1_vpd\":%.2f,\"box_1_co2\":%ld,\"box_2_temp\":%d,"
      "\"box_2_humi\":%d,\"box_2_vpd\":%.2f,\"box_2_co2\":%ld,"
      "\"sensor_health_status\":%d,\"sensor_health_status_text\":\"%s\","
      "\"sensor_health_problem\":\"%s\","
      "\"box_0_sensor_problem\":\"%s\","
      "\"box_1_sensor_problem\":\"%s\","
      "\"box_2_sensor_problem\":\"%s\","
      "\"sensor_health_enabled\":\"%s\","
      "\"sensor_health_period_s\":%u,"
      "\"sensor_health_last_alert\":\"%s\"}",
      get_box_0_temp(),
      get_box_0_humi(),
      (float)get_box_0_vpd() / 100.0f,
      (long)get_box_0_co2(),
      get_box_1_temp(),
      get_box_1_humi(),
      (float)get_box_1_vpd() / 100.0f,
      (long)get_box_1_co2(),
      get_box_2_temp(),
      get_box_2_humi(),
      (float)get_box_2_vpd() / 100.0f,
      (long)get_box_2_co2(),
      sensor_health_status,
      sensor_health_status_text,
      sensor_health_problem,
      box_0_sensor_problem,
      box_1_sensor_problem,
      box_2_sensor_problem,
      get_sensor_health_enabled() ? "ON" : "OFF",
      (unsigned int)get_sensor_health_period_s(),
      buf->alert);
  mqtt_publish_message(buf->topic, buf->payload, 1);
  mqtt_buffers_release();
}

static mqtt_state_diag_mode_t mqtt_state_diag_mode_from_client_id(const char *client_id) {
  if (client_id == NULL) {
    return MQTT_STATE_DIAG_NONE;
  }
  size_t n = strlen(client_id);
  if (n < 4) {
    return MQTT_STATE_DIAG_NONE;
  }
  const char *suffix = client_id + n - 4;
  if (strcmp(suffix, "-s4a") == 0) {
    return MQTT_STATE_DIAG_S4A;
  }
  if (strcmp(suffix, "-s4b") == 0) {
    return MQTT_STATE_DIAG_S4B;
  }
  if (strcmp(suffix, "-s4c") == 0) {
    return MQTT_STATE_DIAG_S4C;
  }
  if (strcmp(suffix, "-s4d") == 0) {
    return MQTT_STATE_DIAG_S4D;
  }
  return MQTT_STATE_DIAG_NONE;
}

static void mqtt_publish_ha_state_diag(mqtt_state_diag_mode_t mode, const char *client_id) {
  mqtt_buffer_pool_t *buf = mqtt_buffers_acquire();
  if (!buf) return;

  int sensor_health_status = 0;
  const char *sensor_health_status_text = NULL;
  const char *sensor_health_problem = NULL;
  const char *box_0_sensor_problem = NULL;
  const char *box_1_sensor_problem = NULL;
  const char *box_2_sensor_problem = NULL;

  if (!connected || client == NULL || client_id == NULL || strlen(client_id) == 0) {
    mqtt_buffers_release();
    return;
  }

  build_ha_state_topic(buf->topic, sizeof(buf->topic), client_id);
  if (mode == MQTT_STATE_DIAG_S4A) {
    snprintf(buf->payload, sizeof(buf->payload), "{\"state\":%d}", get_state());
  } else if (mode == MQTT_STATE_DIAG_S4B) {
    snprintf(buf->payload, sizeof(buf->payload),
        "{\"state\":%d,\"box_0_temp\":%d}",
        get_state(),
        get_box_0_temp());
  } else if (mode == MQTT_STATE_DIAG_S4C) {
    snprintf(buf->payload, sizeof(buf->payload),
        "{\"state\":%d,\"box_0_temp\":%d,\"box_0_humi\":%d,\"box_0_vpd\":%.2f,"
        "\"box_0_co2\":%ld,\"box_1_temp\":%d,\"box_1_humi\":%d,"
        "\"box_1_vpd\":%.2f,\"box_1_co2\":%ld,\"box_2_temp\":%d,"
        "\"box_2_humi\":%d,\"box_2_vpd\":%.2f,\"box_2_co2\":%ld}",
        get_state(),
        get_box_0_temp(),
        get_box_0_humi(),
        (float)get_box_0_vpd() / 100.0f,
        (long)get_box_0_co2(),
        get_box_1_temp(),
        get_box_1_humi(),
        (float)get_box_1_vpd() / 100.0f,
        (long)get_box_1_co2(),
        get_box_2_temp(),
        get_box_2_humi(),
        (float)get_box_2_vpd() / 100.0f,
        (long)get_box_2_co2());
  } else {
    get_sensor_health_last_alert(buf->alert, sizeof(buf->alert) - 1);
    sensor_health_status = get_sensor_health_status();
    sensor_health_status_text = sensor_health_status_to_text(sensor_health_status);
    sensor_health_problem = sensor_health_problem_to_text(sensor_health_status);
    box_0_sensor_problem = box_sensor_problem_to_text(buf->alert, 0);
    box_1_sensor_problem = box_sensor_problem_to_text(buf->alert, 1);
    box_2_sensor_problem = box_sensor_problem_to_text(buf->alert, 2);
    snprintf(buf->payload, sizeof(buf->payload),
        "{\"state\":%d,\"box_0_temp\":%d,\"box_0_humi\":%d,\"box_0_vpd\":%.2f,"
        "\"box_0_co2\":%ld,\"box_1_temp\":%d,\"box_1_humi\":%d,"
        "\"box_1_vpd\":%.2f,\"box_1_co2\":%ld,\"box_2_temp\":%d,"
        "\"box_2_humi\":%d,\"box_2_vpd\":%.2f,\"box_2_co2\":%ld,"
        "\"sensor_health_status\":%d,\"sensor_health_status_text\":\"%s\","
        "\"sensor_health_problem\":\"%s\","
        "\"box_0_sensor_problem\":\"%s\","
        "\"box_1_sensor_problem\":\"%s\","
        "\"box_2_sensor_problem\":\"%s\","
        "\"sensor_health_enabled\":\"%s\","
        "\"sensor_health_period_s\":%u,"
        "\"sensor_health_last_alert\":\"%s\"}",
        get_state(),
        get_box_0_temp(),
        get_box_0_humi(),
        (float)get_box_0_vpd() / 100.0f,
        (long)get_box_0_co2(),
        get_box_1_temp(),
        get_box_1_humi(),
        (float)get_box_1_vpd() / 100.0f,
        (long)get_box_1_co2(),
        get_box_2_temp(),
        get_box_2_humi(),
        (float)get_box_2_vpd() / 100.0f,
        (long)get_box_2_co2(),
        sensor_health_status,
        sensor_health_status_text,
        sensor_health_problem,
        box_0_sensor_problem,
        box_1_sensor_problem,
        box_2_sensor_problem,
        get_sensor_health_enabled() ? "ON" : "OFF",
        (unsigned int)get_sensor_health_period_s(),
        buf->alert);
  }
  mqtt_publish_message(buf->topic, buf->payload, 1);
  mqtt_buffers_release();
}





static void subscribe_cmd() {
  char cmd_channel[MAX_KVALUE_SIZE] = {0};
  char client_id[MAX_KVALUE_SIZE] = {0};
  char topic[MAX_KVALUE_SIZE] = {0};
  int cmd_channel_len = 0;
  get_broker_clientid(client_id, sizeof(client_id) - 1);
  cmd_channel_len = snprintf(cmd_channel, sizeof(cmd_channel), "%s.cmd", client_id);
  if (cmd_channel_len < 0 || cmd_channel_len >= (int)sizeof(cmd_channel)) {
    ESP_LOGE(SGO_LOG_NOSEND, "@MQTT subscribe_cmd channel truncated for client_id");
    return;
  }

  ESP_LOGI(SGO_LOG_NOSEND, "@MQTT subscribe_cmd %s", cmd_channel);
  esp_mqtt_client_subscribe(client, cmd_channel, 2);
  build_ha_command_topic(topic, sizeof(topic), client_id, "reboot");
  esp_mqtt_client_subscribe(client, topic, 1);
  build_ha_command_topic(topic, sizeof(topic), client_id, "ota_start");
  esp_mqtt_client_subscribe(client, topic, 1);
  build_ha_command_topic(topic, sizeof(topic), client_id, "sensor_health_enabled");
  esp_mqtt_client_subscribe(client, topic, 1);
  build_ha_command_topic(topic, sizeof(topic), client_id, "sensor_health_period_s");
  esp_mqtt_client_subscribe(client, topic, 1);
}

static void mqtt_request_state_publish() {
  if (cmd == NULL || xQueueSend(cmd, &CMD_MQTT_PUBLISH_STATE, 0) != pdTRUE) {
    ESP_LOGW(SGO_LOG_NOSEND, "@MQTT state publish request dropped (queue full)");
  }
}

static void parse_ha_command(esp_mqtt_event_handle_t event) {
  char topic[MAX_KVALUE_SIZE] = {0};
  char payload[64] = {0};
  char client_id[MAX_KVALUE_SIZE] = {0};
  char expected[MAX_KVALUE_SIZE] = {0};

  if (event->topic_len <= 0 || event->topic_len >= MAX_KVALUE_SIZE || event->data_len >= (int)sizeof(payload)) {
    return;
  }

  memcpy(topic, event->topic, event->topic_len);
  topic[event->topic_len] = 0;
  memcpy(payload, event->data, event->data_len);
  payload[event->data_len] = 0;

  get_broker_clientid(client_id, sizeof(client_id) - 1);

  build_ha_command_topic(expected, sizeof(expected), client_id, "reboot");
  if (strcmp(topic, expected) == 0) {
    if (strcmp(payload, "PRESS") == 0) {
      ESP_LOGI(SGO_LOG_NOSEND, "@MQTT Reboot requested via Home Assistant");
      reboot_esp();
    }
    return;
  }

  build_ha_command_topic(expected, sizeof(expected), client_id, "ota_start");
  if (strcmp(topic, expected) == 0) {
    if (strcmp(payload, "PRESS") == 0) {
      int v = request_ota_start(1);
      set_ota_start(v);
    }
    return;
  }

  build_ha_command_topic(expected, sizeof(expected), client_id, "sensor_health_enabled");
  if (strcmp(topic, expected) == 0) {
    if (strcmp(payload, "ON") == 0 || strcmp(payload, "1") == 0 || strcmp(payload, "true") == 0) {
      set_sensor_health_enabled(1);
    } else if (strcmp(payload, "OFF") == 0 || strcmp(payload, "0") == 0 || strcmp(payload, "false") == 0) {
      set_sensor_health_enabled(0);
    }
    mqtt_request_state_publish();
    return;
  }

  build_ha_command_topic(expected, sizeof(expected), client_id, "sensor_health_period_s");
  if (strcmp(topic, expected) == 0) {
    int period = atoi(payload);
    if (period >= 5 && period <= 3600) {
      set_sensor_health_period_s((uint16_t)period);
      mqtt_request_state_publish();
    }
  }
}

static void parse_cmd(esp_mqtt_event_handle_t event) {
  if (event->data_len > MAX_REMOTE_CMD_LENGTH + 65) {
    ESP_LOGI(SGO_LOG_NOSEND, "@MQTT Remote command string can't be larger that %d with signature", MAX_REMOTE_CMD_LENGTH + 65);
    return;
  }
  if (event->data_len < 66) {
    ESP_LOGI(SGO_LOG_NOSEND, "@MQTT Remote command disabled: missing signing key");
    return;
  }
  if (hasstr(SIGNING_KEY)) {
    char signingKey[33] = {0};
    getstr(SIGNING_KEY, signingKey, 33);
    char hash[65] = {0};
    char cmd[MAX_REMOTE_CMD_LENGTH + 1] = {0};
    size_t hash_len = event->data_len < 64 ? (size_t)event->data_len : 64;
    memcpy(hash, event->data, hash_len);
    hash[hash_len] = 0;

    size_t cmd_len = (size_t)event->data_len - 65;
    if (cmd_len > MAX_REMOTE_CMD_LENGTH) {
      cmd_len = MAX_REMOTE_CMD_LENGTH;
    }
    memcpy(cmd, &(event->data[65]), cmd_len);
    cmd[cmd_len] = 0;

    char cmdSeeded[MAX_REMOTE_CMD_LENGTH + 33 + 1] = {0};
    uint8_t localHashBin[32] = {0};
    snprintf(cmdSeeded, sizeof(cmdSeeded), "%s:%s", signingKey, cmd);

    //ESP_LOGI(SGO_LOG_NOSEND, "@MQTT Hash: %s - Cmd: %s", hash, cmd);
    mbedtls_sha256_context sha256_ctx;
    mbedtls_sha256_init(&sha256_ctx);
    mbedtls_sha256_starts_ret(&sha256_ctx, false);
    mbedtls_sha256_update_ret(&sha256_ctx, (uint8_t *)cmdSeeded, strlen(cmdSeeded));
    mbedtls_sha256_finish_ret(&sha256_ctx, localHashBin);

    char localHash[65] = {0};
    sodium_bin2hex(localHash, sizeof(localHash), localHashBin, sizeof(localHashBin));
    if (strncmp(localHash, hash, 64) != 0) {
      ESP_LOGI(SGO_LOG_NOSEND, "@MQTT Command signing check failed.");
      return;
    }

    execute_cmd(event->data_len - 65, cmd, true);
  } else {
    ESP_LOGI(SGO_LOG_NOSEND, "@MQTT Remote command disabled: missing signign key");
  }
}





static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event) {
  switch (event->event_id) {
    case MQTT_EVENT_BEFORE_CONNECT:
      ESP_LOGI(SGO_LOG_NOSEND, "@MQTT MQTT_EVENT_BEFORE_CONNECT");
      break;
    case MQTT_EVENT_CONNECTED:
      ESP_LOGI(SGO_LOG_NOSEND, "@MQTT MQTT_EVENT_CONNECTED");
      connected = true;
      if (cmd == NULL) {
        ESP_LOGE(SGO_LOG_NOSEND, "@MQTT CMD_MQTT_CONNECTED queue is NULL");
      } else if (xQueueSend(cmd, &CMD_MQTT_CONNECTED, 0) != pdTRUE) {
        ESP_LOGE(SGO_LOG_NOSEND, "@MQTT CMD_MQTT_CONNECTED enqueue failed");
      }
      break;
    case MQTT_EVENT_DISCONNECTED:
      ESP_LOGI(SGO_LOG_NOSEND, "@MQTT MQTT_EVENT_DISCONNECTED");
      connected = false;
      break;
    case MQTT_EVENT_SUBSCRIBED:
      ESP_LOGI(SGO_LOG_NOSEND, "@MQTT MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
      break;
    case MQTT_EVENT_UNSUBSCRIBED:
      ESP_LOGI(SGO_LOG_NOSEND, "@MQTT MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
      break;
    case MQTT_EVENT_PUBLISHED:
      ESP_LOGI(SGO_LOG_NOSEND, "@MQTT MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
      break;
    case MQTT_EVENT_DATA:
      ESP_LOGI(SGO_LOG_NOSEND, "@MQTT MQTT_EVENT_DATA");

      parse_ha_command(event);

      

      
        if (event->topic_len > 3 && !strncmp(&(event->topic[event->topic_len-3]), "cmd", 3)) {
          parse_cmd(event);
        }
      

      break;
    case MQTT_EVENT_ERROR:
      ESP_LOGI(SGO_LOG_NOSEND, "@MQTT MQTT_EVENT_ERROR");
      break;
  }
  return ESP_OK;
}

static void mqtt_publish_diag(const char *client_id) {
  char topic[MAX_KVALUE_SIZE] = {0};
  char payload[384] = {0};
  nvs_stats_t nvs_stats = {0};
  if (nvs_get_stats(NULL, &nvs_stats) != ESP_OK) {
    nvs_stats.used_entries = 0;
    nvs_stats.free_entries = 0;
  }
  time_t now_s = 0;
  time(&now_s);
  int time_valid = now_s >= 1500000000 ? 1 : 0;
  snprintf(topic, sizeof(topic), "supergreen/%s/diag", client_id);
  snprintf(payload, sizeof(payload),
      "{\"n_restarts\":%d,\"ota_ts\":%ld,\"heap_free\":%lu,\"heap_min_free\":%lu,"
      "\"heap_min_free_at\":%ld,\"heap_low_events\":%d,"
      "\"uptime_s\":%lu,\"state\":%d,\"box_0_enabled\":%d,\"reset_reason\":%d,"
      "\"nvs_used\":%u,\"nvs_free\":%u,\"mqtt_stack_hwm\":%ld,\"time_valid\":%d}",
      (int)get_n_restarts(),
      (long)get_ota_timestamp(),
      (unsigned long)esp_get_free_heap_size(),
      (unsigned long)esp_get_minimum_free_heap_size(),
      get_heap_min_free_at(),
      get_heap_low_events(),
      (unsigned long)(xTaskGetTickCount() * portTICK_PERIOD_MS / 1000),
      (int)get_state(),
      (int)get_box_0_enabled(),
      (int)esp_reset_reason(),
      (unsigned int)nvs_stats.used_entries,
      (unsigned int)nvs_stats.free_entries,
      (long)mqtt_stack_hwm,
      time_valid);
  mqtt_publish_message(topic, payload, 0);
}

static void mqtt_task(void *param) {
  int c;
  bool first_connect = true;
  bool was_connected = false;
  TickType_t last_ha_publish = 0;
  TickType_t last_diag_publish = 0;

  uint64_t _chipmacid;
  esp_efuse_mac_get_default((uint8_t*) (&_chipmacid));

  char client_id[MAX_KVALUE_SIZE] = {0};
  get_broker_clientid(client_id, sizeof(client_id) - 1);
  if (strlen(client_id) == 0) {
    snprintf(client_id, sizeof(client_id)-1, "%llx", _chipmacid);
    set_broker_clientid(client_id);
  } else if (strlen(client_id) == 1 && client_id[0] == '-') {
    client_id[0] = 0;
  }
  ESP_LOGI(SGO_LOG_NOSEND, "@MQTT Log clientid: %s", client_id);
  mqtt_state_diag_mode_t state_diag_mode = mqtt_state_diag_mode_from_client_id(client_id);
  ESP_LOGI(SGO_LOG_NOSEND, "@MQTT State diag mode=%d", (int)state_diag_mode);
  mqtt_diag_set_stage(MQTT_DIAG_STAGE_IDLE);
  mqtt_diag_set_discovery_idx(-1);

  

  char broker_url[MAX_KVALUE_SIZE] = {0};
  char ha_availability_topic[MAX_KVALUE_SIZE] = {0};
  getstr(BROKER_URL, broker_url, sizeof(broker_url)-1);
  build_ha_availability_topic(ha_availability_topic, sizeof(ha_availability_topic), client_id);
  esp_mqtt_client_config_t mqtt_cfg = {
    .uri = broker_url,
    .event_handle = mqtt_event_handler,
    .client_id = client_id,
    .lwt_topic = ha_availability_topic,
    .lwt_msg = "offline",
    .lwt_qos = 0,
    .lwt_retain = 1,
    // .user_context = (void *)your_context
  };

  vTaskDelay(1000 / portTICK_PERIOD_MS); // Looks like we have a race confition with wifi

  client = esp_mqtt_client_init(&mqtt_cfg);
  esp_mqtt_client_start(client);

  // Subscribe to the task watchdog: a future bug that blocks this task
  // (network stall, deadlock) now panics and reboots instead of silently
  // wedging until someone notices and power-cycles the device.
  esp_task_wdt_add(NULL);



  while(true) {
    esp_task_wdt_reset();
    mqtt_stack_hwm = (int32_t)uxTaskGetStackHighWaterMark(NULL);
    if (xQueueReceive(cmd, &c, 10000 / portTICK_PERIOD_MS)) {
      if (c == CMD_MQTT_CONNECTED) {
        mqtt_diag_set_stage(MQTT_DIAG_STAGE_CONNECTED);

        
          subscribe_cmd();
        

        

        mqtt_diag_set_stage(MQTT_DIAG_STAGE_SUBSCRIBED);
        mqtt_publish_ha_availability(client_id, "online");
        mqtt_diag_set_stage(MQTT_DIAG_STAGE_AVAILABILITY);
        if (state_diag_mode == MQTT_STATE_DIAG_NONE && first_connect) {
          ESP_LOGI(SGO_LOG_NOSEND, "@MQTT First connect");
          vTaskDelay(pdMS_TO_TICKS(MQTT_DISCOVERY_CONNECT_COOLDOWN_MS));
          if (!(connected && client != NULL)) {
            ESP_LOGW(SGO_LOG_NOSEND, "@MQTT Skip discovery: disconnected before first-connect discovery");
          } else if (mqtt_publish_ha_discovery(client_id)) {
            first_connect = false;
          } else {
            mqtt_diag_set_stage(MQTT_DIAG_STAGE_DISCOVERY_FAIL);
            ESP_LOGW(SGO_LOG_NOSEND, "@MQTT Discovery publish failed, will retry on next connect");
          }
        }
        if (state_diag_mode == MQTT_STATE_DIAG_NONE) {
          mqtt_publish_ha_state();
          mqtt_diag_set_stage(MQTT_DIAG_STAGE_STATE_ON_CONNECT);
          last_ha_publish = xTaskGetTickCount();
          mqtt_publish_diag(client_id);
          last_diag_publish = xTaskGetTickCount();
        } else {
          mqtt_publish_ha_state_diag(state_diag_mode, client_id);
          mqtt_diag_set_stage(MQTT_DIAG_STAGE_STATE_ON_CONNECT);
        }
      } else if (c == CMD_MQTT_PUBLISH_STATE) {
        // requested from the esp-mqtt event callback (HA command): publish from
        // this task instead of from inside the client's own event dispatch
        if (connected && state_diag_mode == MQTT_STATE_DIAG_NONE) {
          mqtt_publish_ha_state();
          last_ha_publish = xTaskGetTickCount();
        }
      } 
    }
    if (was_connected && !connected) {
      mqtt_diag_set_stage(MQTT_DIAG_STAGE_DISCONNECTED);
    }
    was_connected = connected;
    if (connected) {
      if (state_diag_mode == MQTT_STATE_DIAG_NONE &&
          (xTaskGetTickCount() - last_ha_publish) >= pdMS_TO_TICKS(HA_STATE_PUBLISH_PERIOD_MS)) {
        mqtt_publish_ha_state();
        last_ha_publish = xTaskGetTickCount();
      }

      if (state_diag_mode == MQTT_STATE_DIAG_NONE &&
          (xTaskGetTickCount() - last_diag_publish) >= pdMS_TO_TICKS(DIAG_PUBLISH_PERIOD_MS)) {
        mqtt_publish_diag(client_id);
        last_diag_publish = xTaskGetTickCount();
      }

      
    }
  }
}

static int mqtt_logging_vprintf(const char *str, va_list l) {
  return vprintf(str, l);
}



void mqtt_intercept_log() {
  // the 3.2 KB log queue that used to be created here was never read
  esp_log_set_vprintf(mqtt_logging_vprintf);
}

void init_mqtt() {
  ESP_LOGI(SGO_LOG_NOSEND, "@MQTT Intializing MQTT task");

  //esp_log_level_set("MQTT_CLIENT", ESP_LOG_NONE);

  cmd = xQueueCreate(10, sizeof(int));
  if (cmd == NULL) {
    ESP_LOGE(SGO_LOG_NOSEND, "@MQTT Unable to create mqtt queue");
  }

  

  BaseType_t ret = xTaskCreatePinnedToCore(mqtt_task, "MQTT", 16384, NULL, 10, NULL, 1);
  if (ret != pdPASS) {
    ESP_LOGE(SGO_LOG_NOSEND, "@MQTT Failed to create task");
  }
}


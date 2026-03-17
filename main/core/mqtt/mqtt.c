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
#include "esp_log.h"
#include "sodium/utils.h"
#include "mbedtls/sha256.h"

#include "../log/log.h"
#include "../kv/kv.h"
#include "../wifi/wifi.h"



bool connected = false;


#include "../cmd/cmd.h"




#define MAX_REMOTE_CMD_LENGTH MAX_CMD_LENGTH-10 // keeps some space for the -r true parameter

static esp_mqtt_client_handle_t client;

static QueueHandle_t cmd;
static QueueHandle_t log_queue;

static int CMD_MQTT_CONNECTED = 1;
static int CMD_MQTT_FORCE_FLUSH = 2;

#define HA_TOPIC_PREFIX "supergreen"
#define HA_DISCOVERY_PREFIX "homeassistant"
#define HA_STATE_PUBLISH_PERIOD_MS (30 * 1000)

#define MAX_LOG_QUEUE_ITEMS 25
static uint8_t buf_out[MAX_QUEUE_ITEM_SIZE] = {0};

static void build_ha_device_name(char *dest, size_t len, const char *client_id) {
  snprintf(dest, len, "SuperGreen %s", client_id);
}

static void build_ha_state_topic(char *dest, size_t len, const char *client_id) {
  snprintf(dest, len, "%s/%s/state", HA_TOPIC_PREFIX, client_id);
}

static void build_ha_availability_topic(char *dest, size_t len, const char *client_id) {
  snprintf(dest, len, "%s/%s/availability", HA_TOPIC_PREFIX, client_id);
}

static void mqtt_publish_message(const char *topic, const char *payload, int retain) {
  if (!connected || client == NULL || strlen(topic) == 0) {
    return;
  }
  esp_mqtt_client_publish(client, topic, payload, 0, 0, retain);
}

static void mqtt_publish_ha_availability(const char *client_id, const char *state) {
  char topic[MAX_KVALUE_SIZE] = {0};
  build_ha_availability_topic(topic, sizeof(topic), client_id);
  mqtt_publish_message(topic, state, 1);
}

static void mqtt_publish_ha_config(
    const char *client_id,
    const char *object_id,
    const char *name,
    const char *value_template,
    const char *unit,
    const char *device_class) {
  char topic[160] = {0};
  char payload[640] = {0};
  char state_topic[MAX_KVALUE_SIZE] = {0};
  char availability_topic[MAX_KVALUE_SIZE] = {0};
  char device_name[64] = {0};
  char unit_part[64] = {0};
  char class_part[64] = {0};

  build_ha_state_topic(state_topic, sizeof(state_topic), client_id);
  build_ha_availability_topic(availability_topic, sizeof(availability_topic), client_id);
  build_ha_device_name(device_name, sizeof(device_name), client_id);
  snprintf(topic, sizeof(topic), "%s/sensor/%s/%s/config", HA_DISCOVERY_PREFIX, client_id, object_id);

  if (unit && strlen(unit) != 0) {
    snprintf(unit_part, sizeof(unit_part), ",\"unit_of_measurement\":\"%s\"", unit);
  }
  if (device_class && strlen(device_class) != 0) {
    snprintf(class_part, sizeof(class_part), ",\"device_class\":\"%s\"", device_class);
  }

  snprintf(payload, sizeof(payload),
      "{\"name\":\"%s\",\"unique_id\":\"%s_%s\",\"state_topic\":\"%s\","
      "\"availability_topic\":\"%s\",\"value_template\":\"%s\"%s%s,"
      "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\","
      "\"manufacturer\":\"SuperGreenLab\",\"model\":\"Automation Controller V3\"}}",
      name,
      client_id,
      object_id,
      state_topic,
      availability_topic,
      value_template,
      unit_part,
      class_part,
      client_id,
      device_name);
  mqtt_publish_message(topic, payload, 1);
}

static void mqtt_publish_ha_discovery(const char *client_id) {
  mqtt_publish_ha_config(client_id, "box_0_temp", "Box 1 Temperature", "{{ value_json.box_0_temp }}", "°C", "temperature");
  mqtt_publish_ha_config(client_id, "box_0_humi", "Box 1 Humidity", "{{ value_json.box_0_humi }}", "%", "humidity");
  mqtt_publish_ha_config(client_id, "box_0_vpd", "Box 1 VPD", "{{ value_json.box_0_vpd }}", "kPa", "");
  mqtt_publish_ha_config(client_id, "box_0_co2", "Box 1 CO2", "{{ value_json.box_0_co2 }}", "ppm", "carbon_dioxide");
  mqtt_publish_ha_config(client_id, "sensor_health_status", "Sensor Health Status", "{{ value_json.sensor_health_status }}", "", "");
  mqtt_publish_ha_config(client_id, "sensor_health_last_alert", "Sensor Health Last Alert", "{{ value_json.sensor_health_last_alert }}", "", "");
}

void mqtt_publish_ha_state() {
  char client_id[MAX_KVALUE_SIZE] = {0};
  char topic[MAX_KVALUE_SIZE] = {0};
  char payload[320] = {0};
  char alert[MAX_KVALUE_SIZE] = {0};

  if (!connected || client == NULL) {
    return;
  }

  get_broker_clientid(client_id, sizeof(client_id) - 1);
  if (strlen(client_id) == 0) {
    return;
  }

  get_sensor_health_last_alert(alert, sizeof(alert) - 1);
  build_ha_state_topic(topic, sizeof(topic), client_id);
  snprintf(payload, sizeof(payload),
      "{\"box_0_temp\":%d,\"box_0_humi\":%d,\"box_0_vpd\":%.2f,"
      "\"box_0_co2\":%ld,\"sensor_health_status\":%d,"
      "\"sensor_health_last_alert\":\"%s\"}",
      get_box_0_temp(),
      get_box_0_humi(),
      (float)get_box_0_vpd() / 100.0f,
      (long)get_box_0_co2(),
      get_sensor_health_status(),
      alert);
  mqtt_publish_message(topic, payload, 1);
}





static void subscribe_cmd() {
  char cmd_channel[MAX_KVALUE_SIZE] = {0};
  char client_id[MAX_KVALUE_SIZE] = {0};
  get_broker_clientid(client_id, sizeof(client_id) - 1);
  sprintf(cmd_channel, "%s.cmd", client_id);

  ESP_LOGI(SGO_LOG_NOSEND, "@MQTT subscribe_cmd %s", cmd_channel);
  esp_mqtt_client_subscribe(client, cmd_channel, 2);
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
    strncpy(hash, event->data, 64);
    char cmd[MAX_REMOTE_CMD_LENGTH + 1] = {0};
    strncpy(cmd, &(event->data[65]), event->data_len - 65);

    char cmdSeeded[MAX_REMOTE_CMD_LENGTH + 33 + 1] = {0};
    uint8_t localHashBin[32] = {0};
    sprintf(cmdSeeded, "%s:%s", signingKey, cmd);

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
      xQueueSend(cmd, &CMD_MQTT_CONNECTED, 0);
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

static void mqtt_task(void *param) {
  int c;
  bool first_connect = true;
  TickType_t last_ha_publish = 0;

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

  char log_channel[MAX_KVALUE_SIZE] = {0};
  get_broker_channel(log_channel, sizeof(log_channel) - 1);
  if (strlen(log_channel) == 0) {
    snprintf(log_channel, sizeof(log_channel)-1, "%s.log", client_id);
    set_broker_channel(log_channel);
  }
  ESP_LOGI(SGO_LOG_NOSEND, "@MQTT Log channel: %s", log_channel);

  

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



  while(true) {
    if (xQueueReceive(cmd, &c, 10000 / portTICK_PERIOD_MS)) {
      if (c == CMD_MQTT_CONNECTED) {

        
          subscribe_cmd();
        

        

        if (first_connect) {
          first_connect = false;
          ESP_LOGI(SGO_LOG_NOSEND, "@MQTT First connect");
        }

        mqtt_publish_ha_discovery(client_id);
        mqtt_publish_ha_availability(client_id, "online");
        mqtt_publish_ha_state();
        last_ha_publish = xTaskGetTickCount();
      } 
    }
    if (connected) {
      memset(buf_out, 0, MAX_QUEUE_ITEM_SIZE);
      while (xQueueReceive(log_queue, buf_out, 0)) {
        esp_mqtt_client_publish(client, log_channel, (char *)buf_out, 0, 0, 0);
        memset(buf_out, 0, MAX_QUEUE_ITEM_SIZE);
      }

      if ((xTaskGetTickCount() - last_ha_publish) >= pdMS_TO_TICKS(HA_STATE_PUBLISH_PERIOD_MS)) {
        mqtt_publish_ha_state();
        last_ha_publish = xTaskGetTickCount();
      }

      
    }
  }
}

static int mqtt_logging_vprintf(const char *str, va_list l) {
  if (strlen(str) <= 9+7 ||
      (strncmp("I (%d) %s", &(str[7]), 9) != 0 &&
       strncmp("W (%d) %s", &(str[7]), 9) != 0 &&
       strncmp("E (%d) %s", &(str[7]), 9) != 0)) {
    return vprintf(str, l); 
  }
  int totalsize = vsnprintf(NULL, 0, str, l);
  if (totalsize >= MAX_QUEUE_ITEM_SIZE - 1) {
    return vprintf(str, l);
  }
  
  va_list nl;
  va_copy(nl, l);
  va_arg(nl, int);
  const char *tag = va_arg(nl, const char *);
  if (strcmp(tag, SGO_LOG_MSG) != 0 &&
      strcmp(tag, SGO_LOG_EVENT) != 0 &&
      strcmp(tag, SGO_LOG_METRIC) != 0) {
    return vprintf(str, l);
  }

  uint8_t buf_in[MAX_QUEUE_ITEM_SIZE] = {0};
  if (uxQueueMessagesWaiting(log_queue) >= MAX_LOG_QUEUE_ITEMS) {
    xQueueReceive(log_queue, buf_in, 0);
  }
  memset(buf_in, 0, MAX_QUEUE_ITEM_SIZE);
  int len = vsnprintf((char*)buf_in, MAX_QUEUE_ITEM_SIZE-1, str, l);
  buf_in[len] = 0;
  xQueueSend(log_queue, buf_in, 0);
  if (cmd/* && uxQueueMessagesWaiting(log_queue) > 5*/) {
    xQueueSend(cmd, &CMD_MQTT_FORCE_FLUSH, 0);
  }
  return vprintf(str, l);
}



void mqtt_intercept_log() {
  log_queue = xQueueCreate(MAX_LOG_QUEUE_ITEMS, MAX_QUEUE_ITEM_SIZE);
  if (log_queue == NULL) {
    ESP_LOGE(SGO_LOG_NOSEND, "@MQTT Unable to create mqtt log queue");
  }

  esp_log_set_vprintf(mqtt_logging_vprintf);
}

void init_mqtt() {
  ESP_LOGI(SGO_LOG_NOSEND, "@MQTT Intializing MQTT task");

  //esp_log_level_set("MQTT_CLIENT", ESP_LOG_NONE);

  cmd = xQueueCreate(10, sizeof(int));
  if (cmd == NULL) {
    ESP_LOGE(SGO_LOG_NOSEND, "@MQTT Unable to create mqtt queue");
  }

  

  BaseType_t ret = xTaskCreatePinnedToCore(mqtt_task, "MQTT", 8192, NULL, 10, NULL, 1);
  if (ret != pdPASS) {
    ESP_LOGE(SGO_LOG_NOSEND, "@MQTT Failed to create task");
  }
}

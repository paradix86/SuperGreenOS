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

/*
 * This is mostly the esp-idf OTA example from 6 months back,
 * and is a lot copy-pasted for the htmlapp ota.
 */

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <stdbool.h>

#include "ota.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_ota_ops.h"

#include "../log/log.h"
#include "../wifi/wifi.h"
#include "../kv/kv.h"

#define BUFFSIZE 1024
#define TEXT_BUFFSIZE 1024
#define OTA_RECV_TIMEOUT_S 5

#define OTA_BUILD_TIMESTAMP_BCK "O_B_T_BCK"

static char ota_write_data[BUFFSIZE + 1] = { 0 };
/*an packet receive buffer*/
static char text[BUFFSIZE + 1] = { 0 };
/* an image total length*/
static int binary_file_length = 0;
/*socket id*/
static int socket_id = -1;

static QueueHandle_t cmd;

typedef enum {
  OTA_VERSION_CHECK_ERROR = -1,
  OTA_VERSION_CHECK_UP_TO_DATE = 0,
  OTA_VERSION_CHECK_UPDATE_AVAILABLE = 1,
} ota_version_check_result;

static bool set_socket_recv_timeout(int sock, int timeout_s) {
  struct timeval tv = {
    .tv_sec = timeout_s,
    .tv_usec = 0,
  };
  if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0) {
    ESP_LOGE(SGO_LOG_NOSEND, "@OTA Failed to set recv timeout! errno=%d", errno);
    return false;
  }
  return true;
}

/*read buffer by byte still delim ,return read bytes counts*/
static int read_until(char *buffer, char delim, int len)
{
  //  /*TODO: delim check,buffer check,further: do an buffer length limited*/
  int i = 0;
  while (buffer[i] != delim && i < len) {
    ++i;
  }
  return i + 1;
}

/* resolve a packet from http socket
 * return true if packet including \r\n\r\n that means http packet header finished,start to receive packet body
 * otherwise return false
 * */
static bool read_past_http_header(char text[], int total_len, esp_ota_handle_t update_handle)
{
  /* i means current position */
  int i = 0, i_read_len = 0;
  while (text[i] != 0 && i < total_len) {
    i_read_len = read_until(&text[i], '\n', total_len);
    // if we resolve \r\n line,we think packet header is finished
    if (i_read_len == 2) {
      int i_write_len = total_len - (i + 2);
      memset(ota_write_data, 0, BUFFSIZE);
      /*copy first http packet body to write buffer*/
      memcpy(ota_write_data, &(text[i + 2]), i_write_len);

      esp_err_t err = esp_ota_write( update_handle, (const void *)ota_write_data, i_write_len);
      if (err != ESP_OK) {
        ESP_LOGE(SGO_LOG_NOSEND, "@OTA Error: esp_ota_write failed (%s)!", esp_err_to_name(err));
        return false;
      } else {
        ESP_LOGI(SGO_LOG_NOSEND, "@OTA esp_ota_write header OK");
        binary_file_length += i_write_len;
      }
      return true;
    }
    i += i_read_len;
    vTaskDelay(1 / portTICK_PERIOD_MS);
  }
  return false;
}

static bool connect_to_http_server()
{
  ESP_LOGI(SGO_LOG_NOSEND, "@OTA connect_to_http_server reached");
  char server_ip[20] = {0}; get_ota_server_ip(server_ip, 20);
  int16_t port = get_ota_server_port();
  ESP_LOGI(SGO_LOG_NOSEND, "@OTA Server IP: %s Server Port: %d", server_ip, port);

  int  http_connect_flag = -1;
  struct sockaddr_in sock_info;

  socket_id = socket(AF_INET, SOCK_STREAM, 0);
  if (socket_id == -1) {
    ESP_LOGE(SGO_LOG_NOSEND, "@OTA Create socket failed!");
    return false;
  }

  // set connect info
  memset(&sock_info, 0, sizeof(struct sockaddr_in));
  sock_info.sin_family = AF_INET;
  sock_info.sin_addr.s_addr = inet_addr(server_ip);
  sock_info.sin_port = htons(port);

  // connect to http server
  http_connect_flag = connect(socket_id, (struct sockaddr *)&sock_info, sizeof(sock_info));
  if (http_connect_flag == -1) {
    ESP_LOGE(SGO_LOG_NOSEND, "@OTA Connect to server failed! errno=%d", errno);
    close(socket_id);
    return false;
  } else {
    ESP_LOGI(SGO_LOG_NOSEND, "@OTA Connected to server");
    return true;
  }
 return false;
}

static ota_version_check_result check_new_version(char *new_timestamp, int len) {
  char hostname[128] = {0}; get_ota_server_hostname(hostname, 128);
  int16_t port = get_ota_server_port();
  char basedir[128] = {0}; get_ota_basedir(basedir, 128);
  ESP_LOGI(SGO_LOG_NOSEND, "@OTA check_new_version start host=%s port=%d basedir=%s", hostname, port, basedir);
  /*connect to http server*/
  if (connect_to_http_server()) {
    ESP_LOGI(SGO_LOG_NOSEND, "@OTA Connected to http server");
  } else {
    ESP_LOGE(SGO_LOG_NOSEND, "@OTA Connect to http server failed!");
    close(socket_id);
    return OTA_VERSION_CHECK_ERROR;
  }

  if (!set_socket_recv_timeout(socket_id, OTA_RECV_TIMEOUT_S)) {
    close(socket_id);
    return OTA_VERSION_CHECK_ERROR;
  }

  /*send GET request to http server*/
  const char *GET_FORMAT =
    "GET %s/last_timestamp HTTP/1.0\r\n"
    "Host: %s:%d\r\n"
    "User-Agent: esp-idf/1.0 esp32\r\n\r\n";

  char *http_request = NULL;
  int get_len = asprintf(&http_request, GET_FORMAT, basedir, hostname, port);
  if (get_len < 0) {
    ESP_LOGE(SGO_LOG_NOSEND, "@OTA Failed to allocate memory for GET request buffer");
    close(socket_id);
    return OTA_VERSION_CHECK_ERROR;
  }
  ESP_LOGI(SGO_LOG_NOSEND, "@OTA Attempting GET %s/last_timestamp", basedir);
  int res = send(socket_id, http_request, get_len, 0);
  free(http_request);

  if (res < 0) {
    ESP_LOGE(SGO_LOG_NOSEND, "@OTA Send GET request to server failed");
    close(socket_id);
    return OTA_VERSION_CHECK_ERROR;
  } else {
    ESP_LOGI(SGO_LOG_NOSEND, "@OTA Send GET request to server succeeded");
  }

  int i = 0;
  char buf;
  while (i < 4) {
    int header_res = recv(socket_id, &buf, 1, 0);
    if (header_res != 1) {
      ESP_LOGE(SGO_LOG_NOSEND, "@OTA Failed while reading timestamp header errno=%d", errno);
      close(socket_id);
      return OTA_VERSION_CHECK_ERROR;
    }
    if (buf == '\n' || buf == '\r') ++i;
    else i = 0;
  }

  ESP_LOGI(SGO_LOG_NOSEND, "@OTA Skipped http header");

  int ota_build_timestamp = get_ota_timestamp();
  char timestamp[15] = {0};
  size_t timestamp_len = 0;
  while (timestamp_len < sizeof(timestamp) - 1) {
    int read_len = recv(socket_id, &timestamp[timestamp_len], (sizeof(timestamp) - 1) - timestamp_len, 0);
    if (read_len < 0) {
      ESP_LOGE(SGO_LOG_NOSEND, "@OTA Failed while reading timestamp body errno=%d", errno);
      close(socket_id);
      return OTA_VERSION_CHECK_ERROR;
    }
    if (read_len == 0) {
      break;
    }
    timestamp_len += read_len;
  }
  timestamp[timestamp_len] = '\0';
  close(socket_id);

  char *start = timestamp;
  while (*start && isspace((unsigned char)*start)) {
    ++start;
  }
  char *end = start + strlen(start);
  while (end > start && isspace((unsigned char)*(end - 1))) {
    --end;
    *end = '\0';
  }

  if (*start == '\0') {
    ESP_LOGE(SGO_LOG_NOSEND, "@OTA Empty OTA timestamp response");
    return OTA_VERSION_CHECK_ERROR;
  }

  char *parse_end = NULL;
  long parsed_timestamp = strtol(start, &parse_end, 10);
  if (*parse_end != '\0') {
    ESP_LOGE(SGO_LOG_NOSEND, "@OTA Malformed OTA timestamp response: '%s'", start);
    return OTA_VERSION_CHECK_ERROR;
  }

  int itimestamp = (int)parsed_timestamp;
  ESP_LOGI(SGO_LOG_NOSEND, "@OTA OTA TIMESTAMP: %d (build: %d)", itimestamp, ota_build_timestamp);
  snprintf(new_timestamp, len, "%d", itimestamp);
  if (ota_build_timestamp < itimestamp) {
    return OTA_VERSION_CHECK_UPDATE_AVAILABLE;
  }
  return OTA_VERSION_CHECK_UP_TO_DATE;
}

static void try_ota(const char *new_timestamp)
{
  char server_ip[20] = {0}; get_ota_server_ip(server_ip, 20);
  char hostname[128] = {0}; get_ota_server_hostname(hostname, 128);
  int16_t port = get_ota_server_port();
  char basedir[128] = {0}; get_ota_basedir(basedir, 128);

  esp_err_t err;
  /* update handle : set by esp_ota_begin(), must be freed via esp_ota_end() */
  esp_ota_handle_t update_handle = 0 ;
  const esp_partition_t *update_partition = NULL;

  binary_file_length = 0;
  ESP_LOGI(SGO_LOG_NOSEND, "@OTA Starting OTA");

  const esp_partition_t *configured = esp_ota_get_boot_partition();
  const esp_partition_t *running = esp_ota_get_running_partition();

  if (configured != running) {
    ESP_LOGW(SGO_LOG_NOSEND, "@OTA Configured OTA boot partition at offset 0x%08x, but running from offset 0x%08x",
        configured->address, running->address);
    ESP_LOGW(SGO_LOG_NOSEND, "@OTA (This can happen if either the OTA boot data or preferred boot image become corrupted somehow.)");
  }
  ESP_LOGI(SGO_LOG_NOSEND, "@OTA Running partition type %d subtype %d (offset 0x%08x)",
      running->type, running->subtype, running->address);

  /*connect to http server*/
  if (connect_to_http_server()) {
    ESP_LOGI(SGO_LOG_NOSEND, "@OTA Connected to http server");
  } else {
    ESP_LOGE(SGO_LOG_NOSEND, "@OTA Connect to http server failed!");
    close(socket_id);
    return;
  }
  // Without a receive timeout a download that stalls on a silent connection
  // blocks recv() forever, and with it ota_task and every later OTA request.
  if (!set_socket_recv_timeout(socket_id, OTA_RECV_TIMEOUT_S)) {
    ESP_LOGE(SGO_LOG_NOSEND, "@OTA Failed to set download socket timeout");
    close(socket_id);
    return;
  }

  /*send GET request to http server*/
  const char *GET_FORMAT =
    "GET %s/%s/firmware.bin HTTP/1.0\r\n"
    "Host: %s:%d\r\n"
    "User-Agent: esp-idf/1.0 esp32\r\n\r\n";

  char *http_request = NULL;
  int get_len = asprintf(&http_request, GET_FORMAT, basedir, new_timestamp, hostname, port);
  if (get_len < 0) {
    ESP_LOGE(SGO_LOG_NOSEND, "@OTA Failed to allocate memory for GET request buffer");
    close(socket_id);
    return;
  }

  int res = send(socket_id, http_request, get_len, 0);
  free(http_request);

  if (res < 0) {
    ESP_LOGE(SGO_LOG_NOSEND, "@OTA Send GET request to server failed");
    close(socket_id);
    return;
  } else {
    ESP_LOGI(SGO_LOG_NOSEND, "@OTA Send GET request to server succeeded");
  }

  update_partition = esp_ota_get_next_update_partition(NULL);
  ESP_LOGI(SGO_LOG_NOSEND, "@OTA Writing to partition subtype %d at offset 0x%x",
      update_partition->subtype, update_partition->address);
  assert(update_partition != NULL);

  err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
  if (err != ESP_OK) {
    ESP_LOGE(SGO_LOG_NOSEND, "@OTA esp_ota_begin failed (%s)", esp_err_to_name(err));
    close(socket_id);
    return;
  }
  ESP_LOGI(SGO_LOG_NOSEND, "@OTA esp_ota_begin succeeded");

  bool resp_body_start = false, socket_flag = true, http_200_flag = false;
  /*deal with all receive packet*/
  while (socket_flag) {
    memset(text, 0, TEXT_BUFFSIZE);
    memset(ota_write_data, 0, BUFFSIZE);
    int buff_len = recv(socket_id, text, TEXT_BUFFSIZE, 0);
    if (buff_len < 0) { /*receive error*/
      ESP_LOGE(SGO_LOG_NOSEND, "@OTA Error: receive data error! errno=%d", errno);
      esp_ota_end(update_handle);
      close(socket_id);
      return;
    } else if (buff_len > 0 && !resp_body_start) {  /*deal with response header*/
      // only start ota when server response 200 state code
      if (strstr(text, "200") == NULL && !http_200_flag) {
        ESP_LOGE(SGO_LOG_NOSEND, "@OTA ota url is invalid or bin does not exist");
        esp_ota_end(update_handle);
        close(socket_id);
        return;
      }
      http_200_flag = true;
      memcpy(ota_write_data, text, buff_len);
      resp_body_start = read_past_http_header(text, buff_len, update_handle);
    } else if (buff_len > 0 && resp_body_start) { /*deal with response body*/
      memcpy(ota_write_data, text, buff_len);
      err = esp_ota_write( update_handle, (const void *)ota_write_data, buff_len);
      if (err != ESP_OK) {
        ESP_LOGE(SGO_LOG_NOSEND, "@OTA Error: esp_ota_write failed (%s)!", esp_err_to_name(err));
        esp_ota_end(update_handle);
        close(socket_id);
        return;
      }
      binary_file_length += buff_len;
      ESP_LOGI(SGO_LOG_NOSEND, "@OTA Have written image length %d", binary_file_length);
    } else if (buff_len == 0) {  /*packet over*/
      socket_flag = false;
      ESP_LOGI(SGO_LOG_NOSEND, "@OTA Connection closed, all packets received");
      close(socket_id);
    } else {
      ESP_LOGE(SGO_LOG_NOSEND, "@OTA Unexpected recv result");
    }
    vTaskDelay(1 / portTICK_PERIOD_MS);
  }

  ESP_LOGI(SGO_LOG_NOSEND, "@OTA Total Write binary data length : %d", binary_file_length);

  if (esp_ota_end(update_handle) != ESP_OK) {
    ESP_LOGE(SGO_LOG_NOSEND, "@OTA esp_ota_end failed!");
    close(socket_id);
    return;
  }
  err = esp_ota_set_boot_partition(update_partition);
  if (err != ESP_OK) {
    ESP_LOGE(SGO_LOG_NOSEND, "@OTA esp_ota_set_boot_partition failed (%s)!", esp_err_to_name(err));
    close(socket_id);
    return;
  }
  ESP_LOGI(SGO_LOG_NOSEND, "@OTA Prepare to restart system!");
  esp_restart();
  return;
}

// True from the moment a request is queued until ota_task has handled it. Without
// it a second OTA_START (double click, HA retry) was queued silently and ran right
// after the first one finished, with OTA_START already back to 0.
static volatile bool ota_request_pending = false;

static void ota_task(void *pvParameter) {
  uint8_t c;

  while (true) {
    ESP_LOGI(SGO_LOG_NOSEND, "@OTA Waiting for OTA command");
    while(!xQueueReceive(cmd, &c, portMAX_DELAY));
    ESP_LOGI(SGO_LOG_NOSEND, "@OTA Dequeued OTA command c=%u", (unsigned int)c);

    int ota_build_timestamp = get_ota_timestamp();
    if (ota_build_timestamp == 0) {
      ESP_LOGI(SGO_LOG_NOSEND, "@OTA OTA NOT STARTING timestamp=%d", ota_build_timestamp);
      set_ota_status(OTA_STATUS_DISABLED);
    } else { 
      ESP_LOGI(SGO_LOG_NOSEND, "@OTA Checking firmware update available");
      ESP_LOGI(SGO_LOG_NOSEND, "@OTA timestamp=%d", ota_build_timestamp);
      char new_timestamp[15] = {0};
      ota_version_check_result check_result = check_new_version(new_timestamp, sizeof(new_timestamp)-1);
      if (check_result == OTA_VERSION_CHECK_UPDATE_AVAILABLE) {
        ESP_LOGI(SGO_LOG_NOSEND, "@OTA Start OTA procedure");
        set_ota_status(OTA_STATUS_IN_PROGRESS);
        try_ota(new_timestamp);
        // try_ota() only returns when the update did not complete: on success it restarts.
        ESP_LOGE(SGO_LOG_NOSEND, "@OTA Update failed, see previous errors");
        set_ota_status(OTA_STATUS_FAILED);
      } else if (check_result == OTA_VERSION_CHECK_UP_TO_DATE) {
        ESP_LOGI(SGO_LOG_NOSEND, "@OTA Firmware is up-to-date");
        set_ota_status(OTA_STATUS_IDLE);
      } else {
        ESP_LOGE(SGO_LOG_NOSEND, "@OTA Firmware check failed (network/response error)");
        set_ota_status(OTA_STATUS_FAILED);
      }
    }

    // The request is edge-triggered: release OTA_START so clients can tell
    // "handled" (0 + status) from "still queued" (1) and can trigger again.
    ota_request_pending = false;
    set_ota_start(0);
    ESP_LOGI(SGO_LOG_NOSEND, "@OTA Request handled, status=%d", get_ota_status());
  }
}

void init_ota() {
  ESP_LOGI(SGO_LOG_EVENT, "@OTA OTA_BUILD_TIMESTAMP=%d", OTA_BUILD_TIMESTAMP);
  if (hasi32(OTA_BUILD_TIMESTAMP_BCK)) {
    int ota_build_timestamp_bck = geti32(OTA_BUILD_TIMESTAMP_BCK);
    ESP_LOGI(SGO_LOG_NOSEND, "@OTA ota_build_timestamp_bck = %d", ota_build_timestamp_bck);
    if (ota_build_timestamp_bck != OTA_BUILD_TIMESTAMP) {
      ESP_LOGI(SGO_LOG_NOSEND, "@OTA OTA build update detected, OTA_BUILD_TIMESTAMP_BCK=%d OTA_BUILD_TIMESTAMP=%d", ota_build_timestamp_bck, OTA_BUILD_TIMESTAMP);
      set_ota_timestamp(OTA_BUILD_TIMESTAMP);
    }
  }
  seti32(OTA_BUILD_TIMESTAMP_BCK, OTA_BUILD_TIMESTAMP);

  cmd = xQueueCreate(10, sizeof(uint8_t));
  if (cmd == NULL) {
    ESP_LOGE(SGO_LOG_NOSEND, "@OTA Unable to create cmd queue");
  }

  int ota_build_timestamp = get_ota_timestamp();
  ESP_LOGI(SGO_LOG_NOSEND, "@OTA OTA initialization timestamp=%d", ota_build_timestamp);

  BaseType_t ret = xTaskCreatePinnedToCore(ota_task, "OTA", 8192, NULL, 5, NULL, 1);
  if (ret != pdPASS) {
    ESP_LOGE(SGO_LOG_NOSEND, "@OTA Failed to create task");
  }
}

/* KV Callbacks */

int request_ota_start(int value) {
  if (value != 1) {
    ESP_LOGI(SGO_LOG_NOSEND, "@OTA request_ota_start ignored non-start value=%d", value);
    return value;
  }

  if (cmd == NULL) {
    ESP_LOGE(SGO_LOG_NOSEND, "@OTA request_ota_start queue is NULL");
    return 0;
  }

  if (ota_request_pending) {
    ESP_LOGW(SGO_LOG_NOSEND, "@OTA request_ota_start ignored, previous request still being handled (status=%d)", get_ota_status());
    return 1;
  }
  ota_request_pending = true;

  uint8_t cmd_data = 1;
  UBaseType_t queue_space = uxQueueSpacesAvailable(cmd);
  ESP_LOGI(SGO_LOG_NOSEND, "@OTA request_ota_start enqueue attempt queue_space=%u", (unsigned int)queue_space);
  if (xQueueSend(cmd, &cmd_data, pdMS_TO_TICKS(100)) != pdTRUE) {
    ESP_LOGE(SGO_LOG_NOSEND, "@OTA request_ota_start enqueue failed");
    ota_request_pending = false;
    return 0;
  }

  ESP_LOGI(SGO_LOG_NOSEND, "@OTA request_ota_start dispatched");
  return 1;
}

int on_set_ota_start(int value) {
  return request_ota_start(value);
}

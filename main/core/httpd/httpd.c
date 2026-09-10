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

#include "httpd.h"
#include "../kv/kv_mapping.h"

#include <stdlib.h>

#include <esp_http_server.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <nvs.h>
#include <esp_spiffs.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <stdarg.h>
#include <string.h>

#include "../kv/kv.h"
#include "../log/log.h"
#include "../modules.h"
#include "../mqtt/mqtt.h"
#include "../reboot/reboot.h"

#define IS_URI_SEP(c) (c == '?' || c == '&' || c == '=')
#define MQTT_DIAG_KEY_STAGE "MQTT_STG"
#define MQTT_DIAG_KEY_DISC_IDX "MQTT_DIDX"


esp_err_t download_get_handler(httpd_req_t *req);
esp_err_t upload_post_handler(httpd_req_t *req);
esp_err_t delete_post_handler(httpd_req_t *req);

/* static size_t get_char_count(const char *uri) {
  size_t i = 0;
  for (; uri[i] && !IS_URI_SEP(uri[i]); ++i) {}
  return i;
} */

static const char *move_to_next_elem(const char *uri) {
  for (; *uri && !IS_URI_SEP(*uri); ++uri) {}
  if (*uri) ++uri;
  return uri;
}

static const char *move_to_key_value(const char *uri, const char *name) {
  while (*(uri = move_to_next_elem(uri))) {
    if (strncmp(uri, name, strlen(name)) == 0) {
      break;
    }
  }
  if (!*uri) return uri;
  return move_to_next_elem(uri);
}

inline int ishex(int x)
{
  return	(x >= '0' && x <= '9')	||
    (x >= 'a' && x <= 'f')	||
    (x >= 'A' && x <= 'F');
}

int url_decode(const char *s, char *dec)
{
  char *o;
  const char *end = s + strlen(s);
  int c;

  for (o = dec; s <= end; o++) {
    c = *s++;
    if (c == '+') c = ' ';
    else if (c == '%') {
      // A request cut short right after '%' (e.g. "...&v=abc%") must not read
      // past the NUL terminator of the caller's buffer: check the two hex
      // digits are actually there before consuming them.
      if (end - s < 2 || !ishex(s[0]) || !ishex(s[1]) || !sscanf(s, "%2x", &c)) {
        return -1;
      }
      s += 2;
    }

    if (dec) *o = c;
  }

  return o - dec;
}

/*static int find_int_param(const char *uri, const char *name) {
  int res = 0;
  const char *uri_offset = move_to_key_value(uri, name);
  if (!*uri_offset) {
    return 0;
  }
  return res;
}*/

static void find_str_param(const char *uri, const char *name, char *out, size_t *len) {
  uri = move_to_key_value(uri, name);
  if (!*uri) {
    return;
  }
  int i = 0;
  for (i = 0; uri[i] && i < (*len-1) && !IS_URI_SEP(uri[i]); ++i) {
    out[i] = uri[i];
  }
  *len = i;
}

static esp_err_t geti_handler(httpd_req_t *req) {
  if (auth_request(req) == false) {
    return 0;
  }
  size_t len = 50;
  char name[50] = {0};
  find_str_param(req->uri, "k", name, &len);
  const kvi8_mapping *hi8 = get_kvi8_mapping(name, false);
  const kvui8_mapping *hui8 = get_kvui8_mapping(name, false);
  const kvi16_mapping *hi16 = get_kvi16_mapping(name, false);
  const kvui16_mapping *hui16 = get_kvui16_mapping(name, false);
  const kvi32_mapping *hi32 = get_kvi32_mapping(name, false);
  const kvui32_mapping *hui32 = get_kvui32_mapping(name, false);

  if (!hi8 && !hui8 && !hi16 && !hui16 && !hi32 && !hui32) {
    return httpd_resp_send_404(req);
  }

  int v = 0;
  if (hi8 && hi8->getter) {
    v = hi8->getter();
  } else if (hui8 && hui8->getter) {
    v = hui8->getter();
  } else if (hi16 && hi16->getter) {
    v = hi16->getter();
  } else if (hui16 && hui16->getter) {
    v = hui16->getter();
  } else if (hi32 && hi32->getter) {
    v = hi32->getter();
  } else if (hui32 && hui32->getter) {
    v = hui32->getter();
  }
  char ret[12] = {0};
  snprintf(ret, sizeof(ret) - 1, "%d", v);

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_send(req, ret, strlen(ret));
  return ESP_OK;
}

static esp_err_t seti_handler(httpd_req_t *req) {
  if (auth_request(req) == false) {
    return 0;
  }
  size_t len = 50;
  char name[50] = {0};
  find_str_param(req->uri, "k", name, &len);
  const kvi8_mapping *hi8 = get_kvi8_mapping(name, false);
  bool is_i8 = hi8 && hi8->setter;
  const kvui8_mapping *hui8 = get_kvui8_mapping(name, false);
  bool is_ui8 = hui8 && hui8->setter;
  const kvi16_mapping *hi16 = get_kvi16_mapping(name, false);
  bool is_i16 = hi16 && hi16->setter;
  const kvui16_mapping *hui16 = get_kvui16_mapping(name, false);
  bool is_ui16 = hui16 && hui16->setter;
  const kvi32_mapping *hi32 = get_kvi32_mapping(name, false);
  bool is_i32 = hi32 && hi32->setter;
  const kvui32_mapping *hui32 = get_kvui32_mapping(name, false);
  bool is_ui32 = hui32 && hui32->setter;

  if (!is_i8 && !is_ui8 && !is_i16 && !is_ui16 && !is_i32 && !is_ui32) {
    return httpd_resp_send_404(req);
  }

  len = 50;
  char value[50] = {0};
  find_str_param(req->uri, "v", value, &len);
  int res = atoi(value);

  if (is_i8) {
    hi8->setter((int8_t)res);
  } else if (is_ui8) {
    hui8->setter((uint8_t)res);
  } else if (is_i16) {
    hi16->setter((int16_t)res);
  } else if (is_ui16) {
    hui16->setter((uint16_t)res);
  } else if (is_i32) {
    hi32->setter((int32_t)res);
  } else if (is_ui32) {
    hui32->setter((uint32_t)res);
  }
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_send(req, "OK", 2);
  return ESP_OK;
}

static esp_err_t getstr_handler(httpd_req_t *req) {
  if (auth_request(req) == false) {
    return 0;
  }
  char name[50] = {0};
  size_t len = 50;
  find_str_param(req->uri, "k", name, &len);

  const kvs_mapping *h = get_kvs_mapping(name, false);
  if (!h) {
    return httpd_resp_send_404(req);
  }

  char v[MAX_KVALUE_SIZE] = {0};
  if (h->getter) {
    h->getter(v, MAX_KVALUE_SIZE - 1);
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_send(req, v, strlen(v));
  return ESP_OK;
}

static esp_err_t setstr_handler(httpd_req_t *req) {
  if (auth_request(req) == false) {
    return 0;
  }
  char name[50] = {0};
  size_t len = 50;
  find_str_param(req->uri, "k", name, &len);

  const kvs_mapping *h = get_kvs_mapping(name, false);
  if (!h || !h->setter) {
    return httpd_resp_send_404(req);
  }

  len = MAX_KVALUE_SIZE;
  char raw[MAX_KVALUE_SIZE] = {0};
  char value[MAX_KVALUE_SIZE] = {0};
  find_str_param(req->uri, "v", raw, &len);
  url_decode(raw, value);

  h->setter(value);
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_send(req, "OK", 2);
  return ESP_OK;
}

static esp_err_t setsigningkey_handler(httpd_req_t *req) {
  if (auth_request(req) == false) {
    return 0;
  }
  size_t len = 33;
  char key[33] = {0};
  find_str_param(req->uri, "key", key, &len);

  setstr(SIGNING_KEY, key);

  httpd_resp_send(req, "OK", 2);

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return ESP_OK;
}

static esp_err_t option_handler(httpd_req_t *req) {
  if (auth_request(req) == false) {
    return 0;
  }
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET,POST,DELETE,OPTIONS");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type,Access-Control-Allow-Origin");
  httpd_resp_send(req, "OK", 2);
  return ESP_OK;
}

static esp_err_t get_ip_handler(httpd_req_t *req) {
  if (auth_request(req) == false) {
    return 0;
  }
  int socket = httpd_req_to_sockfd(req);

  struct sockaddr_in6 destAddr;
  unsigned socklen=sizeof(destAddr);

  if(getpeername(socket, (struct sockaddr *)&destAddr, &socklen)<0) {
    return httpd_resp_send_500(req);
  }
  char ip[20] = {0};
  uint8_t *iphex = (uint8_t *)&(destAddr.sin6_addr.un.u32_addr[3]);
  snprintf(ip, sizeof(ip)-1, "%d.%d.%d.%d", iphex[0], iphex[1], iphex[2], iphex[3]);
  httpd_resp_send(req, ip, strlen(ip));
  return ESP_OK;
}

#define MQTTDIAG_RET_SIZE 1400

static esp_err_t mqttdiag_get_handler(httpd_req_t *req) {
  if (auth_request(req) == false) {
    return 0;
  }

  char *ret = malloc(MQTTDIAG_RET_SIZE);
  if (!ret) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  int32_t mqtt_stage = hasi32(MQTT_DIAG_KEY_STAGE) ? geti32(MQTT_DIAG_KEY_STAGE) : -1;
  int32_t mqtt_disc_idx = hasi32(MQTT_DIAG_KEY_DISC_IDX) ? geti32(MQTT_DIAG_KEY_DISC_IDX) : -1;
  int state = get_state();
  int wifi_status = get_wifi_status();
  int mqtt_connected = get_mqtt_connected() ? 1 : 0;
  int n_restarts = get_n_restarts();
  int ota_status = get_ota_status();
  // Why the current boot happened (esp_reset_reason_t: 1 power-on, 3 software,
  // 4 panic, 5/6/7 watchdogs, 9 brownout) plus heap and uptime, so a reboot
  // loop or a leak can be diagnosed over HTTP without a serial console.
  int reset_reason = (int)esp_reset_reason();
  unsigned long heap_free = (unsigned long)esp_get_free_heap_size();
  unsigned long heap_min_free = (unsigned long)esp_get_minimum_free_heap_size();
  long uptime_s = (long)(esp_timer_get_time() / 1000000LL);
  int32_t mqtt_stack_hwm = get_mqtt_stack_hwm();
  time_t now_s = 0;
  time(&now_s);
  int time_valid = now_s >= 1500000000 ? 1 : 0;  // clock past 2017 = NTP or NVS seed applied
  size_t fs_total = 0, fs_used = 0;
  if (esp_spiffs_info(NULL, &fs_total, &fs_used) != ESP_OK) {
    fs_total = 0;
    fs_used = 0;
  }
  nvs_stats_t nvs_stats = {0};
  if (nvs_get_stats(NULL, &nvs_stats) != ESP_OK) {
    nvs_stats.used_entries = 0;
    nvs_stats.free_entries = 0;
  }
  // 128 is plenty for a broker URL / client id and keeps this handler's stack
  // use well inside the HTTP server task; getstr() truncates longer values.
  char broker_url[128] = {0};
  char broker_clientid[128] = {0};
  char reset_history[128] = {0};

  getstr(BROKER_URL, broker_url, sizeof(broker_url) - 1);
  getstr(BROKER_CLIENTID, broker_clientid, sizeof(broker_clientid) - 1);
  get_reset_history(reset_history, sizeof(reset_history) - 1);

  int written = snprintf(ret, MQTTDIAG_RET_SIZE,
      "{\"mqtt_stage\":%ld,\"mqtt_disc_idx\":%ld,\"state\":%d,"
      "\"wifi_status\":%d,\"mqtt_connected\":%d,\"n_restarts\":%d,"
      "\"ota_status\":%d,\"reset_reason\":%d,\"reset_history\":\"%s\",\"heap_free\":%lu,"
      "\"heap_min_free\":%lu,\"heap_min_free_at\":%ld,\"heap_low_events\":%d,\"heap_min_ctx\":\"%s\","
      "\"uptime_s\":%ld,\"fs_used\":%u,\"fs_total\":%u,\"nvs_used\":%u,\"nvs_free\":%u,"
      "\"mqtt_stack_hwm\":%ld,\"time_valid\":%d,\"broker_url\":\"%s\","
      "\"broker_clientid\":\"%s\"}",
      (long)mqtt_stage,
      (long)mqtt_disc_idx,
      state,
      wifi_status,
      mqtt_connected,
      n_restarts,
      ota_status,
      reset_reason,
      reset_history,
      heap_free,
      heap_min_free,
      get_heap_min_free_at(),
      get_heap_low_events(),
      get_heap_min_ctx(),
      uptime_s,
      (unsigned int)fs_used,
      (unsigned int)fs_total,
      (unsigned int)nvs_stats.used_entries,
      (unsigned int)nvs_stats.free_entries,
      (long)mqtt_stack_hwm,
      time_valid,
      broker_url,
      broker_clientid);
  if (written < 0 || written >= MQTTDIAG_RET_SIZE) {
    ESP_LOGE(SGO_LOG_NOSEND, "@HTTPD /mqttdiag JSON truncated (%d bytes)", written);
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_send(req, ret, strlen(ret));
  free(ret);
  return ESP_OK;
}

/* /dash: everything the web dashboard shows, in one JSON document. The
 * page used to issue ~40 GET /i requests per refresh; every open socket costs
 * this chip 3-4 KB of heap, so one chunked response is much cheaper for it.
 * Chunked because the httpd task stack is 6 KB: only one box is formatted at
 * a time in a 768-byte buffer. */
#define DASH_CHUNK_SIZE 768

static void dash_append(char *buf, size_t size, size_t *len, const char *fmt, ...) {
  if (*len + 1 >= size) {
    return;
  }
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf + *len, size - *len, fmt, ap);
  va_end(ap);
  if (n < 0) {
    return;
  }
  *len += (size_t)n < size - *len ? (size_t)n : size - *len - 1;
}

static esp_err_t dash_send(httpd_req_t *req, const char *buf, size_t len) {
  if (len + 1 >= DASH_CHUNK_SIZE) {
    ESP_LOGE(SGO_LOG_NOSEND, "@HTTPD /dash chunk truncated (%u bytes)", (unsigned int)len);
  }
  return httpd_resp_send_chunk(req, buf, len);
}

static esp_err_t dash_get_handler(httpd_req_t *req) {
  if (auth_request(req) == false) {
    return 0;
  }
  char buf[DASH_CHUNK_SIZE];
  size_t len = 0;

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");

  len = 0;
  dash_append(buf, sizeof(buf), &len, "{\"boxes\":[");
#ifdef MODULE_BOX
  for (int i = 0; i < N_BOX; ++i) {
    if (dash_send(req, buf, len) != ESP_OK) {
      return ESP_FAIL;
    }
    len = 0;
    dash_append(buf, sizeof(buf), &len,
        "%s{\"i\":%d,\"enabled\":%d,\"temp\":%d,\"humi\":%d,\"vpd\":%d,\"co2\":%ld,\"weight\":%ld,\"led_dim\":%ld,"
        "\"started_at\":%lu,\"duration_days\":%u",
        i ? "," : "", i, (int)get_box_enabled(i), (int)get_box_temp(i), (int)get_box_humi(i), (int)get_box_vpd(i),
        (long)get_box_co2(i), (long)get_box_weight(i), (long)get_box_led_dim(i),
        (unsigned long)get_box_started_at(i), (unsigned int)get_box_duration_days(i));
#ifdef MODULE_TIMER
    dash_append(buf, sizeof(buf), &len,
        ",\"timer_type\":%d,\"timer_output\":%d,\"on_hour\":%d,\"on_min\":%d,\"off_hour\":%d,\"off_min\":%d",
        (int)get_box_timer_type(i), (int)get_box_timer_output(i), (int)get_box_on_hour(i), (int)get_box_on_min(i),
        (int)get_box_off_hour(i), (int)get_box_off_min(i));
#endif
#ifdef MODULE_FAN
    dash_append(buf, sizeof(buf), &len,
        ",\"fan_duty\":%d,\"fan_ref\":%d,\"fan_ref_min\":%d,\"fan_ref_max\":%d,\"fan_ref_source\":%d",
        (int)get_box_fan_duty(i), (int)get_box_fan_ref(i), (int)get_box_fan_ref_min(i), (int)get_box_fan_ref_max(i),
        (int)get_box_fan_ref_source(i));
#endif
#ifdef MODULE_BLOWER
    dash_append(buf, sizeof(buf), &len,
        ",\"blower_duty\":%d,\"blower_ref\":%d,\"blower_ref_min\":%d,\"blower_ref_max\":%d,\"blower_ref_source\":%d",
        (int)get_box_blower_duty(i), (int)get_box_blower_ref(i), (int)get_box_blower_ref_min(i),
        (int)get_box_blower_ref_max(i), (int)get_box_blower_ref_source(i));
#endif
#ifdef MODULE_WATERING
    dash_append(buf, sizeof(buf), &len,
        ",\"watering_power\":%d,\"watering_left\":%d,\"watering_last\":%ld,\"watering_period\":%u,\"watering_duration\":%u",
        (int)get_box_watering_power(i), (int)get_box_watering_left(i), (long)get_box_watering_last(i),
        (unsigned int)get_box_watering_period(i), (unsigned int)get_box_watering_duration(i));
#endif
    dash_append(buf, sizeof(buf), &len, "}");
  }
#endif
  if (dash_send(req, buf, len) != ESP_OK) {
    return ESP_FAIL;
  }

  len = 0;
  dash_append(buf, sizeof(buf), &len, "],\"leds\":[");
#ifdef MODULE_LED
  for (int i = 0; i < N_LED; ++i) {
    dash_append(buf, sizeof(buf), &len, "%s{\"box\":%d,\"duty\":%d,\"dim\":%d}",
        i ? "," : "", (int)get_led_box(i), (int)get_led_duty(i), (int)get_led_dim(i));
  }
#endif
  dash_append(buf, sizeof(buf), &len, "]");
  if (dash_send(req, buf, len) != ESP_OK) {
    return ESP_FAIL;
  }

  len = 0;
#ifdef MODULE_SENSOR_HEALTH
  char last_alert[64] = {0};
  get_sensor_health_last_alert(last_alert, sizeof(last_alert) - 1);
  dash_append(buf, sizeof(buf), &len,
      ",\"sensor_health\":{\"status\":%d,\"last_alert\":\"%s\",\"enabled\":%d,\"period_s\":%u,"
      "\"warmup_samples\":%u,\"stuck_samples\":%u}",
      (int)get_sensor_health_status(), last_alert, (int)get_sensor_health_enabled(),
      (unsigned int)get_sensor_health_period_s(), (unsigned int)get_sensor_health_warmup_samples(),
      (unsigned int)get_sensor_health_stuck_samples());
#endif
  time_t now_s = 0;
  time(&now_s);
  dash_append(buf, sizeof(buf), &len, ",\"time\":%ld}", (long)now_s);
  if (dash_send(req, buf, len) != ESP_OK) {
    return ESP_FAIL;
  }
  return httpd_resp_send_chunk(req, NULL, 0);
}

/* /kv: every readable key in one JSON document,
 * {"i":{"NAME":int,...},"s":{"NAME":"str",...}}. The app used to load the
 * ~290 parameters one GET /i or /s at a time (about 40 s over Wi-Fi); one
 * chunked answer costs the chip a single socket. Streamed byte by byte into
 * the same 768-byte buffer as /dash, so the httpd stack stays small. */
typedef struct {
  httpd_req_t *req;
  char buf[DASH_CHUNK_SIZE];
  size_t len;
  bool failed;
} kv_out_t;

static void kv_putc(kv_out_t *o, char c) {
  if (o->failed) {
    return;
  }
  if (o->len + 1 >= sizeof(o->buf)) {
    if (httpd_resp_send_chunk(o->req, o->buf, o->len) != ESP_OK) {
      o->failed = true;
      return;
    }
    o->len = 0;
  }
  o->buf[o->len++] = c;
}

static void kv_puts(kv_out_t *o, const char *s) {
  while (*s) {
    kv_putc(o, *s++);
  }
}

static void kv_put_json_string(kv_out_t *o, const char *s) {
  kv_putc(o, '"');
  for (; *s; ++s) {
    unsigned char c = (unsigned char)*s;
    if (c == '"' || c == '\\') {
      kv_putc(o, '\\');
      kv_putc(o, (char)c);
    } else if (c < 0x20) {
      char esc[8];
      snprintf(esc, sizeof(esc), "\\u%04x", c);
      kv_puts(o, esc);
    } else {
      kv_putc(o, (char)c);
    }
  }
  kv_putc(o, '"');
}

static void kv_put_int(kv_out_t *o, const char *name, long value, bool *first) {
  char item[80];
  snprintf(item, sizeof(item), "%s\"%s\":%ld", *first ? "" : ",", name, value);
  *first = false;
  kv_puts(o, item);
}

#define KV_PUT_ALL(mappings) \
  for (int i = 0; mappings[i].name != NULL; ++i) { \
    if (mappings[i].getter) { \
      kv_put_int(&o, mappings[i].name, (long)mappings[i].getter(), &first); \
    } \
  }

static esp_err_t kv_get_handler(httpd_req_t *req) {
  if (auth_request(req) == false) {
    return 0;
  }
  kv_out_t o = { .req = req, .len = 0, .failed = false };
  bool first = true;

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");

  kv_puts(&o, "{\"i\":{");
  KV_PUT_ALL(kvi8_mappings)
  KV_PUT_ALL(kvui8_mappings)
  KV_PUT_ALL(kvi16_mappings)
  KV_PUT_ALL(kvui16_mappings)
  KV_PUT_ALL(kvi32_mappings)
  KV_PUT_ALL(kvui32_mappings)
  kv_puts(&o, "},\"s\":{");

  first = true;
  char value[MAX_KVALUE_SIZE];
  for (int i = 0; kvs_mappings[i].name != NULL; ++i) {
    if (!kvs_mappings[i].getter) {
      continue;
    }
    // Wi-Fi / AP passwords stay reachable one by one through /s (Alan's
    // choice: no HTTP auth), but a bulk dump should not carry them along.
    if (strstr(kvs_mappings[i].name, "PASSWORD") != NULL) {
      continue;
    }
    memset(value, 0, sizeof(value));
    kvs_mappings[i].getter(value, MAX_KVALUE_SIZE - 1);
    kv_puts(&o, first ? "\"" : ",\"");
    kv_puts(&o, kvs_mappings[i].name);
    kv_puts(&o, "\":");
    kv_put_json_string(&o, value);
    first = false;
  }
  kv_puts(&o, "}}");

  if (o.failed) {
    return ESP_FAIL;
  }
  if (o.len > 0 && httpd_resp_send_chunk(req, o.buf, o.len) != ESP_OK) {
    return ESP_FAIL;
  }
  return httpd_resp_send_chunk(req, NULL, 0);
}

httpd_uri_t uri_get_kv = {
  .uri      = "/kv",
  .method   = HTTP_GET,
  .handler  = kv_get_handler,
  .user_ctx = NULL
};

httpd_uri_t uri_get_dash = {
  .uri      = "/dash",
  .method   = HTTP_GET,
  .handler  = dash_get_handler,
  .user_ctx = NULL
};

httpd_uri_t uri_geti = {
  .uri      = "/i",
  .method   = HTTP_GET,
  .handler  = geti_handler,
  .user_ctx = NULL
};

httpd_uri_t uri_seti = {
  .uri      = "/i",
  .method   = HTTP_POST,
  .handler  = seti_handler,
  .user_ctx = NULL
};

httpd_uri_t uri_getstr = {
  .uri      = "/s",
  .method   = HTTP_GET,
  .handler  = getstr_handler,
  .user_ctx = NULL
};

httpd_uri_t uri_setstr = {
  .uri      = "/s",
  .method   = HTTP_POST,
  .handler  = setstr_handler,
  .user_ctx = NULL
};

httpd_uri_t uri_setsigningkey = {
  .uri      = "/signing",
  .method   = HTTP_POST,
  .handler  = setsigningkey_handler,
  .user_ctx = NULL
};

httpd_uri_t uri_get_ip = {
  .uri      = "/myip",
  .method   = HTTP_GET,
  .handler  = get_ip_handler,
  .user_ctx = NULL
};

httpd_uri_t uri_get_mqttdiag = {
  .uri      = "/mqttdiag",
  .method   = HTTP_GET,
  .handler  = mqttdiag_get_handler,
  .user_ctx = NULL
};

httpd_uri_t file_download = {
	.uri       = "/fs/?*",
	.method    = HTTP_GET,
	.handler   = download_get_handler,
	.user_ctx  = NULL
};

httpd_uri_t file_upload = {
	.uri       = "/fs/*",
	.method    = HTTP_POST,
	.handler   = upload_post_handler,
	.user_ctx  = NULL
};

httpd_uri_t file_delete = {
	.uri       = "/fs/*",
	.method    = HTTP_DELETE,
	.handler   = delete_post_handler,
	.user_ctx  = NULL
};

httpd_uri_t uri_option = {
  .uri      = "/*",
  .method   = HTTP_OPTIONS,
  .handler  = option_handler,
  .user_ctx = NULL
};

static httpd_handle_t server = NULL;

static void start_webserver_task(void *args) {
  vTaskDelay(1000 / portTICK_PERIOD_MS); // Looks like we have a race confition with wifi

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  // handlers keep MAX_KVALUE_SIZE (517 B) buffers on the stack; the 4 KB default is tight
  config.stack_size = 6144;
  config.lru_purge_enable = true;
  config.uri_match_fn = httpd_uri_match_wildcard;
  config.max_uri_handlers = 13;

  if (httpd_start(&server, &config) == ESP_OK) {
    httpd_register_uri_handler(server, &uri_geti);
    httpd_register_uri_handler(server, &uri_seti);
    httpd_register_uri_handler(server, &uri_getstr);
    httpd_register_uri_handler(server, &uri_setstr);
    httpd_register_uri_handler(server, &uri_setsigningkey);
    httpd_register_uri_handler(server, &uri_get_ip);
    httpd_register_uri_handler(server, &uri_get_mqttdiag);
    httpd_register_uri_handler(server, &uri_get_dash);
    httpd_register_uri_handler(server, &uri_get_kv);
    httpd_register_uri_handler(server, &file_download);
		httpd_register_uri_handler(server, &file_upload);
		httpd_register_uri_handler(server, &file_delete);
    httpd_register_uri_handler(server, &uri_option);
  } else {
    ESP_LOGE(SGO_LOG_NOSEND, "Failed to start httpd!");
  }

  vTaskDelete(NULL);
}

void init_httpd() {
  ESP_LOGI(SGO_LOG_NOSEND, "@HTTPD Intializing HTTPD task");

  BaseType_t ret = xTaskCreatePinnedToCore(start_webserver_task, "START_WEBSERVER", 2048, NULL, 10, NULL, 1);
  if (ret != pdPASS) {
    ESP_LOGE(SGO_LOG_NOSEND, "@HTTPD Failed to create task");
  }
}

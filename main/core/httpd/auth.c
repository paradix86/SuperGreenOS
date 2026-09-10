/*
 * Copyright (C) 2021  SuperGreenLab <towelie@supergreenlab.com>
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
#include <string.h>
#include <esp_http_server.h>
#include <esp_timer.h>

#include "../kv/kv.h"
#include "../log/log.h"

static bool auth_failed(httpd_req_t *req) {
  httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Please login\"");
  httpd_resp_set_status(req, "401");
  httpd_resp_send(req, "NOK", 3);
  return false;
}

static char g_last_uri[64] = {0};
static volatile long g_last_uri_at_s = 0;

void httpd_last_request(char *uri, size_t size, long *at_s) {
  strncpy(uri, g_last_uri, size - 1);
  uri[size - 1] = 0;
  *at_s = g_last_uri_at_s;
}

static void note_request(httpd_req_t *req) {
  const char *q = strchr(req->uri, '?');
  size_t n = q ? (size_t)(q - req->uri) : strlen(req->uri);
  if (n >= sizeof(g_last_uri)) {
    n = sizeof(g_last_uri) - 1;
  }
  memcpy(g_last_uri, req->uri, n);
  g_last_uri[n] = 0;
  g_last_uri_at_s = (long)(esp_timer_get_time() / 1000000LL);
}

bool auth_request(httpd_req_t *req) {
  note_request(req);
  if (!hasstr(HTTPD_AUTH)) {
    return true;
  }
  char *auth = malloc(MAX_KVALUE_SIZE);
  if (!auth) return auth_failed(req);
  getstr(HTTPD_AUTH, auth, MAX_KVALUE_SIZE);

  if (strlen(auth) == 0) {
    free(auth);
    return true;
  }

  char *reqAuth = malloc(MAX_KVALUE_SIZE);
  if (!reqAuth) {
    free(auth);
    return auth_failed(req);
  }
  bool result = true;
  if(httpd_req_get_hdr_value_str(req, "Authorization", reqAuth, MAX_KVALUE_SIZE) != ESP_OK) {
    result = false;
  } else if (strlen(reqAuth) != 6 + strlen(auth) || strncmp(auth, &(reqAuth[6]), MAX_KVALUE_SIZE-6) != 0) {
    result = false;
  }
  free(auth);
  free(reqAuth);
  return result ? true : auth_failed(req);
}

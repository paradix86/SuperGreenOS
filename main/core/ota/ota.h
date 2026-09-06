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

#ifndef OTA_H_
#define OTA_H_

#define OTA_BUILD_TIMESTAMP 0

// Delay before confirming a freshly OTA-updated image as valid (see
// CONFIG_APP_ROLLBACK_ENABLE): long enough to get past early-init crashes,
// short enough that few unrelated reboots happen while still unconfirmed.
#define OTA_MARK_VALID_DELAY_S 30

typedef enum {
  OTA_STATUS_IDLE,
  OTA_STATUS_IN_PROGRESS,
  OTA_STATUS_DISABLED,
  OTA_STATUS_FAILED,
} ota_status;

void init_ota();
int request_ota_start(int value);
int on_set_ota_start(int value);

#endif

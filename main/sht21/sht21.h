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

#ifndef SHT21_H_
#define SHT21_H_

#include <stdint.h>

void init_sht21(int i2cId);
void loop_sht21(int i2cId);

// Number of times the raw 16-bit temperature or humidity reading of the
// sensor on i2c port [i2cId] differed from the previous one. A live SHT21
// never returns the same raw words for long (14-bit resolution, thermal
// noise); a frozen or unplugged one does. Used by sensor_health.
uint32_t get_sht21_raw_changes(int i2cId);

#endif

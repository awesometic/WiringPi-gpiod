/*
 * wiringGpiod.h
 *      Support library to use modern libgpiod for Odroid.
 *      This library works only as a part of WiringPi Odroid-ports.
********************************************************************************
Copyright (C) 2020-2021 Deokgyu Yang <secugyu@gmail.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
********************************************************************************
 */

#ifndef __WIRING_GPIOD_H__
#define __WIRING_GPIOD_H__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define WPI_GPIOD_CONSUMER_NAME	"WiringPi"

/*
 * Define kernel version for the specified libgpiod version
 * If this isn't regarded, implmented functions using gpiod might not work
 */
#define WPI_LIBGPIOD_VER		"1.6.2"
#define WPI_GPIOD_MIN_KERN_VER_MAJOR	5
#define WPI_GPIOD_MIN_KERN_VER_MINOR	5

extern char isGpiodInstalled();
extern void initGpiod(struct libodroid *libwiring);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __WIRING_GPIOD_H__ */

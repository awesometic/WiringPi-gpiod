/*
 * wiringGpiod.c
 *      Implementation of wiringGpiod.h
 *
 *      Make sure to change permission of /dev/gpiochip* files
 *      for user-space use.
 *
 *      Copyright (c) 2020 Deokgyu Yang <secugyu@gmail.com>
********************************************************************************
Copyright (C) 2020 Deokgyu Yang <secugyu@gmail.com>

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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <gpiod.h>

#include "wiringPi.h"
#include "wiringGpiod.h"
#include "softPwm.h"
#include "softTone.h"

/*----------------------------------------------------------------------------*/
// All Odroid boards has to have their line name as in these physical number
/*----------------------------------------------------------------------------*/
const char *_odroidPhyToLine[WPI_PINMAP_SIZE] = {
	"",			//  0
	"", "",			//  1 |  2
	"PIN_3", "",		//  3 |  4
	"PIN_5", "",		//  5 |  6
	"PIN_7", "PIN_8",	//  7 |  8
	"", "PIN_24",		//  9 | 10
	"PIN_23", "PIN_12",	// 11 | 12
	"PIN_13", "",		// 13 | 14
	"PIN_15", "PIN_16",	// 15 | 16
	"", "PIN_18",		// 17 | 18
	"PIN_19", "",		// 19 | 20
	"PIN_21", "PIN_22",	// 21 | 22
	"PIN_23", "PIN_24",	// 23 | 24
	"", "PIN_26",		// 25 | 26
	"PIN_27", "PIN_28",	// 27 | 28
	"PIN_29", "",		// 29 | 30
	"PIN_31", "PIN_32",	// 31 | 32
	"PIN_33", "",		// 33 | 34
	"PIN_35", "PIN_36",	// 35 | 36
	"", "",			// 37 | 38
	"", "",			// 39 | 40
	// 7 pin header		// 41...47
	"", "PIN_42", "", "PIN_44", "PIN_45", "PIN_46", "PIN_47",
	// Not used		// 48...63
	"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", ""
};

/*----------------------------------------------------------------------------*/
// The array has gpiod_line pointers of all of the gpio lines
// which are physically accessible
/*----------------------------------------------------------------------------*/
static struct gpiod_line *_gpiodLines[WPI_PINMAP_SIZE] = { NULL, };

/*----------------------------------------------------------------------------*/
// The array has pointers of all of the gpio lines request
// which are physically accessible
/*----------------------------------------------------------------------------*/
static int _gpiodRequests[WPI_PINMAP_SIZE] = { -1, };

/*----------------------------------------------------------------------------*/
// Stores presets of various configs to set easily when the pin mode
// about to be changed and back to the original states
//
// I prepared a enum to use that instead of preprocessors
/*----------------------------------------------------------------------------*/
static struct gpiod_line_request_config *_gpiodReqConfigs[16];

enum GpiodConfigPresets {
	CONF_DIR_ASIS = 0,
	CONF_DIR_IN,
	CONF_DIR_OUT,
	CONF_NUM_OF_TYPES
};

/*----------------------------------------------------------------------------*/
// Global struct variable and prototypes of core functions
/*----------------------------------------------------------------------------*/
static struct libodroid *lib = NULL;

char isGpiodInstalled();
char isCurrentModeGpiod();
void initGpiod(struct libodroid *libwiring);

int _makeSureToUsephyPin(int pin);
void _closeIfRequested(struct gpiod_line *line);

int _gpiod_digitalRead(int pin);
int _gpiod_digitalWrite(int pin, int value);
int _gpiod_pinMode(int pin, int mode);
unsigned int _gpiod_digitalReadByte();
int _gpiod_digitalWriteByte(const unsigned int value);

/*----------------------------------------------------------------------------*/
// Implements
/*----------------------------------------------------------------------------*/
int _makeSureToUsePhyPin(int pin) {
	if (lib->mode == MODE_PINS)
		return wpiToPhys[pin];
	else if (lib->mode == MODE_PHYS)
		return pin;
	else
		msg(MSG_ERR, "%s: Current mode is not supported for using gpiod.\n", __func__);

	return -1;
}

void _closeIfRequested(struct gpiod_line *line) {
	if (gpiod_line_is_requested(line))
		gpiod_line_release(line);
}

char isGpiodInstalled() {
	return system("/sbin/ldconfig -p | grep libgpiod > /dev/null")
		? FALSE : TRUE;
}

char isCurrentModeGpiod() {
	return lib->usingGpiod;
}

void initGpiod(struct libodroid *libwiring) {
	int i, req;
	const char *lineName;
	struct gpiod_line *line;

	if (!isGpiodInstalled())
		msg(MSG_ERR, "It seems this system hasn't libgpiod library.\n\tInstall that first and try again.\n");

	for (i = 0; i < CONF_NUM_OF_TYPES; i++) {
		_gpiodReqConfigs[i] = (struct gpiod_line_request_config *) malloc(sizeof(struct gpiod_line_request_config));
		_gpiodReqConfigs[i]->consumer = WPI_GPIOD_CONSUMER_NAME;
		_gpiodReqConfigs[i]->flags = 0;

		switch (i) {
		case CONF_DIR_ASIS:
			_gpiodReqConfigs[i]->request_type = GPIOD_LINE_REQUEST_DIRECTION_AS_IS;
			break;
		case CONF_DIR_IN:
			_gpiodReqConfigs[i]->request_type = GPIOD_LINE_REQUEST_DIRECTION_INPUT;
			break;
		case CONF_DIR_OUT:
			_gpiodReqConfigs[i]->request_type = GPIOD_LINE_REQUEST_DIRECTION_OUTPUT;
			break;
		default:
			break;
		}
	}

	// The index i will be used as the physical pin number
	for (i = 0; i < WPI_PINMAP_SIZE; i++) {
		lineName = _odroidPhyToLine[i];

		if (strlen(lineName) <= 1)
			continue;

		if ((line = gpiod_line_find(lineName)) == NULL)
			continue;
		_closeIfRequested(line);

		if ((req = gpiod_line_request(line, _gpiodReqConfigs[CONF_DIR_ASIS], -1)) < 0)
			continue;

		_gpiodLines[i] = line;
		_gpiodRequests[i] = req;
	}

	libwiring->usingGpiod = TRUE;
	libwiring->digitalRead = _gpiod_digitalRead;
	libwiring->digitalWrite = _gpiod_digitalWrite;
	libwiring->pinMode = _gpiod_pinMode;
	libwiring->digitalReadByte = _gpiod_digitalReadByte;
	libwiring->digitalWriteByte = _gpiod_digitalWriteByte;

	lib = libwiring;
}

UNU int _gpiod_digitalRead(int pin) {
	int ret, phyPin;
	struct gpiod_line *line;

	phyPin = _makeSureToUsePhyPin(pin);
	if ((line = _gpiodLines[phyPin]) == NULL)
		return -1;

	if ((ret = gpiod_line_get_value(line)) < 0) {
		msg(MSG_WARN, "%s: Error on getting value of the pin physical #%d.\n", __func__, phyPin);
		return -1;
	}

	return ret;
}

UNU int _gpiod_digitalWrite(int pin, int value) {
	int ret, phyPin;
	struct gpiod_line *line;

	phyPin = _makeSureToUsePhyPin(pin);

	if ((line = _gpiodLines[phyPin]) == NULL)
		return -1;

	if ((ret = gpiod_line_set_value(line, value)) < 0) {
		msg(MSG_WARN, "%s: Error on setting value of the pin physical #%d.\n", __func__, phyPin);
		return -1;
	}

	return ret;
}

UNU int _gpiod_pinMode(int pin, int mode) {
	int req, phyPin;
	struct gpiod_line *line;

	phyPin = _makeSureToUsePhyPin(pin);
	if ((line = _gpiodLines[phyPin]) == NULL)
		return -1;

	_closeIfRequested(line);

	softPwmStop(phyPin);
	softToneStop(phyPin);

	switch (mode) {
	case INPUT:
		if (gpiod_line_request(line, _gpiodReqConfigs[CONF_DIR_IN], 0) < 0) {
			msg(MSG_ERR, "%s: Error on setting direction of the pin physical #%d.\n", __func__, phyPin);
			return -1;
		}
		break;
	case OUTPUT:
		if (gpiod_line_request(line, _gpiodReqConfigs[CONF_DIR_OUT], 1) < 0) {
			msg(MSG_ERR, "%s: Error on setting direction of the pin physical #%d.\n", __func__, phyPin);
			return -1;
		}
		break;
	case INPUT_PULLUP:
	case INPUT_PULLDOWN:
		msg(MSG_WARN, "%s: Requested mode is not implemented yet on libgpiod.\n", __func__);
		break;
	case SOFT_PWM_OUTPUT:
		softPwmCreate(phyPin, 0, 100);
		break;
	case SOFT_TONE_OUTPUT:
		softToneCreate(phyPin);
		break;
	default:
		break;
	}

	_closeIfRequested(line);

	if ((req = gpiod_line_request(line, _gpiodReqConfigs[CONF_DIR_ASIS], -1)) < 0) {
		msg(MSG_ERR, "%s: Error on setting pin mode for physical pin #%d.\n\tThis pin will be disabled.\n", __func__, phyPin);

		_gpiodLines[phyPin] = NULL;
		return -1;
	}

	_gpiodRequests[phyPin] = req;
	return 0;
}

UNU unsigned int _gpiod_digitalReadByte() {
	int ret, phyPin;
	unsigned char i, hexVal, value = 0;
	struct gpiod_line *line;

	for (i = 0; i < 8; i++) {
		phyPin = wpiToPhys[i];
		if ((line = _gpiodLines[phyPin]) == NULL)
			return -1;

		if ((ret = gpiod_line_get_value(line)) < 0) {
			msg(MSG_WARN, "%s: Error on getting value of the pin physical #%d.\n", __func__, phyPin);
			return -1;
		}

		switch (i) {
		case 0: hexVal = 0x01; break;
		case 1: hexVal = 0x02; break;
		case 2: hexVal = 0x04; break;
		case 3: hexVal = 0x08; break;
		case 4: hexVal = 0x10; break;
		case 5: hexVal = 0x20; break;
		case 6: hexVal = 0x40; break;
		case 7: hexVal = 0x80; break;
		}

		value |= ret ? hexVal : 0b0;
	}

	return value;
}

UNU int _gpiod_digitalWriteByte(const unsigned int value) {
	int ret, phyPin;
	unsigned char i, hexVal;
	struct gpiod_line *line;

	for (i = 0; i < 8; i++) {
		phyPin = wpiToPhys[i];
		if ((line = _gpiodLines[phyPin]) == NULL)
			return -1;

		switch (i) {
		case 0: hexVal = 0x01; break;
		case 1: hexVal = 0x02; break;
		case 2: hexVal = 0x04; break;
		case 3: hexVal = 0x08; break;
		case 4: hexVal = 0x10; break;
		case 5: hexVal = 0x20; break;
		case 6: hexVal = 0x40; break;
		case 7: hexVal = 0x80; break;
		}

		if ((ret = gpiod_line_set_value(line, value & hexVal)) < 0) {
			msg(MSG_WARN, "%s: Error on setting value of the pin physical #%d.\n", __func__, phyPin);
			return -1;
		}
	}

	return 0;
}

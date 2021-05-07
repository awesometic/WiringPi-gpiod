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
	"", "PIN_10",		//  9 | 10
	"PIN_11", "PIN_12",	// 11 | 12
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
// Stores presets of various configs to set easily when the pin mode
// about to be changed and back to the original states
//
// I prepared a enum to use that instead of preprocessors
/*----------------------------------------------------------------------------*/
enum GpiodConfigPresets {
	CONF_DIR_ASIS = 0,
	CONF_DIR_IN,
	CONF_DIR_OUT,
	CONF_PULL_DISABLED,
	CONF_PULL_DOWN,
	CONF_PULL_UP,
	CONF_NUM_OF_TYPES
};

static struct gpiod_line_request_config *_gpiodReqConfigs[CONF_NUM_OF_TYPES];

/*----------------------------------------------------------------------------*/
// Global struct variable and prototypes of core functions
/*----------------------------------------------------------------------------*/
static struct libodroid *lib = NULL;

char isGpiodInstalled();
void initGpiod(struct libodroid *libwiring);

int _makeSureToUsephyPin(int pin);

int _gpiod_getPUPD (int pin);
int _gpiod_pullUpDnControl (int pin, int pud);
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

char isGpiodInstalled() {
	return system("/sbin/ldconfig -p | grep libgpiod > /dev/null")
		? FALSE : TRUE;
}

void initGpiod(struct libodroid *libwiring) {
	int i;
	const char *lineName;
	struct gpiod_line *line;

	if (!isGpiodInstalled())
		msg(MSG_ERR, "It seems this system hasn't libgpiod library.\n\tInstall that first and try again.\n");

	if (wiringPiDebug)
		printf("%s: %4d: About to initialize gpiod mode\n", __func__, __LINE__);

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
		case CONF_PULL_DISABLED:
			_gpiodReqConfigs[i]->request_type = GPIOD_LINE_REQUEST_DIRECTION_INPUT;
			_gpiodReqConfigs[i]->flags |= GPIOD_LINE_REQUEST_FLAG_BIAS_DISABLE;
			break;
		case CONF_PULL_DOWN:
			_gpiodReqConfigs[i]->request_type = GPIOD_LINE_REQUEST_DIRECTION_INPUT;
			_gpiodReqConfigs[i]->flags |= GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_DOWN;
			break;
		case CONF_PULL_UP:
			_gpiodReqConfigs[i]->request_type = GPIOD_LINE_REQUEST_DIRECTION_INPUT;
			_gpiodReqConfigs[i]->flags |= GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_UP;
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

		_gpiodLines[i] = line;
	}

	libwiring->usingGpiod = TRUE;
	libwiring->getPUPD = _gpiod_getPUPD;
	libwiring->pullUpDnControl = _gpiod_pullUpDnControl;
	libwiring->digitalRead = _gpiod_digitalRead;
	libwiring->digitalWrite = _gpiod_digitalWrite;
	libwiring->pinMode = _gpiod_pinMode;
	libwiring->digitalReadByte = _gpiod_digitalReadByte;
	libwiring->digitalWriteByte = _gpiod_digitalWriteByte;

	lib = libwiring;

	if (wiringPiDebug)
		printf("%s: %4d: gpiod mode initilized \n", __func__, __LINE__);
}

UNU int _gpiod_getPUPD(int pin) {
	int phyPin;
	struct gpiod_line *line;

	phyPin = _makeSureToUsePhyPin(pin);
	if ((line = _gpiodLines[phyPin]) == NULL)
		return -1;

	switch (gpiod_line_bias(line)) {
	case GPIOD_LINE_REQUEST_FLAG_BIAS_DISABLE:
		return PUD_OFF;
	case GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_DOWN:
		return PUD_DOWN;
	case GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_UP:
		return PUD_UP;
	default:
		msg(MSG_ERR, "%s: Error on getting pull status of the pin physical #%d.\n", __func__, phyPin);
		return -1;
	}
}

UNU int _gpiod_pullUpDnControl(int pin, int pud) {
	int pudModeForPinMode = 0;

	switch (pud) {
	case PUD_OFF:
		pudModeForPinMode = INPUT_PULLOFF;
		break;
	case PUD_DOWN:
		pudModeForPinMode = INPUT_PULLDOWN;
		break;
	case PUD_UP:
		pudModeForPinMode = INPUT_PULLUP;
		break;
	}

	return _gpiod_pinMode(pin, pudModeForPinMode);
}

UNU int _gpiod_digitalRead(int pin) {
	int ret, phyPin;
	struct gpiod_chip *chip;
	struct gpiod_line *line;

	phyPin = _makeSureToUsePhyPin(pin);
	if ((line = _gpiodLines[phyPin]) == NULL)
		return -1;
	chip = gpiod_line_get_chip(line);

	if ((ret = gpiod_ctxless_get_value(
		gpiod_chip_name(chip),
		gpiod_line_offset(line),
		FALSE,
		WPI_GPIOD_CONSUMER_NAME)) < 0) {

		msg(MSG_WARN, "%s: Error on getting value of the pin physical #%d.\n", __func__, phyPin);
		return -1;
	}

	return ret;
}

UNU int _gpiod_digitalWrite(int pin, int value) {
	int ret, phyPin;
	struct gpiod_chip *chip;
	struct gpiod_line *line;

	phyPin = _makeSureToUsePhyPin(pin);

	if ((line = _gpiodLines[phyPin]) == NULL)
		return -1;
	chip = gpiod_line_get_chip(line);

	if ((ret = gpiod_ctxless_set_value(
		gpiod_chip_name(chip),
		gpiod_line_offset(line),
		value,
		FALSE,
		WPI_GPIOD_CONSUMER_NAME,
		NULL, NULL)) < 0) {
		msg(MSG_WARN, "%s: Error on setting value of the pin physical #%d.\n", __func__, phyPin);
		return -1;
	}

	return ret;
}

UNU int _gpiod_pinMode(int pin, int mode) {
	int phyPin;
	struct gpiod_line *line;

	phyPin = _makeSureToUsePhyPin(pin);
	if ((line = _gpiodLines[phyPin]) == NULL)
		return -1;

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
		if (gpiod_line_request(line, _gpiodReqConfigs[CONF_PULL_UP], 0) < 0) {
			msg(MSG_ERR, "%s: Error on setting pull status of the pin physical #%d.\n", __func__, phyPin);
			return -1;
		}
		break;
	case INPUT_PULLDOWN:
		if (gpiod_line_request(line, _gpiodReqConfigs[CONF_PULL_DOWN], 0) < 0) {
			msg(MSG_ERR, "%s: Error on setting pull status of the pin physical #%d.\n", __func__, phyPin);
			return -1;
		}
		break;
	case INPUT_PULLOFF:
		if (gpiod_line_request(line, _gpiodReqConfigs[CONF_PULL_DISABLED], 0) < 0) {
			msg(MSG_ERR, "%s: Error on setting pull status of the pin physical #%d.\n", __func__, phyPin);
			return -1;
		}
		break;
	case SOFT_PWM_OUTPUT:
		return softPwmCreate(phyPin, 0, 100);
	case SOFT_TONE_OUTPUT:
		return softToneCreate(phyPin);
	default:
		break;
	}

	gpiod_line_release(line);
	return 0;
}

UNU unsigned int _gpiod_digitalReadByte() {
	int ret;
	unsigned char i, hexVal, value = 0;

	for (i = 0; i < 8; i++) {
		ret = _gpiod_digitalRead(i);

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
	unsigned char i, hexVal;

	for (i = 0; i < 8; i++) {
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

		_gpiod_digitalWrite(i, value & hexVal);
	}

	return 0;
}

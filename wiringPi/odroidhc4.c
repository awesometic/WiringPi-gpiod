/*----------------------------------------------------------------------------*/
//
//
//	WiringPi ODROID-C4 Board Control file (AMLogic 64Bits Platform)
//
//
/*----------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <asm/ioctl.h>
#include <sys/mman.h>

/*----------------------------------------------------------------------------*/
#include "softPwm.h"
#include "softTone.h"

/*----------------------------------------------------------------------------*/
#include "wiringPi.h"
#include "odroidc4.h"

/*----------------------------------------------------------------------------*/
// wiringPi gpio map define
/*----------------------------------------------------------------------------*/
static const int pinToGpio[64] = {
	// wiringPi number to native gpio number
	493, 494,	//  0 |  1 : GPIOX.17(I2C-2_SDA), GPIOX.18(I2C-2_SCL)
	481,  -1,	//  2 |  3 : , GPIOX.5
	 -1,  -1,	//  4 |  5 :
	 -1,  -1,	//  6 |  7 :
	 -1,  -1,	//  8 |  9 :
	 -1,  -1,	// 10 | 11 :
	 -1,  -1,	// 12 | 13 :
	 -1,  -1,	// 14 | 15 :
	 -1,  -1,	// 16 | 17 :
	 -1,  -1,	// 18 | 19 :
	 -1,  -1,	// 20 | 21 :
	 -1,  -1,	// 22 | 23 :
	 -1,  -1,	// 24 | 25 :
	 -1,  -1,	// 26 | 27 :
	 -1,  -1,	// 28 | 29 :
	 -1,  -1,	// 30 | 31 :
	// Padding:
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,	// 32...47
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,	// 48...63
};

static const int phyToGpio[64] = {
	// physical header pin number to native gpio number
	 -1,		//  0
	 -1, 493,	//  1 |  2 : 3.3V, GPIOX.17(I2C-2_SDA)
	494, 481,	//  3 |  4 : GPIOX.18(I2C-2_SCL), GPIOX.5
	 -1,  -1,	//  5 |  6 : GND,
	 -1,  -1,	//  7 |  8 :
	 -1,  -1,	//  9 | 10 :
	 -1,  -1,	// 11 | 12 :
	 -1,  -1,	// 13 | 14 :
	 -1,  -1,	// 15 | 16 :
	 -1,  -1,	// 17 | 18 :
	 -1,  -1,	// 19 | 20 :
	 -1,  -1,	// 21 | 22 :
	 -1,  -1,	// 23 | 24 :
	 -1,  -1,	// 25 | 26 :
	 -1,  -1,	// 27 | 28 :
	 -1,  -1,	// 29 | 30 :
	 -1,  -1,	// 31 | 32 :
	 -1,  -1,	// 33 | 34 :
	 -1,  -1,	// 35 | 36 :
	 -1,  -1,	// 37 | 38 :
	 -1,  -1,	// 39 | 40 :
	// Not used
	-1, -1, -1, -1, -1, -1, -1, -1,	// 41...48
	-1, -1, -1, -1, -1, -1, -1, -1,	// 49...56
	-1, -1, -1, -1, -1, -1, -1	// 57...63
};

/*----------------------------------------------------------------------------*/
//
// Global variable define
//
/*----------------------------------------------------------------------------*/
// wiringPi Pinmap control arrary
/*----------------------------------------------------------------------------*/
/* GPIO mmap control */
static volatile uint32_t *gpio;

/* wiringPi Global library */
static struct libodroid	*lib = NULL;

/*----------------------------------------------------------------------------*/
// Function prototype define
/*----------------------------------------------------------------------------*/
static int	gpioToGPSETReg	(int pin);
static int	gpioToGPLEVReg	(int pin);
static int	gpioToPUENReg	(int pin);
static int	gpioToPUPDReg	(int pin);
static int	gpioToShiftReg	(int pin);
static int	gpioToGPFSELReg	(int pin);
static int	gpioToDSReg	(int pin);
static int	gpioToMuxReg	(int pin);

/*----------------------------------------------------------------------------*/
// wiringPi core function
/*----------------------------------------------------------------------------*/
static int		_getModeToGpio		(int mode, int pin);
static int		_setDrive		(int pin, int value);
static int		_getDrive		(int pin);
static int		_pinMode		(int pin, int mode);
static int		_getAlt			(int pin);
static int		_getPUPD		(int pin);
static int		_pullUpDnControl	(int pin, int pud);
static int		_digitalRead		(int pin);
static int		_digitalWrite		(int pin, int value);

/*----------------------------------------------------------------------------*/
// board init function
/*----------------------------------------------------------------------------*/
static 	void init_gpio_mmap	(void);

	void init_odroidhc4 	(struct libodroid *libwiring);

/*----------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/
//
// offset to the GPIO Set regsiter
//
/*----------------------------------------------------------------------------*/
static int gpioToGPSETReg (int pin)
{
	if (pin >= C4_GPIOH_PIN_START && pin <= C4_GPIOH_PIN_END)
		return  C4_GPIOH_OUTP_REG_OFFSET;
	if (pin >= C4_GPIOA_PIN_START && pin <= C4_GPIOA_PIN_END)
		return  C4_GPIOA_OUTP_REG_OFFSET;
	if (pin >= C4_GPIOX_PIN_START && pin <= C4_GPIOX_PIN_END)
		return  C4_GPIOX_OUTP_REG_OFFSET;
	return	-1;
}

/*---------------------------------------------------------------------------r-*/
//
// offset to the GPIO Input regsiter
//
/*----------------------------------------------------------------------------*/
static int gpioToGPLEVReg (int pin)
{
	if (pin >= C4_GPIOH_PIN_START && pin <= C4_GPIOH_PIN_END)
		return  C4_GPIOH_INP_REG_OFFSET;
	if (pin >= C4_GPIOA_PIN_START && pin <= C4_GPIOA_PIN_END)
		return  C4_GPIOA_INP_REG_OFFSET;
	if (pin >= C4_GPIOX_PIN_START && pin <= C4_GPIOX_PIN_END)
		return  C4_GPIOX_INP_REG_OFFSET;
	return	-1;
}

/*----------------------------------------------------------------------------*/
//
// offset to the GPIO Pull up/down enable regsiter
//
/*----------------------------------------------------------------------------*/
static int gpioToPUENReg (int pin)
{
	if (pin >= C4_GPIOH_PIN_START && pin <= C4_GPIOH_PIN_END)
		return  C4_GPIOH_PUEN_REG_OFFSET;
	if (pin >= C4_GPIOA_PIN_START && pin <= C4_GPIOA_PIN_END)
		return  C4_GPIOA_PUEN_REG_OFFSET;
	if (pin >= C4_GPIOX_PIN_START && pin <= C4_GPIOX_PIN_END)
		return  C4_GPIOX_PUEN_REG_OFFSET;
	return	-1;
}

/*----------------------------------------------------------------------------*/
//
// offset to the GPIO Pull up/down regsiter
//
/*----------------------------------------------------------------------------*/
static int gpioToPUPDReg (int pin)
{
	if (pin >= C4_GPIOH_PIN_START && pin <= C4_GPIOH_PIN_END)
		return  C4_GPIOH_PUPD_REG_OFFSET;
	if (pin >= C4_GPIOA_PIN_START && pin <= C4_GPIOA_PIN_END)
		return  C4_GPIOA_PUPD_REG_OFFSET;
	if (pin >= C4_GPIOX_PIN_START && pin <= C4_GPIOX_PIN_END)
		return	C4_GPIOX_PUPD_REG_OFFSET;
	return	-1;
}

/*----------------------------------------------------------------------------*/
//
// offset to the GPIO bit
//
/*----------------------------------------------------------------------------*/
static int gpioToShiftReg (int pin)
{
	if (pin >= C4_GPIOH_PIN_START && pin <= C4_GPIOH_PIN_END)
		return  pin - C4_GPIOH_PIN_START;
	if (pin >= C4_GPIOA_PIN_START && pin <= C4_GPIOA_PIN_END)
		return  pin - C4_GPIOA_PIN_START;
	if (pin >= C4_GPIOX_PIN_START && pin <= C4_GPIOX_PIN_END)
		return  pin - C4_GPIOX_PIN_START;
	return	-1;
}

/*----------------------------------------------------------------------------*/
//
// offset to the GPIO Function register
//
/*----------------------------------------------------------------------------*/
static int gpioToGPFSELReg (int pin)
{
	if (pin >= C4_GPIOH_PIN_START && pin <= C4_GPIOH_PIN_END)
		return  C4_GPIOH_FSEL_REG_OFFSET;
	if(pin >= C4_GPIOA_PIN_START && pin <= C4_GPIOA_PIN_END)
		return  C4_GPIOA_FSEL_REG_OFFSET;
	if(pin >= C4_GPIOX_PIN_START && pin <= C4_GPIOX_PIN_END)
		return  C4_GPIOX_FSEL_REG_OFFSET;
	return	-1;
}

/*----------------------------------------------------------------------------*/
//
// offset to the GPIO Drive Strength register
//
/*----------------------------------------------------------------------------*/
static int gpioToDSReg (int pin)
{
	if (pin >= C4_GPIOH_PIN_START && pin <= C4_GPIOH_PIN_END)
		return  C4_GPIOH_DS_REG_3A_OFFSET;
	if (pin >= C4_GPIOA_PIN_START && pin <= C4_GPIOA_PIN_END)
		return  C4_GPIOA_DS_REG_5A_OFFSET;
	if (pin >= C4_GPIOX_PIN_START && pin <= C4_GPIOX_PIN_MID)
		return  C4_GPIOX_DS_REG_2A_OFFSET;
	if (pin > C4_GPIOX_PIN_MID && pin <= C4_GPIOX_PIN_END)
		return  C4_GPIOX_DS_REG_2B_OFFSET;
	return	-1;
}

/*----------------------------------------------------------------------------*/
//
// offset to the GPIO Pin Mux register
//
/*----------------------------------------------------------------------------*/
static int gpioToMuxReg (int pin)
{
	switch (pin) {
	case	C4_GPIOH_PIN_START	...C4_GPIOH_PIN_END:
		return  C4_GPIOH_MUX_B_REG_OFFSET;
	case	C4_GPIOA_PIN_START	...C4_GPIOA_PIN_START + 7:
		return  C4_GPIOA_MUX_D_REG_OFFSET;
	case	C4_GPIOA_PIN_START + 8	...C4_GPIOA_PIN_END:
		return  C4_GPIOA_MUX_E_REG_OFFSET;
	case	C4_GPIOX_PIN_START	...C4_GPIOX_PIN_START + 7:
		return  C4_GPIOX_MUX_3_REG_OFFSET;
	case	C4_GPIOX_PIN_START + 8	...C4_GPIOX_PIN_START + 15:
		return  C4_GPIOX_MUX_4_REG_OFFSET;
	case	C4_GPIOX_PIN_START + 16	...C4_GPIOX_PIN_END:
		return  C4_GPIOX_MUX_5_REG_OFFSET;
	default:
		return -1;
	}
}

/*----------------------------------------------------------------------------*/
static int _getModeToGpio (int mode, int pin)
{
	int retPin = -1;

	switch (mode) {
	/* Native gpio number */
	case	MODE_GPIO:
		retPin = pin;
		break;
	/* Native gpio number for sysfs */
	case	MODE_GPIO_SYS:
		retPin = lib->sysFds[pin] != -1 ? pin : -1;
		break;
	/* wiringPi number */
	case	MODE_PINS:
		retPin = pin < 64 ? pinToGpio[pin] : -1;
		break;
	/* header pin number */
	case	MODE_PHYS:
		retPin = pin < 64 ? phyToGpio[pin] : -1;
		break;
	default	:
		msg(MSG_WARN, "%s : Unknown Mode %d\n", __func__, mode);
		return -1;
	}

	return retPin;
}

/*----------------------------------------------------------------------------*/
static int _setDrive (int pin, int value)
{
	int ds, shift;

	if (lib->mode == MODE_GPIO_SYS)
		return -1;

	if ((pin = _getModeToGpio(lib->mode, pin)) < 0)
		return -1;

	if (value < 0 || value > 3) {
		msg(MSG_WARN, "%s : Invalid value %d (Must be 0 ~ 3)\n", __func__, value);
		return -1;
	}

	ds    = gpioToDSReg(pin);
	shift = gpioToShiftReg(pin);
	shift = pin > C4_GPIOX_PIN_MID ? (shift - 16) * 2 : shift * 2;

	*(gpio + ds) &= ~(0b11 << shift);
	*(gpio + ds) |= (value << shift);

	return 0;
}

/*----------------------------------------------------------------------------*/
static int _getDrive (int pin)
{
	int ds, shift;

	if (lib->mode == MODE_GPIO_SYS)
		return -1;

	if ((pin = _getModeToGpio(lib->mode, pin)) < 0)
		return -1;

	ds    = gpioToDSReg(pin);
	shift = gpioToShiftReg(pin);
	shift = pin > C4_GPIOX_PIN_MID ? (shift - 16) * 2 : shift * 2;

	return (*(gpio + ds)	>> shift) & 0b11;
}

/*----------------------------------------------------------------------------*/
static int _pinMode (int pin, int mode)
{
	int fsel, shift, origPin = pin;

	if (lib->mode == MODE_GPIO_SYS)
		return -1;

	if ((pin = _getModeToGpio(lib->mode, pin)) < 0)
		return -1;

	softPwmStop  (origPin);
	softToneStop (origPin);

	fsel  = gpioToGPFSELReg(pin);
	shift = gpioToShiftReg (pin);

	switch (mode) {
	case	INPUT:
		*(gpio + fsel) = (*(gpio + fsel) | (1 << shift));
		break;
	case	OUTPUT:
		*(gpio + fsel) = (*(gpio + fsel) & ~(1 << shift));
		break;
	case	SOFT_PWM_OUTPUT:
		softPwmCreate (pin, 0, 100);
		break;
	case	SOFT_TONE_OUTPUT:
		softToneCreate (pin);
		break;
	default:
		msg(MSG_WARN, "%s : Unknown Mode %d\n", __func__, mode);
		return -1;
	}

	return 0;
}

/*----------------------------------------------------------------------------*/
static int _getAlt (int pin)
{
	int fsel, mux, shift, target, mode;

	if (lib->mode == MODE_GPIO_SYS)
		return	-1;

	if ((pin = _getModeToGpio(lib->mode, pin)) < 0)
		return	-1;

	fsel   = gpioToGPFSELReg(pin);
	mux    = gpioToMuxReg(pin);
	target = shift = gpioToShiftReg(pin);

	while (target >= 8) {
		target -= 8;
	}

	mode = (*(gpio + mux) >> (target * 4)) & 0xF;
	return	mode ? mode + 1 : (*(gpio + fsel) & (1 << shift)) ? 0 : 1;
}

/*----------------------------------------------------------------------------*/
static int _getPUPD (int pin)
{
	int puen, pupd, shift;

	if (lib->mode == MODE_GPIO_SYS)
		return -1;

	if ((pin = _getModeToGpio(lib->mode, pin)) < 0)
		return -1;

	puen  = gpioToPUENReg(pin);
	pupd  = gpioToPUPDReg(pin);
	shift = gpioToShiftReg(pin);

	if (*(gpio + puen) & (1 << shift))
		return *(gpio + pupd) & (1 << shift) ? 1 : 2;
	else
		return 0;
}

/*----------------------------------------------------------------------------*/
static int _pullUpDnControl (int pin, int pud)
{
	int shift = 0;

	if (lib->mode == MODE_GPIO_SYS)
		return -1;

	if ((pin = _getModeToGpio(lib->mode, pin)) < 0)
		return -1;

	shift = gpioToShiftReg(pin);

	if (pud) {
		// Enable Pull/Pull-down resister
		*(gpio + gpioToPUENReg(pin)) =
			(*(gpio + gpioToPUENReg(pin)) | (1 << shift));

		if (pud == PUD_UP)
			*(gpio + gpioToPUPDReg(pin)) =
				(*(gpio + gpioToPUPDReg(pin)) |  (1 << shift));
		else
			*(gpio + gpioToPUPDReg(pin)) =
				(*(gpio + gpioToPUPDReg(pin)) & ~(1 << shift));
	} else	// Disable Pull/Pull-down resister
		*(gpio + gpioToPUENReg(pin)) =
			(*(gpio + gpioToPUENReg(pin)) & ~(1 << shift));

	return 0;
}

/*----------------------------------------------------------------------------*/
static int _digitalRead (int pin)
{
	char c ;

	if (lib->mode == MODE_GPIO_SYS) {
		if (lib->sysFds[pin] == -1)
			return -1;

		lseek	(lib->sysFds[pin], 0L, SEEK_SET);
		if (read(lib->sysFds[pin], &c, 1) < 0) {
			msg(MSG_WARN, "%s: Failed with reading from sysfs GPIO node. \n", __func__);
			return -1;
		}

		return	(c == '0') ? LOW : HIGH;
	}

	if ((pin = _getModeToGpio(lib->mode, pin)) < 0)
		return	-1;

	if ((*(gpio + gpioToGPLEVReg(pin)) & (1 << gpioToShiftReg(pin))) != 0)
		return HIGH ;
	else
		return LOW ;
}

/*----------------------------------------------------------------------------*/
static int _digitalWrite (int pin, int value)
{
	if (lib->mode == MODE_GPIO_SYS) {
		if (lib->sysFds[pin] != -1) {
			if (value == LOW) {
				if (write(lib->sysFds[pin], "0\n", 2) < 0)
					msg(MSG_WARN, "%s: Failed with reading from sysfs GPIO node. \n", __func__);
			} else {
				if (write(lib->sysFds[pin], "1\n", 2) < 0)
					msg(MSG_WARN, "%s: Failed with reading from sysfs GPIO node. \n", __func__);
			}
		}
		return -1;
	}

	if ((pin = _getModeToGpio(lib->mode, pin)) < 0)
		return -1;

	if (value == LOW)
		*(gpio + gpioToGPSETReg(pin)) &= ~(1 << gpioToShiftReg(pin));
	else
		*(gpio + gpioToGPSETReg(pin)) |=  (1 << gpioToShiftReg(pin));

	return 0;
}

/*----------------------------------------------------------------------------*/
static void init_gpio_mmap (void)
{
	int fd = -1;
	void *mapped;

	/* GPIO mmap setup */
	if (!getuid()) {
		if ((fd = open ("/dev/mem", O_RDWR | O_SYNC | O_CLOEXEC) ) < 0)
			msg (MSG_ERR,
				"wiringPiSetup: Unable to open /dev/mem: %s\n",
				strerror (errno));
	} else {
		if (access("/dev/gpiomem",0) == 0) {
			if ((fd = open ("/dev/gpiomem", O_RDWR | O_SYNC | O_CLOEXEC)) < 0) {
				msg(MSG_ERR,
					"wiringPiSetup: Unable to open /dev/gpiomem: %s\n",
					strerror (errno));
			}
			setUsingGpiomem(TRUE);
		} else
			msg (MSG_ERR,
				"wiringPiSetup: /dev/gpiomem doesn't exist. Please try again with sudo.\n");
	}

	if (fd < 0) {
		msg(MSG_ERR, "wiringPiSetup: Cannot open memory area for GPIO use. \n");
	} else {
		// #define C4_GPIO_BASE		0xff634000
#ifdef ANDROID
#if defined(__aarch64__)
		mapped = mmap(0, BLOCK_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, C4_GPIO_BASE);
#else
		mapped = mmap64(0, BLOCK_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, (off64_t)C4_GPIO_BASE);
#endif
#else
		mapped = mmap(0, BLOCK_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, C4_GPIO_BASE);
#endif

		if (mapped == MAP_FAILED)
			msg(MSG_ERR, "wiringPiSetup: mmap (GPIO) failed: %s \n", strerror (errno));
		else
			gpio = (uint32_t *) mapped;
	}
}

/*----------------------------------------------------------------------------*/
void init_odroidhc4 (struct libodroid *libwiring)
{
	init_gpio_mmap();

	/* wiringPi Core function initialize */
	libwiring->getModeToGpio	= _getModeToGpio;
	libwiring->setDrive		= _setDrive;
	libwiring->getDrive		= _getDrive;
	libwiring->pinMode		= _pinMode;
	libwiring->getAlt		= _getAlt;
	libwiring->getPUPD		= _getPUPD;
	libwiring->pullUpDnControl	= _pullUpDnControl;
	libwiring->digitalRead		= _digitalRead;
	libwiring->digitalWrite		= _digitalWrite;

	/* specify pin base number */
	libwiring->pinBase		= C4_GPIO_PIN_BASE;

	/* global variable setup */
	lib = libwiring;
}

/*----------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/

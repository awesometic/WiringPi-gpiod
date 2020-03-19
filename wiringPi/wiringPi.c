/*----------------------------------------------------------------------------*/
/*

	WiringPi Library for ODROIDs

 */
/*----------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>
#include <poll.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <asm/ioctl.h>

/*----------------------------------------------------------------------------*/
#include "softPwm.h"
#include "softTone.h"

/*----------------------------------------------------------------------------*/
#include "wiringPi.h"
#include "../version.h"

/*----------------------------------------------------------------------------*/
#include "odroidc1.h"
#include "odroidc2.h"
#include "odroidxu3.h"
#include "odroidn1.h"
#include "odroidn2.h"
#include "odroidc4.h"

/*----------------------------------------------------------------------------*/
// Const string define
/*----------------------------------------------------------------------------*/
const char *piModelNames [16] =
{
	// These names must be full name of the board.
	// And, the model name on the target board has to be a part of an item of the array.
	// e.g, ODROID-C or ODROID-XU3/4 may not be used for recognizing a board.
	"Unknown",
	"ODROID-C1/C1+",
	"ODROID-C2",
	"ODROID-XU3/XU4",
	"ODROID-N1",
	"ODROID-N2",
	"ODROID-C4",
};

const char *piRevisionNames [16] =
{
	"00",
	"01",
	"02",
	"03",
	"04",
	"05",
	"06",
	"07",
	"08",
	"09",
	"10",
	"11",
	"12",
	"13",
	"14",
	"15",
} ;

const char *piMakerNames [16] =
{
	"Unknown",	// 0
	"AMLogic",	// 1
	"Samsung",	// 2
	"Rockchip",	// 3
	"Unknown04",	// 4
	"Unknown05",	// 5
	"Unknown06",	// 6
	"Unknown07",	// 7
	"Unknown08",	// 8
	"Unknown09",	// 9
	"Unknown10",	// 10
	"Unknown11",	// 11
	"Unknown12",	// 12
	"Unknown13",	// 13
	"Unknown14",	// 14
	"Unknown15",	// 15
} ;

const int piMemorySize [8] =
{
	256,		//	 0
	512,		//	 1
	1024,		//	 2
	2048,		//	 3
	4096,		//	 4
	8192,		//	 5
	0,		//	 6
	0,		//	 7
} ;

/*----------------------------------------------------------------------------*/
// Misc
static pthread_mutex_t pinMutex ;

/*----------------------------------------------------------------------------*/
#ifdef __ANDROID__
int pthread_cancel(pthread_t h) {
    return pthread_kill(h, 0);
}
#endif /* __ANDROID__ */

/*----------------------------------------------------------------------------*/

// Debugging & Return codes
int wiringPiDebug       = FALSE ;
int wiringPiReturnCodes = FALSE ;

// ODROID Wiring Library
struct libodroid	libwiring;

unsigned int	usingGpioMem	= FALSE;
int		wiringPiSetuped	= FALSE;

/*----------------------------------------------------------------------------*/
//
// Return true/false if the supplied module is loaded
//
/*----------------------------------------------------------------------------*/
int moduleLoaded (char *modName)
{
	int len   = strlen (modName) ;
	int found = FALSE ;
	FILE *fd = fopen ("/proc/modules", "r") ;
	char line [80] ;

	if (fd == NULL) {
		fprintf (stderr, "gpio: Unable to check /proc/modules: %s\n",
			strerror (errno)) ;
		exit (1) ;
	}

	while (fgets (line, 80, fd) != NULL) {
		if (strncmp (line, modName, len) != 0)
			continue ;

		found = TRUE ;
		break ;
	}
	fclose (fd) ;

	return found ;
}

/*----------------------------------------------------------------------------*/
//
// ODROID System Message function
//
/*----------------------------------------------------------------------------*/
int msg (int type, const char *message, ...)
{
	va_list argp;
	char buffer [1024];

	va_start (argp, message) ;
	vsnprintf (buffer, 1023, message, argp);
	va_end (argp) ;

	fprintf (stderr, "%s : %s", type == MSG_WARN ? "warn" : "err", buffer) ;

	if (type != MSG_WARN)
		exit (EXIT_FAILURE) ;
	return 0 ;
}

/*----------------------------------------------------------------------------*/
static void warn_msg(const char *func)
{
	msg(MSG_WARN, "(%s) : This function is not supported by ODROID Board.\n", func);
}

/*----------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/
/*
 * wiringPiFailure:
 *	Fail. Or not.
 */
/*----------------------------------------------------------------------------*/
int wiringPiFailure (int fatal, const char *message, ...)
{
	va_list argp ;
	char buffer [1024] ;

	if (!fatal && wiringPiReturnCodes)
		return -1 ;

	va_start (argp, message) ;
	vsnprintf (buffer, 1023, message, argp) ;
	va_end (argp) ;

	fprintf (stderr, "%s", buffer);
	exit (EXIT_FAILURE);

	return 0 ;
}

/*----------------------------------------------------------------------------*/
/*
 * setupCheck
 *	Another sanity check because some users forget to call the setup
 *	function. Mosty because they need feeding C drip by drip )-:
 */
/*----------------------------------------------------------------------------*/
void setupCheck (const char *fName)
{
	if (!wiringPiSetuped) {
		fprintf (stderr, "%s: You have not called one of the wiringPiSetup\n"
		"  functions, so I'm aborting your program before it crashes anyway.\n", fName) ;
		exit (EXIT_FAILURE) ;
	}
}

/*----------------------------------------------------------------------------*/
/*
 * gpioMemCheck:
 *	See if we're using the /dev/gpiomem interface, if-so then some operations
 *	can't be done and will crash the Pi.
 */
/*----------------------------------------------------------------------------*/
void usingGpioMemCheck (const char *what)
{
	if (usingGpioMem) {
	fprintf (stderr, "%s: Unable to do this when using /dev/gpiomem. Try sudo?\n", what) ;
	exit (EXIT_FAILURE) ;
	}
}

/*----------------------------------------------------------------------------*/
void setUsingGpioMem( const unsigned int value )
{
	usingGpioMem = value;
}

/*----------------------------------------------------------------------------*/
int getModelFromCpuinfo(char *line, FILE *cpuFd) {
	char *model;

	if ((cpuFd = fopen("/proc/cpuinfo", "r")) != NULL) {
		while (fgets(line, 120, cpuFd) != NULL) {
			if (strncmp(line, "Hardware", 8) == 0)
				break;
		}

		if (!(strncmp(line, "Hardware", 8) != 0)) {
			if (wiringPiDebug)
				printf("piGpioLayout: %s: Hardware: %s\n", __func__, line);

			model = strcasestr(line, "odroid");
			if (!model)
				return -1;

			strcpy(line, model);
			return 0;
		}
	}

	return -1;
}

/*----------------------------------------------------------------------------*/
int getModelFromDt(char *line, FILE *dtFd) {
	char *model;

	if ((dtFd = fopen("/sys/firmware/devicetree/base/model", "r")) != NULL) {
		if (fgets(line, 120, dtFd) == NULL)
			return -1;

		if (wiringPiDebug)
			printf("piGpioLayout: %s: Hardware: %s\n", __func__, line);

		model = strcasestr(line, "odroid");
		if (!model)
			return -1;

		strcpy(line, model);
		return 0;
	}

	return -1;
}

/*----------------------------------------------------------------------------*/
int piGpioLayout (void) {
	FILE *cpuFd = NULL, *dtFd = NULL;
	char line[120];
	char *model, *modelCodename, *buf, *seps = "\t\n\v\f\r ";
	int sizeOfAssignedModelNames = 0;
	int i;

	if (getModelFromCpuinfo(line, cpuFd) != 0 && getModelFromDt(line, dtFd) != 0)
		wiringPiFailure(WPI_FATAL, "** This board is not an Odroid **");

	for (i = 1; i < (int)(sizeof(piModelNames) / sizeof(char*)); i++) {
		if (piModelNames[i] == NULL) {
			sizeOfAssignedModelNames = i - 1;
			break;
		}
	}

	i = strlen(line) - 1;
	while (i >= 0 && strchr(seps, line[i]) != NULL) {
		line[i] = '\0';
		i--;
	}

	buf = strchr(line, '-');
	modelCodename = buf != NULL ? buf : strchr(line, ' ');
	if (modelCodename == NULL)
		wiringPiFailure(WPI_FATAL, "** Model string on this board is not well formatted **");
	modelCodename++;

	libwiring.model = 0;
	for (i = 1; i <= sizeOfAssignedModelNames; i++) {
		model = strstr(piModelNames[i], "-");

		if (strcasestr(model, modelCodename) != NULL) {
			libwiring.model = i;
			break;
		}
	}

	switch (libwiring.model) {
		case MODEL_ODROID_C1:
			libwiring.maker = MAKER_AMLOGIC;
			libwiring.mem = 2;
			libwiring.rev = 1;
			break;
		case MODEL_ODROID_C2:
			libwiring.maker = MAKER_AMLOGIC;
			libwiring.mem = 3;
			{
				int fd = 0;
				char buf[2];

				if ((fd = open("/sys/class/odroid/boardrev", O_RDONLY)) < 0) {
					printf ("ERROR : file not found.(boardrev)\n");
					libwiring.rev = 1;
				} else {
					if (read(fd, buf, sizeof(buf)) < 0) {
						fprintf(stderr, "Unable to read from the file descriptor: %s \n", strerror(errno));
					}
					close(fd);
					libwiring.rev = atoi(buf) + 1;
				}
			}
			break;
		case MODEL_ODROID_XU3:
			libwiring.maker = MAKER_SAMSUNG;
			libwiring.mem = 3;
			libwiring.rev = 1;
			break;
		case MODEL_ODROID_N1:
			libwiring.maker = MAKER_ROCKCHIP;
			libwiring.mem = 4;
			libwiring.rev = 1;
			break;
		case MODEL_ODROID_N2:
			libwiring.maker = MAKER_AMLOGIC;
			libwiring.mem = 4;
			libwiring.rev = 1;
			break;
		case MODEL_ODROID_C4:
			libwiring.maker = MAKER_AMLOGIC;
			libwiring.mem = 4;
			libwiring.rev = 1;
			break;
		case MODEL_UNKNOWN:
		default:
			libwiring.model = MAKER_UNKNOWN;
			libwiring.maker = MAKER_UNKNOWN;
			libwiring.mem = 0;
			libwiring.rev = 0;
	}

	if (wiringPiDebug)
		printf("BoardRev: Returning revision: %d\n", libwiring.rev);

	return libwiring.rev;
}

/*----------------------------------------------------------------------------*/
/*
 * piBoardId:
 *	Return the real details of the board we have.
 *
 *  000a - Model ODROID C0/C1/C1+, Rev 1.0, 1024M, Hardkernel
 *  added :
 *  0100 - Model ODROID XU3/4, Rev 1.0, 2048M, Hardkernel
 *  added :
 *  02xx - Model ODROID C2, 2048M, Hardkernel
 *         Rev 1.0 : /sys/class/odroid/boardrev value is 0 (Dev board)
 *         Rev 1.1 : /sys/class/odroid/boardrev value is 1 (Mass board)
 *  03xx - Model ODROID N1, 4096M, Hardkernel
 *  04xx - Model ODROID N2, 4096M, Hardkernel
 *  05xx - Model ODROID C4, 4096M, Hardkernel
 */
/*----------------------------------------------------------------------------*/
void piBoardId (int *model, int *rev, int *mem, int *maker, int *warranty)
{
	// Call this first to make sure all's OK. Don't care about the result.
	(void)piGpioLayout () ;

	*model	= libwiring.model;
	*maker	= libwiring.maker;
	*rev	= libwiring.rev;
	*mem	= libwiring.mem;
	*warranty = 1;
}

/*----------------------------------------------------------------------------*/
/*
 * wpiPinToGpio:
 *	Translate a wiringPi Pin number to native GPIO pin number.
 *	Provided for external support.
 */
/*----------------------------------------------------------------------------*/
int wpiPinToGpio (int wpiPin)
{
	if (libwiring.getModeToGpio)
		return	libwiring.getModeToGpio(MODE_PINS, wpiPin);

	return	-1;
}

/*----------------------------------------------------------------------------*/
/*
 * physPinToGpio:
 *	Translate a physical Pin number to native GPIO pin number.
 *	Provided for external support.
 */
/*----------------------------------------------------------------------------*/
int physPinToGpio (int physPin)
{
	if (libwiring.getModeToGpio)
		return	libwiring.getModeToGpio(MODE_PHYS, physPin);

	return	-1;
}

/*----------------------------------------------------------------------------*/
/*
 * setPadDrive:
 *	Set the PAD driver value
 */
/*----------------------------------------------------------------------------*/
void setPadDrive (int pin, int value)
{
	if (libwiring.setPadDrive)
		if (libwiring.setPadDrive(pin, value) < 0)
			msg(MSG_WARN, "%s: Not available for pin %d. \n", __func__, pin);
}

/*----------------------------------------------------------------------------*/
/*
 * getPadDrive:
 *	Get the PAD driver value
 */
/*----------------------------------------------------------------------------*/
int getPadDrive (int pin)
{
	if (libwiring.getPadDrive)
		return	libwiring.getPadDrive(pin);

	return	-1;
}

/*----------------------------------------------------------------------------*/
/*
 * getAlt:
 *	Returns the ALT bits for a given port. Only really of-use
 *	for the gpio readall command (I think)
 */
/*----------------------------------------------------------------------------*/
int getAlt (int pin)
{
	if (libwiring.getAlt)
		return	libwiring.getAlt(pin);

	return	-1;
}

/*----------------------------------------------------------------------------*/
/*
 * pwmSetMode:
 *	Select the native "balanced" mode, or standard mark:space mode
 */
/*----------------------------------------------------------------------------*/

void pwmSetMode (int mode)
{
	if (libwiring.pwmSetMode) {
		libwiring.pwmSetMode(mode);
	} else {
		warn_msg(__func__);
	}
}

/*----------------------------------------------------------------------------*/
/*
 * pwmSetRange:
 *	Set the PWM range register. We set both range registers to the same
 *	value. If you want different in your own code, then write your own.
 */
/*----------------------------------------------------------------------------*/

void pwmSetRange (unsigned int range)
{
	if (libwiring.pwmSetRange) {
		libwiring.pwmSetRange(range);
	} else {
		warn_msg(__func__);
	}
}

/*----------------------------------------------------------------------------*/
/*
 * pwmSetClock:
 *	Set/Change the PWM clock. Originally my code, but changed
 *	(for the better!) by Chris Hall, <chris@kchall.plus.com>
 *	after further study of the manual and testing with a 'scope
 */
/*----------------------------------------------------------------------------*/

void pwmSetClock (int divisor)
{
	if (libwiring.pwmSetClock) {
		libwiring.pwmSetClock(divisor);
	} else {
		warn_msg(__func__);
	}
}

/*----------------------------------------------------------------------------*/
/*
 * getPUPD:
 *	Returns the PU/PD bits for a given port. Only really of-use
 *	for the gpio readall command (I think)
 */
/*----------------------------------------------------------------------------*/
int getPUPD (int pin)
{
	if (libwiring.getPUPD)
		return	libwiring.getPUPD(pin);

	return	-1;
}

/*----------------------------------------------------------------------------*/
/*
 * Core Functions
 */
/*----------------------------------------------------------------------------*/
void pinMode (int pin, int mode)
{
	if (libwiring.pinMode)
		if (libwiring.pinMode(pin, mode) < 0)
			msg(MSG_WARN, "%s: Not available for pin %d. \n", __func__, pin);

}

/*----------------------------------------------------------------------------*/
void pullUpDnControl (int pin, int pud)
{
	if (libwiring.pullUpDnControl)
		if (libwiring.pullUpDnControl(pin, pud) < 0)
			msg(MSG_WARN, "%s: Not available for pin %d. \n", __func__, pin);
}

/*----------------------------------------------------------------------------*/
int digitalRead (int pin)
{
	if (libwiring.digitalRead)
		return	libwiring.digitalRead(pin);

	return	-1;
}

/*----------------------------------------------------------------------------*/
void digitalWrite (int pin, int value)
{
	if (libwiring.digitalWrite)
		if (libwiring.digitalWrite(pin, value) < 0)
			msg(MSG_WARN, "%s: Not available for pin %d. \n", __func__, pin);
}

/*----------------------------------------------------------------------------*/
void pwmWrite(int pin, int value)
{
	if (libwiring.pwmWrite) {
		if (libwiring.pwmWrite(pin, value) < 0)
			msg(MSG_WARN, "%s: Not available for pin %d. \n", __func__, pin);
	} else {
		warn_msg(__func__);
	}
}

/*----------------------------------------------------------------------------*/
int analogRead (int pin)
{
	if (libwiring.analogRead)
		return	libwiring.analogRead(pin);

	return	-1;
}

/*----------------------------------------------------------------------------*/
void digitalWriteByte (const int value)
{
	if (libwiring.digitalWriteByte)
		if (libwiring.digitalWriteByte(value) < 0)
			msg(MSG_WARN, "%s: Not available. \n", __func__);
}

/*----------------------------------------------------------------------------*/
unsigned int digitalReadByte (void)
{
	if (libwiring.digitalReadByte)
		return	libwiring.digitalReadByte();

	return	-1;
}

/*----------------------------------------------------------------------------*/
int waitForInterrupt (int pin, int mS)
{
	int fd, x;
	uint8_t c;
	struct pollfd polls;

	if ((fd = libwiring.sysFds[PIN_NUM_CALC_SYSFD(pin)]) ==  -1)
		return	-2;

	// Setup poll structure
	polls.fd     = fd ;
	polls.events = POLLPRI | POLLERR ;

	// Wait for it ...
	x = poll (&polls, 1, mS) ;

	// If no error, do a dummy read to clear the interrupt
	//	A one character read appars to be enough.
	if (x > 0) {
		lseek (fd, 0, SEEK_SET) ;	// Rewind
		if (read (fd, &c, 1) < 0) {	// Read & clear
			fprintf(stderr, "Unable to read from the file descriptor: %s \n", strerror(errno));
		}
	}
	return x ;
}

/*----------------------------------------------------------------------------*/
static void *interruptHandler (void *arg)
{
	int myPin ;

	(void)piHiPri (55) ;	// Only effective if we run as root

	myPin   = *((int *) arg);
	free(arg);

	for (;;)
		if (waitForInterrupt (myPin, -1) > 0) {
			pthread_mutex_lock (&pinMutex) ;
			if (libwiring.isrFunctions[PIN_NUM_CALC_SYSFD(myPin)] == 0) {
				pthread_mutex_unlock (&pinMutex) ;
				break;
			}
			libwiring.isrFunctions [PIN_NUM_CALC_SYSFD(myPin)] () ;
			pthread_mutex_unlock (&pinMutex) ;
		}

	return NULL ;
}

/*----------------------------------------------------------------------------*/
int wiringPiISR (int pin, int mode, void (*function)(void))
{
	pthread_t threadId;
	const char *modeS;
	char fName   [64];
	char  pinS [8];
	pid_t pid;
	int   count, i;
	char  c;
	int   GpioPin;

	if (libwiring.mode == MODE_UNINITIALISED)
		return wiringPiFailure (
			WPI_FATAL,
			"wiringPiISR: wiringPi has not been initialised. " \
			"Unable to continue.\n") ;

	if (libwiring.getModeToGpio)
		GpioPin = libwiring.getModeToGpio(libwiring.mode, pin);
	else
		return wiringPiFailure (
			WPI_FATAL,
			"%s: getModeToGpio function not initialize!\n",
			__func__);

	// Now export the pin and set the right edge
	// We're going to use the gpio program to do this, so it assumes
	// a full installation of wiringPi. It's a bit 'clunky', but it
	// is a way that will work when we're running in "Sys" mode, as
	// a non-root user. (without sudo)
	if (mode != INT_EDGE_SETUP) {
		if (mode == INT_EDGE_FALLING)
			modeS = "falling" ;
		else if (mode == INT_EDGE_RISING)
			modeS = "rising" ;
		else
			modeS = "both" ;

		sprintf (pinS, "%d", GpioPin) ;

		FILE *export, *direct, *edge;
		int count;

		export = fopen("/sys/class/gpio/export", "w") ;
		fprintf (export, "%d\n", GpioPin) ;
		fclose (export) ;

		char fDirection[64];
		sprintf (fDirection, "/sys/class/gpio/gpio%d/direction", GpioPin) ;
		for(count = 5; count > 0; --count) {
			if((direct = fopen(fDirection, "w")) != NULL)
				break;
			else
				if(count != 1)
					sleep(1);
				else
					return wiringPiFailure (
						WPI_FATAL,
						"wiringPiISR: unable to open %s: %s\n",
						fDirection, strerror (errno)) ;
		}
		fprintf (direct, "in\n") ;
		fclose (direct) ;

		char fEdge[64];
		sprintf (fEdge, "/sys/class/gpio/gpio%d/edge", GpioPin) ;
		for(count = 5; count > 0; --count) {
			if((edge = fopen(fEdge, "w")) != NULL)
				break;
			else
				if(count != 1)
					sleep(1);
				else
					return wiringPiFailure (
						WPI_FATAL,
						"wiringPiISR: unable to open %s: %s\n",
						fEdge, strerror (errno)) ;
		}
		if (mode  == INT_EDGE_FALLING)
			fprintf (edge, "falling\n");
		else if (mode  == INT_EDGE_RISING)
			fprintf (edge, "rising\n");
		else if (mode == INT_EDGE_BOTH)
			fprintf (edge, "both\n");
		else
			fprintf (edge, "none\n");
		fclose (edge) ;
	}

	// Now pre-open the /sys/class node - but it may already be open if
	//	we are in Sys mode...

	if (libwiring.sysFds [PIN_NUM_CALC_SYSFD(GpioPin)] == -1) {
		sprintf (fName, "/sys/class/gpio/gpio%d/value", GpioPin) ;

		if ((libwiring.sysFds [PIN_NUM_CALC_SYSFD(GpioPin)] = open (fName, O_RDWR)) < 0)
			return wiringPiFailure (
				WPI_FATAL,
				"wiringPiISR: unable to open %s: %s\n",
				fName, strerror (errno)) ;
	}

	// Clear any initial pending interrupt
	ioctl (libwiring.sysFds [PIN_NUM_CALC_SYSFD(GpioPin)], FIONREAD, &count) ;
	for (i = 0 ; i < count ; ++i)
		if (read(libwiring.sysFds [PIN_NUM_CALC_SYSFD(GpioPin)], &c, 1) < 0) {
			fprintf(stderr, "Unable to read from the sysfs GPIO node: %s \n", strerror(errno));
		}

	pthread_mutex_lock (&pinMutex) ;
	int *pinNumber = malloc(sizeof(*pinNumber));
	*pinNumber= GpioPin ;
	pthread_create (&threadId, NULL, interruptHandler, pinNumber) ;
	pthread_mutex_unlock (&pinMutex) ;

	pthread_mutex_lock (&pinMutex) ;
	libwiring.isrFunctions [PIN_NUM_CALC_SYSFD(GpioPin)] = function ;
	libwiring.isrThreadIds [PIN_NUM_CALC_SYSFD(GpioPin)] = threadId ;
	pthread_mutex_unlock (&pinMutex) ;

	return 0 ;
}

/*----------------------------------------------------------------------------*/
int wiringPiISRCancel(int pin) {
	int GpioPin = -1;

	if (libwiring.mode == MODE_UNINITIALISED)
		return wiringPiFailure (
			WPI_FATAL,
			"wiringPiISRCancel: wiringPi has not been initialised. " \
			"Unable to continue.\n") ;

	if (libwiring.getModeToGpio)
		GpioPin = libwiring.getModeToGpio(libwiring.mode, pin);
	else
		return wiringPiFailure (
			WPI_FATAL,
			"%s: getModeToGpio function not initialize!\n",
			__func__);

	pthread_t threadId = libwiring.isrThreadIds[PIN_NUM_CALC_SYSFD(GpioPin)];

	if (pthread_cancel(threadId) < 0)
		return wiringPiFailure (
			WPI_FATAL,
			"%s: wiringPiISRCancel: Unregister for the interrupt pin failed!\n",
			__func__);
	else {
		pthread_mutex_lock (&pinMutex) ;
		libwiring.isrFunctions[PIN_NUM_CALC_SYSFD(GpioPin)] = NULL;
		libwiring.isrThreadIds[PIN_NUM_CALC_SYSFD(GpioPin)] = 0;
		pthread_mutex_unlock (&pinMutex) ;
	}

	return 0;
}

/*----------------------------------------------------------------------------*/
static void initialiseEpoch (void)
{
#ifdef	OLD_WAY
	struct timeval tv;

	gettimeofday (&tv, NULL) ;
	libwiring.epochMilli = (uint64_t)tv.tv_sec * (uint64_t)1000    +
				(uint64_t)(tv.tv_usec / 1000) ;
	libwiring.epochMicro = (uint64_t)tv.tv_sec * (uint64_t)1000000 +
				(uint64_t)(tv.tv_usec) ;
#else
	struct timespec ts;

	clock_gettime (CLOCK_MONOTONIC_RAW, &ts) ;
	libwiring.epochMilli = (uint64_t)ts.tv_sec * (uint64_t)1000    +
				(uint64_t)(ts.tv_nsec / 1000000L) ;
	libwiring.epochMicro = (uint64_t)ts.tv_sec * (uint64_t)1000000 +
				(uint64_t)(ts.tv_nsec /    1000L) ;
#endif
}

/*----------------------------------------------------------------------------*/
void delay (unsigned int howLong)
{
	struct timespec sleeper, dummy;

	sleeper.tv_sec  = (time_t)(howLong / 1000) ;
	sleeper.tv_nsec = (long)(howLong % 1000) * 1000000 ;

	nanosleep (&sleeper, &dummy) ;
}

/*----------------------------------------------------------------------------*/
void delayMicrosecondsHard (unsigned int howLong)
{
	struct timeval tNow, tLong, tEnd;

	gettimeofday (&tNow, NULL) ;
	tLong.tv_sec  = howLong / 1000000 ;
	tLong.tv_usec = howLong % 1000000 ;
	timeradd (&tNow, &tLong, &tEnd) ;

	while (timercmp (&tNow, &tEnd, <))
		gettimeofday (&tNow, NULL) ;
}

/*----------------------------------------------------------------------------*/
void delayMicroseconds (unsigned int howLong)
{
	struct timespec sleeper;
	unsigned int uSecs = howLong % 1000000;
	unsigned int wSecs = howLong / 1000000;

	if (howLong == 0)
		return ;
	else if (howLong < 100)
		delayMicrosecondsHard (howLong);
	else {
		sleeper.tv_sec  = wSecs;
		sleeper.tv_nsec = (long)(uSecs * 1000L);
		nanosleep (&sleeper, NULL);
	}
}

/*----------------------------------------------------------------------------*/
unsigned int millis (void)
{
	uint64_t now;

#ifdef	OLD_WAY
	struct timeval tv;

	gettimeofday (&tv, NULL);
	now = (uint64_t)tv.tv_sec * (uint64_t)1000 +
		(uint64_t)(tv.tv_usec / 1000);
#else
	struct  timespec ts;

	clock_gettime (CLOCK_MONOTONIC_RAW, &ts);
	now = (uint64_t)ts.tv_sec * (uint64_t)1000 +
		(uint64_t)(ts.tv_nsec / 1000000L);
#endif
	return (uint32_t)(now - libwiring.epochMilli);
}

/*----------------------------------------------------------------------------*/
unsigned int micros (void)
{
	uint64_t now;
#ifdef	OLD_WAY
	struct timeval tv;

	gettimeofday (&tv, NULL);
	now = (uint64_t)tv.tv_sec * (uint64_t)1000000 +
		(uint64_t)tv.tv_usec;
#else
	struct timespec ts;

	clock_gettime (CLOCK_MONOTONIC_RAW, &ts) ;
	now = (uint64_t)ts.tv_sec * (uint64_t)1000000 +
		(uint64_t)(ts.tv_nsec / 1000);
#endif
	return (uint32_t)(now - libwiring.epochMicro);
}

/*----------------------------------------------------------------------------*/
void wiringPiVersion (int *major, char **minor)
{
	*major = VERSION_MAJOR ;
	*minor = VERSION_MINOR ;
}

/*----------------------------------------------------------------------------*/
int wiringPiSetup (void)
{
	int i;
	if (wiringPiSetuped)
		return 0;

	wiringPiSetuped = TRUE;

	// libwiring init
	memset(&libwiring, 0x00, sizeof(struct libodroid));
	// sysFds init
	for(i = 0; i < 256; i++)
		libwiring.sysFds[i] = -1;
	// init wiringPi mode
	libwiring.mode = MODE_UNINITIALISED;

	if (getenv (ENV_DEBUG) != NULL)
		wiringPiDebug = TRUE;

	if (getenv (ENV_CODES) != NULL)
		wiringPiReturnCodes = TRUE;

	(void)piGpioLayout();

	if (wiringPiDebug) {
		printf ("wiringPi: wiringPiSetup called\n") ;
		printf ("Model Name  : %s\n", piModelNames[libwiring.model]);
		printf ("Model Maker : %s\n", piMakerNames[libwiring.maker]);
		printf ("Model MEM   : %d\n", libwiring.mem);
		printf ("Model REV   : %d\n", libwiring.rev);
	}

	switch (libwiring.model) {
	case MODEL_ODROID_C1:
		init_odroidc1(&libwiring);
	break;
	case MODEL_ODROID_C2:
		init_odroidc2(&libwiring);
	break;
	case MODEL_ODROID_XU3:
		init_odroidxu3(&libwiring);
	break;
	case MODEL_ODROID_N1:
		init_odroidn1(&libwiring);
	break;
	case MODEL_ODROID_N2:
		init_odroidn2(&libwiring);
	break;
	case MODEL_ODROID_C4:
		init_odroidc4(&libwiring);
	break;
	default:
		return wiringPiFailure (WPI_ALMOST,
			"wiringPiSetup: Unknown model\n");
	}

	initialiseEpoch ();

	libwiring.mode = MODE_PINS;
	return 0;
}

/*----------------------------------------------------------------------------*/
/*
 * wiringPiSetupGpio:
 *	Must be called once at the start of your program execution.
 *
 * GPIO setup: Initialises the system into GPIO Pin mode and uses the
 *	memory mapped hardware directly.
 */
/*----------------------------------------------------------------------------*/
int wiringPiSetupGpio (void)
{
	(void)wiringPiSetup ();

	if (wiringPiDebug)
		printf ("wiringPi: wiringPiSetupGpio called\n") ;

	libwiring.mode = MODE_GPIO;
	return 0 ;
}

/*----------------------------------------------------------------------------*/
/*
 * wiringPiSetupPhys:
 *	Must be called once at the start of your program execution.
 *
 * Phys setup: Initialises the system into Physical Pin mode and uses the
 *	memory mapped hardware directly.
 */
/*----------------------------------------------------------------------------*/
int wiringPiSetupPhys (void)
{
	(void)wiringPiSetup () ;

	if (wiringPiDebug)
		printf ("wiringPi: wiringPiSetupPhys called\n") ;

	libwiring.mode = MODE_PHYS ;
	return 0 ;
}

/*----------------------------------------------------------------------------*/
/*
 * wiringPiSetupSys:
 *	Must be called once at the start of your program execution.
 *
 * Initialisation (again), however this time we are using the /sys/class/gpio
 *	interface to the GPIO systems - slightly slower, but always usable as
 *	a non-root user, assuming the devices are already exported and setup correctly.
 */
/*----------------------------------------------------------------------------*/
int wiringPiSetupSys (void)
{
	int pin ;
	char fName [128] ;

	(void)wiringPiSetup();

	if (wiringPiDebug)
		printf ("wiringPi: wiringPiSetupSys called\n");

	// Open and scan the directory, looking for exported GPIOs, and pre-open
	//	the 'value' interface to speed things up for later

	for (pin = 0 ; pin < 256 ; ++pin)
	{
		switch (libwiring.model) {
		case	MODEL_ODROID_N1:
		case	MODEL_ODROID_N2:
			sprintf (fName, "/sys/class/gpio/gpio%d/value", pin + libwiring.pinBase);
			break;
		default:
			sprintf (fName, "/sys/class/gpio/gpio%d/value", pin);
			break;
		}

		libwiring.sysFds [pin] = open (fName, O_RDWR);
	}

	initialiseEpoch ();

	libwiring.mode = MODE_GPIO_SYS;
	return 0;
}

/*----------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/

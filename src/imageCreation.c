#ifdef WIN
#include<windows.h>
#else
#define _POSIX_C_SOURCE 200809L
#endif

#include<string.h> /* memset */
#include<stdlib.h>

#include<time.h>
#include "common.h"
#include "configurations.h"
#include "imageCreation.h"
#include "log.h"
#include "camera.h"
#include "kbhit.h"
#include "io/io.h"

static int saveErrCount = 0;  /* counting how often saving an image failed */
static int startErrCount = 0; /* counting how often the start of capture process failed */

static void callback(sParameterStruct * sSO2Parameters);

static void callback(sParameterStruct * sSO2Parameters)
{
	sSO2Parameters->fBufferReady = TRUE;

	/* Increment the Display Buffer Ready Count */
	sSO2Parameters->dBufferReadyCount++;
}

int startAquisition(sParameterStruct * sParameters_A,
	sParameterStruct * sParameters_B, sConfigStruct * config)
{

	log_message("Starting acquisition...\n");
	log_message("Press a key to exit\n");

	while (!kbhit()
	       && !(sParameters_A->fFifoOverFlow
		    || sParameters_B->fFifoOverFlow)) {
		aquire(sParameters_A, sParameters_B, config);
	}

	return 0;
}

int aquire(sParameterStruct * sParameters_A, sParameterStruct * sParameters_B, sConfigStruct * config)
{
	int status = 0;        /* status variable */

	/* get current time with milliseconds precision
	 * TODO: handle return codes
	 */
	getTime(sParameters_A->timestampBefore);
	getTime(sParameters_B->timestampBefore);

	/* Now start our capture, return control immediately back to program
	 * TODO: handle return codes
	 */
	status = camera_trigger(sParameters_A, callback);
	status = camera_trigger(sParameters_B, callback);

	if (!status) {
		/* if starting the capture was successful reset error counter to zero */
		startErrCount = 0;

		/* Wait for a user defined period between each camera trigger call */
		sleepMs(config->dInterFrameDelay);

		/* Wait here until either:
		 * (a) The user aborts the wait by pressing a key in the console window
		 * (b) The BufferReady event occurs indicating that the image is complete
		 * (c) The FIFO overflow event occurs indicating that the image is corrupt.
		 * Keep calling the sleep function to avoid burning CPU cycles */
		while (
			   !(sParameters_A->fBufferReady && sParameters_B->fBufferReady)
		    && !(sParameters_A->fFifoOverFlow && sParameters_B->fFifoOverFlow)
			&& !kbhit()
		){
			sleepMs(10);
		}

		/* Reset the buffer ready flags to false for next cycle */
		sParameters_A->fBufferReady = FALSE;
		sParameters_B->fBufferReady = FALSE;

		/* download the captured image */
		status = camera_get(sParameters_A);
		status = camera_get(sParameters_B);

		/* save the captured image */
		/* FIXME: Check return values */
		status = io_write(sParameters_A, config);
		status = io_write(sParameters_B, config);

		if (!status) {
			log_error("Saving an image failed. This is not fatal");
			/* if saving failed somehow more than 3 times program stops */
			saveErrCount++;
			if (saveErrCount >= 3) {
				log_error
				    ("Saving 3 images in a row failed. This is fatal");
				camera_abort(sParameters_A);
				camera_abort(sParameters_B);
				return 1;
			}
		} else {
			/* if saving was successful error counter is reset to zero */
			/* image counter is set +1 */
			config->dImageCounter++;
			config->dImageCounter++;

			saveErrCount = 0;
		}
		camera_abort(sParameters_A);
		camera_abort(sParameters_B);
	} else {
		log_error("Starting the acquisition failed. This is not fatal");
		/* if starting the capture failed more than 3 times program stops */
		startErrCount++;
		if (startErrCount >= 3) {
			log_error
			    ("starting the acquisition failed 3 times in a row. this is fatal");
			camera_abort(sParameters_A);
			camera_abort(sParameters_B);
			return 2;
		}
	}

	return 0;
}

time_t TimeFromTimeStruct(const timeStruct * pTime)
{
	struct tm tm;
	memset(&tm, 0, sizeof(tm));

	tm.tm_year = pTime->year - 1900;
	tm.tm_mon = pTime->mon - 1;
	tm.tm_mday = pTime->day;
	tm.tm_hour = pTime->hour;
	tm.tm_min = pTime->min;
	tm.tm_sec = pTime->sec;

	return mktime(&tm);
}

#ifdef WIN

/* WINDOWS VERSION */
int getTime(timeStruct * pTS)
{
	SYSTEMTIME time;
	GetSystemTime(&time);
	pTS->year = time.wYear;
	pTS->mon = time.wMonth;
	pTS->day = time.wDay;
	pTS->hour = time.wHour;
	pTS->min = time.wMinute;
	pTS->sec = time.wSecond;
	pTS->milli = time.wMilliseconds;
	return 0;
}

#else

/* POSIX VERSION */
int getTime(timeStruct * pTS)
{
	time_t seconds;
	long milliseconds;
	struct tm *tm;
	struct timespec spec;
	int stat;

	stat = clock_gettime(CLOCK_REALTIME, &spec);
	if (stat != 0) {
		log_error("clock_gettime failed. (posix) \n");
		return 1;
	}

	milliseconds = round(spec.tv_nsec / 1.0e6);
	seconds = spec.tv_sec;
	tm = gmtime(&seconds);

	pTS->year = tm->tm_year + 1900;
	pTS->mon = tm->tm_mon + 1;
	pTS->day = tm->tm_mday;
	pTS->hour = tm->tm_hour;
	pTS->min = tm->tm_min;
	pTS->sec = tm->tm_sec;
	pTS->milli = milliseconds;
	return 0;
}
#endif

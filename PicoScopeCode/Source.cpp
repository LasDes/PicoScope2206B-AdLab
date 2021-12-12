#include <iostream>
#include "ps2000aApi.h"
#include <stdio.h>
#include <string>

/* (Author's) Headers for Windows */
#ifdef _WIN32
#include "windows.h"
#include <conio.h>
#include "ps2000aApi.h"
#else
#include <sys/types.h>
#include <string.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#endif

#define Sleep(a) usleep(1000*a)
#define scanf_s scanf
#define fscanf_s fscanf
#define memcpy_s(a,b,c,d) memcpy(a,c,d)

//The author defines a number of C-style structs at the beginning of his file
//they seem like useful ways to manage data about the device or the data it's taking in
//so we'll just paste them here and use them as is

typedef enum
{
	ANALOGUE,
	DIGITAL,
	AGGREGATED,
	MIXED
}MODE;

typedef struct
{
	int16_t DCcoupled;
	int16_t range;
	int16_t enabled;
}CHANNEL_SETTINGS;

typedef struct tTriggerDirections
{
	PS2000A_THRESHOLD_DIRECTION channelA;
	PS2000A_THRESHOLD_DIRECTION channelB;
	PS2000A_THRESHOLD_DIRECTION channelC;
	PS2000A_THRESHOLD_DIRECTION channelD;
	PS2000A_THRESHOLD_DIRECTION ext;
	PS2000A_THRESHOLD_DIRECTION aux;
}TRIGGER_DIRECTIONS;

typedef struct tPwq
{
	PS2000A_PWQ_CONDITIONS* conditions;
	int16_t nConditions;
	PS2000A_THRESHOLD_DIRECTION direction;
	uint32_t lower;
	uint32_t upper;
	PS2000A_PULSE_WIDTH_TYPE type;
}PWQ;

typedef struct
{
	int16_t					handle;
	PS2000A_RANGE			firstRange;
	PS2000A_RANGE			lastRange;
	uint8_t					signalGenerator;
	uint8_t					ETS;
	int16_t                 channelCount;
	int16_t					maxValue;
	CHANNEL_SETTINGS		channelSettings[PS2000A_MAX_CHANNELS];
	int16_t					digitalPorts;
	int16_t					awgBufferSize;
	double					awgDACFrequency;
}UNIT;

// Use this struct to help with streaming data collection
typedef struct tBufferInfo
{
	UNIT* unit;
	MODE mode;
	int16_t** driverBuffers;
	int16_t** appBuffers;
	int16_t** driverDigBuffers;
	int16_t** appDigBuffers;

} BUFFER_INFO;

uint16_t inputRanges[PS2000A_MAX_RANGES] = { 10,
	20,
	50,
	100,
	200,
	500,
	1000,
	2000,
	5000,
	10000,
	20000,
	50000 };

//not sure why this was needed
//might go through code and replace these
//BOOLS with regular bool data type
//ORIGINAL:
/*
typedef enum enBOOL
{
	FALSE, 
	TRUE,
} BOOL;
*/


#define		BUFFER_SIZE 	1024
#define		DUAL_SCOPE		2
#define		QUAD_SCOPE		4

#define		AWG_DAC_FREQUENCY      20e6
#define		AWG_DAC_FREQUENCY_MSO  2e6
#define		AWG_PHASE_ACCUMULATOR  4294967296.0
#define PREF4 __stdcall //not sure why we're redefining here, might just take this out and throw __stdcall directly into the file

//Global variables
//adding on g_ convention for clarity in my code
int32_t g_cycles = 0;
uint32_t	g_timebase = 8;
int16_t     g_oversample = 1;
BOOL		g_scaleVoltages = TRUE;

BOOL     		g_ready = FALSE;
int32_t 		g_times[PS2000A_MAX_CHANNELS];
int16_t     	g_timeUnit;
int32_t      	g_sampleCount;
uint32_t		g_startIndex;
int16_t			g_autoStopped;
int16_t			g_trig = 0;
uint32_t		g_trigAt = 0;
int16_t			g_overflow = 0;

char BlockFile[20] = "block.txt";
char DigiBlockFile[20] = "digiblock.txt";
std::string filename = "data_";
//char StreamFile[50];

/*Seems to be Utility Function to detect keyboard interrupts, may
want to look into this closer/ modify how this works,
as the experiment is designed to be run for several months

Want to make it curious-freshman proof*/
//was this written specifically for linux?
/*
int32_t _kbhit()
{
	struct termios oldt, newt;
	int32_t bytesWaiting;
	tcgetattr(STDIN_FILENO, &oldt);
	newt = oldt;
	newt.c_lflag &= ~(ICANON | ECHO);
	tcsetattr(STDIN_FILENO, TCSANOW, &newt);
	setbuf(stdin, NULL);
	ioctl(STDIN_FILENO, FIONREAD, &bytesWaiting);

	tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
	return bytesWaiting;
}
*/
//I'll try to write a windows version?
//It also seems like there's a DIFFERENT kbinit function defined somewhere 
//in one of the header files I've #include'd
//we'll try that one if the bottom two are problematic
int16_t _kbhitinit()
{
	//GetKeyState returns a short int, which is 2 bytes
	//Looking at the windows documentation...
	//high-order bit is 1, the key is down; otherwise, it is up.
	//If the low - order bit is 1, the key is toggled
	//Q for quit?
	return (GetKeyState('Q') && 0x0001);
}


bool _kbhitpoll(int16_t init)
{
	//this should check if the key has been toggled since
		//can probably be fooled by quick double presses
	//just grab lowest order bit, then compare with initial state
	if ((GetKeyState('Q') && 0x0001) == init)
	{
		return false;
	}
	return true;
}


/****************************************************************************
* timeUnitsToString
*
* Converts PS2000A_TIME_UNITS enumeration to string (used for streaming mode)
*
****************************************************************************/
int8_t* timeUnitsToString(PS2000A_TIME_UNITS timeUnits)
{
	int8_t* timeUnitsStr = (int8_t*)"ns";

	switch (timeUnits)
	{
	case PS2000A_FS:

		timeUnitsStr = (int8_t*)"fs";
		break;

	case PS2000A_PS:

		timeUnitsStr = (int8_t*)"ps";
		break;

	case PS2000A_NS:

		timeUnitsStr = (int8_t*)"ns";
		break;

	case PS2000A_US:

		timeUnitsStr = (int8_t*)"us";
		break;

	case PS2000A_MS:

		timeUnitsStr = (int8_t*)"ms";
		break;

	case PS2000A_S:

		timeUnitsStr = (int8_t*)"s";
		break;

	default:

		timeUnitsStr = (int8_t*)"ns";
	}

	return timeUnitsStr;

}

/****************************************************************************
* mv_to_adc
*
* Convert a millivolt value into a 16-bit ADC count
*
*  (useful for setting trigger thresholds)
****************************************************************************/
int16_t mv_to_adc(int16_t mv, int16_t ch, UNIT* unit)
{
	return (mv * unit->maxValue) / inputRanges[ch];
}

/****************************************************************************
* adc_to_mv
*
* Convert an 16-bit ADC count into millivolts
****************************************************************************/
int32_t adc_to_mv(int32_t raw, int32_t ch, UNIT* unit)
{
	return (raw * inputRanges[ch]) / unit->maxValue;
}

/*Restore default settings to the device*/
void SetDefaults(UNIT* unit)
{
	PICO_STATUS status;
	int32_t i;

	status = ps2000aSetEts(unit->handle, PS2000A_ETS_OFF, 0, 0, NULL); // Turn off ETS

	for (i = 0; i < unit->channelCount; i++) // reset channels to most recent settings
	{
		status = ps2000aSetChannel(unit->handle, (PS2000A_CHANNEL)(PS2000A_CHANNEL_A + i),
			unit->channelSettings[PS2000A_CHANNEL_A + i].enabled,
			(PS2000A_COUPLING)unit->channelSettings[PS2000A_CHANNEL_A + i].DCcoupled,
			(PS2000A_RANGE)unit->channelSettings[PS2000A_CHANNEL_A + i].range, 0);
	}
}

void get_info(UNIT* unit)
{
	int8_t description[11][25] = { "Driver Version",
									"USB Version",
									"Hardware Version",
									"Variant Info",
									"Serial",
									"Cal Date",
									"Kernel",
									"Digital H/W",
									"Analogue H/W",
									"Firmware 1",
									"Firmware 2" };
	//had some issues with how some of these variables were declared
	//originally tried redefining them, but eventually just went with 
	//some casts and that seemed to work
	int16_t i, r = 0;
	//the way line is declared as a int8_t seems to be causing some issues
	int8_t line[80];
	//we'll try this instead
	//char line[80];
	PICO_STATUS status = PICO_OK;
	int16_t numChannels = DUAL_SCOPE;
	//Also issues with how channelNum is declared as int8_t
	int8_t channelNum = 0;
	//We'll try this instead
	//char channelNum = NULL;
	int8_t character = 'A';

	unit->signalGenerator = TRUE;
	unit->ETS = FALSE;
	unit->firstRange = PS2000A_20MV; // This is for new PicoScope 220X B, B MSO, 2405A and 2205A MSO models, older devices will have a first range of 50 mV
	unit->lastRange = PS2000A_20V;
	unit->channelCount = DUAL_SCOPE;
	unit->digitalPorts = 0;
	unit->awgBufferSize = PS2000A_MAX_SIG_GEN_BUFFER_SIZE;

	if (unit->handle)
	{
		for (i = 0; i < 11; i++)
		{
			status = ps2000aGetUnitInfo(unit->handle, (int8_t*)line, sizeof(line), &r, i);

			if (i == PICO_VARIANT_INFO)
			{
				// Check if device has four channels

				channelNum = line[1];
				numChannels = atoi((const char*)&channelNum);

				if (numChannels == QUAD_SCOPE)
				{
					unit->channelCount = QUAD_SCOPE;
				}

				// Set first range for voltage if device is a 2206/7/8, 2206/7/8A or 2205 MSO
				if (numChannels == DUAL_SCOPE)
				{
					if (strlen((const char*)line) == 4 || (strlen((const char*)line) == 5 && _strcmpi((const char*)&line[4], "A") == 0) || (_strcmpi((const char*)line, "2205MSO")) == 0) //using strcmpi here causes issues
					{
						unit->firstRange = PS2000A_50MV;
					}
				}

				// Check if device is an MSO 
				if (strstr((const char*)line, "MSO"))
				{
					unit->digitalPorts = 2;
				}

			}
			printf("%s: %s\n", description[i], line);
		}
	}
}

/*Author's SetTrigger Function, I'll include his summary comment below*/
/****************************************************************************
* SetTrigger
*
* Parameters
* - *unit               - pointer to the UNIT structure
* - *channelProperties  - pointer to the PS2000A_TRIGGER_CHANNEL_PROPERTIES structure
* - nChannelProperties  - the number of PS2000A_TRIGGER_CHANNEL_PROPERTIES elements in channelProperties
* - *triggerConditions  - pointer to the PS2000A_TRIGGER_CONDITIONS structure
* - nTriggerConditions  - the number of PS2000A_TRIGGER_CONDITIONS elements in triggerConditions
* - *directions         - pointer to the TRIGGER_DIRECTIONS structure
* - *pwq                - pointer to the pwq (Pulse Width Qualifier) structure
* - delay               - Delay time between trigger & first sample
* - auxOutputEnable     - Not used
* - autoTriggerMs       - timeout period if no trigger occurrs
* - *digitalDirections  - pointer to the PS2000A_DIGITAL_CHANNEL_DIRECTIONS structure
* - nDigitalDirections  - the number of PS2000A_DIGITAL_CHANNEL_DIRECTIONS elements in digitalDirections
*
* Returns			    - PICO_STATUS - to show success or if an error occurred
*
***************************************************************************/
PICO_STATUS SetTrigger(UNIT* unit,
	PS2000A_TRIGGER_CHANNEL_PROPERTIES* channelProperties,
	int16_t nChannelProperties,
	PS2000A_TRIGGER_CONDITIONS* triggerConditions,
	int16_t nTriggerConditions,
	TRIGGER_DIRECTIONS* directions,
	PWQ* pwq,
	uint32_t delay,
	int16_t auxOutputEnabled,
	int32_t autoTriggerMs,
	PS2000A_DIGITAL_CHANNEL_DIRECTIONS* digitalDirections,
	int16_t nDigitalDirections)
{
	PICO_STATUS status;

	if ((status = ps2000aSetTriggerChannelProperties(unit->handle,
		channelProperties,
		nChannelProperties,
		auxOutputEnabled,
		autoTriggerMs)) != PICO_OK)
	{
		printf("SetTrigger:ps2000aSetTriggerChannelProperties ------ Ox%8lx \n", status);
		return status;
	}

	if ((status = ps2000aSetTriggerChannelConditions(unit->handle, triggerConditions, nTriggerConditions)) != PICO_OK)
	{
		printf("SetTrigger:ps2000aSetTriggerChannelConditions ------ 0x%8lx \n", status);
		return status;
	}

	if ((status = ps2000aSetTriggerChannelDirections(unit->handle,
		directions->channelA,
		directions->channelB,
		directions->channelC,
		directions->channelD,
		directions->ext,
		directions->aux)) != PICO_OK)
	{
		printf("SetTrigger:ps2000aSetTriggerChannelDirections ------ 0x%08lx \n", status);
		return status;
	}

	if ((status = ps2000aSetTriggerDelay(unit->handle, delay)) != PICO_OK)
	{
		printf("SetTrigger:ps2000aSetTriggerDelay ------ 0x%08lx \n", status);
		return status;
	}

	if ((status = ps2000aSetPulseWidthQualifier(unit->handle,
		pwq->conditions,
		pwq->nConditions,
		pwq->direction,
		pwq->lower,
		pwq->upper,
		pwq->type)) != PICO_OK)
	{
		printf("SetTrigger:ps2000aSetPulseWidthQualifier ------ 0x%08lx \n", status);
		return status;
	}

	if (unit->digitalPorts)					// ps2000aSetTriggerDigitalPortProperties function only applies to MSO	
	{
		if ((status = ps2000aSetTriggerDigitalPortProperties(unit->handle,
			digitalDirections,
			nDigitalDirections)) != PICO_OK)
		{
			printf("SetTrigger:ps2000aSetTriggerDigitalPortProperties ------ 0x%08lx \n", status);
			return status;
		}
	}
	return status;
}

PICO_STATUS OpenDevice(UNIT* unit)
{
	int16_t value = 0;
	int16_t qinit = -1;
	int32_t i;
	PWQ pulseWidth;
	TRIGGER_DIRECTIONS directions;

	PICO_STATUS status = ps2000aOpenUnit(&(unit->handle), NULL);
	qinit = _kbhitinit();

	printf("Handle: %d\n", unit->handle);

	if (status != PICO_OK)
	{
		printf("Unable to open device\n");
		printf("Error code : %d\n", (int32_t)status);
		//Added print statement to account for different _kbhit() function
		printf("Press the \'Q\' key to exit the program.\n");
		while (!_kbhitpoll(qinit));
		exit(99); // exit program
	}

	printf("Device opened successfully, cycle %d\n\n", ++g_cycles);

	// setup devices
	get_info(unit);
	g_timebase = 1;

	ps2000aMaximumValue(unit->handle, &value);
	unit->maxValue = value;

	for (i = 0; i < unit->channelCount; i++)
	{
		unit->channelSettings[i].enabled = TRUE;
		unit->channelSettings[i].DCcoupled = TRUE;
		unit->channelSettings[i].range = PS2000A_5V;
	}

	memset(&directions, 0, sizeof(TRIGGER_DIRECTIONS));
	memset(&pulseWidth, 0, sizeof(PWQ));

	SetDefaults(unit);

	/* Trigger disabled	(for now) */
	SetTrigger(unit, NULL, 0, NULL, 0, &directions, &pulseWidth, 0, 0, 0, 0, 0);

	return status;
}

/****************************************************************************
* Callback
* used by ps2000a data streaming collection calls, on receipt of data.
* used to set global flags etc checked by user routines
****************************************************************************/
void PREF4 CallBackStreaming(int16_t handle,
	int32_t noOfSamples,
	uint32_t startIndex,
	int16_t overflow,
	uint32_t triggerAt,
	int16_t triggered,
	int16_t autoStop,
	void* pParameter)
{
	int32_t channel;
	int32_t digiPort;
	BUFFER_INFO* bufferInfo = NULL;

	if (pParameter != NULL)
	{
		bufferInfo = (BUFFER_INFO*)pParameter;
	}

	// used for streaming
	g_sampleCount = noOfSamples;
	g_startIndex = startIndex;
	g_autoStopped = autoStop;
	g_overflow = overflow;

	// flag to say done reading data
	g_ready = TRUE;

	// flags to show if & where a trigger has occurred
	g_trig = triggered;
	g_trigAt = triggerAt;

	if (bufferInfo != NULL && noOfSamples)
	{
		if (bufferInfo->mode == ANALOGUE)
		{
			for (channel = 0; channel < bufferInfo->unit->channelCount; channel++)
			{
				if (bufferInfo->unit->channelSettings[channel].enabled)
				{
					if (bufferInfo->appBuffers && bufferInfo->driverBuffers)
					{
						//I think it knows how to access the device buffer through assignments to the 
						//BUFFER_INFO* bufferInfo piece of data
							//treat as black box?
						if (bufferInfo->appBuffers[channel * 2] && bufferInfo->driverBuffers[channel * 2])
						{
							memcpy_s(&bufferInfo->appBuffers[channel * 2][startIndex], noOfSamples * sizeof(int16_t),
								&bufferInfo->driverBuffers[channel * 2][startIndex], noOfSamples * sizeof(int16_t));
						}
						if (bufferInfo->appBuffers[channel * 2 + 1] && bufferInfo->driverBuffers[channel * 2 + 1])
						{
							memcpy_s(&bufferInfo->appBuffers[channel * 2 + 1][startIndex], noOfSamples * sizeof(int16_t),
								&bufferInfo->driverBuffers[channel * 2 + 1][startIndex], noOfSamples * sizeof(int16_t));
						}
					}
				}
			}
		}
	}
}

/****************************************************************************
* ClearDataBuffers
*
* stops GetData writing values to memory that has been released
****************************************************************************/
PICO_STATUS ClearDataBuffers(UNIT* unit)
{
	int32_t i;
	PICO_STATUS status;

	for (i = 0; i < unit->channelCount; i++)
	{
		if ((status = ps2000aSetDataBuffers(unit->handle, (int16_t)i, NULL, NULL, 0, 0, PS2000A_RATIO_MODE_NONE)) != PICO_OK)
		{
			printf("ClearDataBuffers:ps2000aSetDataBuffers(channel %d) ------ 0x%08lx \n", i, status);
		}
	}


	for (i = 0; i < unit->digitalPorts; i++)
	{
		if ((status = ps2000aSetDataBuffer(unit->handle, (PS2000A_CHANNEL)(i + PS2000A_DIGITAL_PORT0), NULL, 0, 0, PS2000A_RATIO_MODE_NONE)) != PICO_OK)
		{
			printf("ClearDataBuffers:ps2000aSetDataBuffer(port 0x%X) ------ 0x%08lx \n", i + PS2000A_DIGITAL_PORT0, status);
		}
	}

	return status;
}


/* Author's Data Handling Function, modified
* Since in our application we're only using the analog
* coupling type, I'm going to take out the aggregate and
* digital parts of this function
* Also seems to set the device up for multi channel use
* We only need one channel, lots of this will be skipped with
* enabled channel checks, so it's probably safe to leave the
* multichannel stuff in for now
*/
/****************************************************************************
* Stream Data Handler
* - Used by the two stream data examples - untriggered and triggered
* Inputs:
* - unit - the unit to sample on
* - preTrigger - the number of samples in the pre-trigger phase
*					(0 if no trigger has been set)
***************************************************************************/
void StreamDataHandler(UNIT* unit, uint32_t preTrigger, MODE mode)
{
	int8_t* timeUnitsStr;

	int16_t  autostop;
	uint16_t portValue, portValueOR, portValueAND;
	uint32_t segmentIndex = 0;

	int16_t* buffers[PS2000A_MAX_CHANNEL_BUFFERS];
	int16_t* appBuffers[PS2000A_MAX_CHANNEL_BUFFERS];

	int32_t index = 0;
	int32_t totalSamples;
	int32_t bit;
	int32_t i, j; //these could get changed over to unsigned's

	int32_t sampleCount = 40000; /*make sure buffer large enough (this too)*/
	uint32_t postTrigger;
	uint32_t downsampleRatio = 1;
	uint32_t sampleInterval;
	uint32_t triggeredAt = 0;

	BUFFER_INFO bufferInfo;
	FILE* fp = NULL;

	PICO_STATUS status;
	PS2000A_TIME_UNITS timeUnits;
	PS2000A_RATIO_MODE ratioMode;
	int16_t qinit = -1;


	qinit = _kbhit();

	if (mode == ANALOGUE)		// Analogue 
	{
		for (i = 0; i < unit->channelCount; i++)
		{
			if (unit->channelSettings[i].enabled)
			{
				buffers[i * 2] = (int16_t*)malloc(sampleCount * sizeof(int16_t));
				buffers[i * 2 + 1] = (int16_t*)malloc(sampleCount * sizeof(int16_t));
				status = ps2000aSetDataBuffers(unit->handle, (int32_t)i, buffers[i * 2], buffers[i * 2 + 1], sampleCount, segmentIndex, PS2000A_RATIO_MODE_AGGREGATE);

				appBuffers[i * 2] = (int16_t*)malloc(sampleCount * sizeof(int16_t));
				appBuffers[i * 2 + 1] = (int16_t*)malloc(sampleCount * sizeof(int16_t));

				printf(status ? "StreamDataHandler:ps2000aSetDataBuffers(channel %ld) ------ 0x%08lx \n" : "", i, status);
			}
		}

		downsampleRatio = 20;  //might want to alter this (make it 1?)
		timeUnits = PS2000A_US; //what timescale do we actually want?
		sampleInterval = 1;
		ratioMode = PS2000A_RATIO_MODE_AGGREGATE; //might want to disable this
		postTrigger = 1000000; //different value here?
		autostop = TRUE;
	}

	bufferInfo.unit = unit;
	bufferInfo.mode = mode;
	bufferInfo.driverBuffers = buffers;
	bufferInfo.appBuffers = appBuffers;

	if (autostop)
	{
		printf("\nStreaming Data for %lu samples", postTrigger / downsampleRatio);

		if (preTrigger)	// we pass 0 for preTrigger if we're not setting up a trigger
		{
			printf(" after the trigger occurs\nNote: %lu Pre Trigger samples before Trigger arms\n\n", preTrigger / downsampleRatio);
		}
		else
		{
			printf("\n\n");
		}
	}
	else
	{
		printf("\nStreaming Data continually\n\n");
	}

	g_autoStopped = FALSE;

	status = ps2000aRunStreaming(unit->handle, &sampleInterval, timeUnits, preTrigger, postTrigger - preTrigger,
		autostop, downsampleRatio, ratioMode, (uint32_t)sampleCount);

	if (status == PICO_OK)
	{
		timeUnitsStr = timeUnitsToString(timeUnits);
		printf("Streaming data... (interval: %d %s) Press a key to stop\n", sampleInterval, timeUnitsStr);
	}
	else
	{
		printf("StreamDataHandler:ps2000aRunStreaming ------ 0x%08lx \n", status);
	}

	if (mode == ANALOGUE)
	{
		fopen_s(&fp, filename.c_str(), "w"); //might want to do something so that the file name changes each time so we don't overwrite

		//This is where we need to start messing with our own stuff 
		if (fp != NULL)
		{
			fprintf(fp, "For each of the %d Channels, results shown are....\n", unit->channelCount);
			fprintf(fp, "Maximum Aggregated value ADC Count & mV, Minimum Aggregated value ADC Count & mV\n\n");

			for (i = 0; i < unit->channelCount; i++)
			{
				if (unit->channelSettings[i].enabled)
				{
					fprintf(fp, "Max ADC   Max mV   Min ADC   Min mV"); //chnage this header based off of what data we're writing
				}
			}

			fprintf(fp, "\n");
		}
	}

	totalSamples = 0;

	// Capture data unless a key is pressed or the g_autoStopped flag is set in the streaming callback
	while (!_kbhitpoll(qinit) && !g_autoStopped)
	{
		/* Poll until data is received. Until then, GetStreamingLatestValues wont call the callback */
		g_ready = FALSE;

		status = ps2000aGetStreamingLatestValues(unit->handle, CallBackStreaming, &bufferInfo);
		index++;

		if (g_ready && g_sampleCount > 0) /* can be ready and have no data, if autoStop has fired */
		{
			if (g_trig)
			{
				triggeredAt = totalSamples + g_trigAt;		// calculate where the trigger occurred in the total samples collected
			}

			totalSamples += g_sampleCount;
			printf("\nCollected %3li samples, index = %5lu, Total: %6d samples ", g_sampleCount, g_startIndex, totalSamples);

			if (g_trig)
			{
				printf("Trig. at index %lu", triggeredAt);	// show where trigger occurred
			}

			//Do data processing here for peak info, then write to file in if blocks below

			//writes header to file above saying min/max adc counts, mv values
			// but here it seems to be writing the actual data values
			// am I misunderstanding or is it a bad label?
			for (i = g_startIndex; i < (int32_t)(g_startIndex + g_sampleCount); i++)
			{
				if (mode == ANALOGUE)
				{
					if (fp != NULL)
					{
						for (j = 0; j < unit->channelCount; j++)
						{
							//unsure why appbuffers has two j indices accessed
							//add in qualifier to only print if trig is triggered?
							if (unit->channelSettings[j].enabled)
							{
							//if (unit->channelSettings[j].enabled && g_trig)
							//{
								fprintf(fp,
									"%d, %d, %d, %d, ",
									appBuffers[j * 2][i],
									adc_to_mv(appBuffers[j * 2][i], unit->channelSettings[PS2000A_CHANNEL_A + j].range, unit),
									appBuffers[j * 2 + 1][i],
									adc_to_mv(appBuffers[j * 2 + 1][i], unit->channelSettings[PS2000A_CHANNEL_A + j].range, unit));
							}
						}

						fprintf(fp, "\n");
					}
					else
					{
						printf("Cannot open the file stream.txt for writing.\n");
					}

				}
			}
		}
	}

	ps2000aStop(unit->handle);

	if (!g_autoStopped)
	{
		printf("\nData collection aborted.\n");
		_getch();
	}

	if (g_overflow)
	{
		printf("Overflow on voltage range.\n");
	}

	if (fp != NULL)
	{
		fclose(fp);
	}

	if (mode == ANALOGUE)		// Only if we allocated these buffers
	{
		for (i = 0; i < unit->channelCount; i++)
		{
			if (unit->channelSettings[i].enabled)
			{
				free(buffers[i * 2]);
				free(buffers[i * 2 + 1]);

				free(appBuffers[i * 2]);
				free(appBuffers[i * 2 + 1]);
			}
		}
	}

	ClearDataBuffers(unit);
}


void CollectStreamingTriggered(UNIT* unit)
{
	int16_t triggerVoltage = mv_to_adc(1000, unit->channelSettings[PS2000A_CHANNEL_A].range, unit); // ChannelInfo stores ADC counts
	struct tPwq pulseWidth;

	SYSTEMTIME SystemTime;
	GetLocalTime(&SystemTime);
	
	filename += "Year_";
	filename += std::to_string(SystemTime.wYear);
	filename += "Month_";
	filename += std::to_string(SystemTime.wMonth);
	filename += "Day_";
	filename += std::to_string(SystemTime.wDay);
	filename += "Hour_";
	filename += std::to_string(SystemTime.wHour);
	filename += "Min_";
	filename += std::to_string(SystemTime.wMinute);
	filename += "Sec_";
	filename += std::to_string(SystemTime.wSecond);
	filename += ".txt";

	//might want to change triggervoltage here
	struct tPS2000ATriggerChannelProperties sourceDetails = { triggerVoltage,
		256 * 10,
		triggerVoltage,
		256 * 10,
		PS2000A_CHANNEL_A,
		PS2000A_LEVEL };

	struct tPS2000ATriggerConditions conditions = { PS2000A_CONDITION_TRUE,				// Channel A
		PS2000A_CONDITION_DONT_CARE,		// Channel B
		PS2000A_CONDITION_DONT_CARE,		// Channel C
		PS2000A_CONDITION_DONT_CARE,		// Channel D
		PS2000A_CONDITION_DONT_CARE,		// External
		PS2000A_CONDITION_DONT_CARE,		// aux
		PS2000A_CONDITION_DONT_CARE,		// PWQ
		PS2000A_CONDITION_DONT_CARE };		// digital

	struct tTriggerDirections directions = { PS2000A_RISING,			// Channel A
		PS2000A_NONE,			// Channel B
		PS2000A_NONE,			// Channel C
		PS2000A_NONE,			// Channel D
		PS2000A_NONE,			// External
		PS2000A_NONE };			// Aux

	memset(&pulseWidth, 0, sizeof(struct tPwq));

	printf("Collect streaming triggered...\n");
	printf("Data is written to disk file (%s)\n", filename.c_str());
	printf("Indicates when value rises past %d", g_scaleVoltages ?
		adc_to_mv(sourceDetails.thresholdUpper, unit->channelSettings[PS2000A_CHANNEL_A].range, unit)	// If scaleVoltages, print mV value
		: sourceDetails.thresholdUpper);																// else print ADC Count
	printf(g_scaleVoltages ? "mV\n" : "ADC Counts\n");
	printf("Press a key to start...\n");
	_getch();

	SetDefaults(unit);

	/* Trigger enabled
	* Rising edge
	* Threshold = 1000mV */
	//set trigger characteristics HERE
	//copying the function's arg explanations for convenience
	/*
	*Parameters
		* -*unit - pointer to the UNIT structure
		* -*channelProperties - pointer to the PS2000A_TRIGGER_CHANNEL_PROPERTIES structure
		* -nChannelProperties - the number of PS2000A_TRIGGER_CHANNEL_PROPERTIES elements in channelProperties
		* -*triggerConditions - pointer to the PS2000A_TRIGGER_CONDITIONS structure
		* -nTriggerConditions - the number of PS2000A_TRIGGER_CONDITIONS elements in triggerConditions
		* -*directions - pointer to the TRIGGER_DIRECTIONS structure
		* -*pwq - pointer to the pwq(Pulse Width Qualifier) structure
		* -delay - Delay time between trigger & first sample-> probably don't want a delay here as we're collecting the peak that sets the trigger
		* -auxOutputEnable - Not used
		* -autoTriggerMs - timeout period if no trigger occurrs->0 seems reasonable here, as we don't want a timeout
		* -*digitalDirections - pointer to the PS2000A_DIGITAL_CHANNEL_DIRECTIONS structure->not using digital
		* -nDigitalDirections - the number of PS2000A_DIGITAL_CHANNEL_DIRECTIONS elements in digitalDirections->not using digital
	*/
	SetTrigger(unit, &sourceDetails, 1, &conditions, 1, &directions, &pulseWidth, 0, 0, 0, 0, 0);

	StreamDataHandler(unit, 0, ANALOGUE);
}


/*Closes the device*/
void CloseDevice(UNIT* unit)
{
	ps2000aCloseUnit(unit->handle);
}


//going to try to piece together the example code here instead of writing from scratch
//I am not (yet) a systems programmer
int main()
{
	PICO_STATUS status;
	UNIT unit;
	status = OpenDevice(&unit);

	CollectStreamingTriggered(&unit);

	return 0;
}

//Old big garbo attempt 
/*
int main()
{
	//open scope unit
	//Read in wave form
	//check number of peaks/ check for two peaks
		//??
			//define some trigger event 
			// while loop
				//whenever trigger is hit, collect block of data->waveform
				//check for two peaks(how?)
				// if two peaks....->
		//If two peaks
			//record waveform
				//how do we output waveform data?	
			//find and record distance between peaks
				//how do we access/manipulate waveform data?->ps2000aGetValues() ?
				
	//and do it all over again

	//do I need to close it after each measurement and then reopen? Or just keep it open
	int16_t* handle = new int16_t;
	int8_t* serial = new int8_t;
	int16_t* status = new int16_t;
	int16_t* progressPercent = new int16_t;
	int16_t* complete = new int16_t;
	uint32_t nSamples = 1024; //different number here?
	int16_t buffer[1024];
	uint32_t* sampleInterval;
	int barWidth = 70;

	enum enBOOL
	{
		FALSE, TRUE
	};

	//*serial = (int8_t) GQ876 / 0095; //

	//ps2000aOpenUnit(handle, serial);
	//std::cout << "serial = " << (int8_t)*serial << std::endl;


	ps2000aOpenUnitAsync(status, NULL);
	if (*status)
	{
		std::cout << "Opening operation successfully started." << std::endl;
	}
	else
	{
		std::cout << "Open operation was disallowed because another open operation is in progress." << std::endl;
		return -1;
	}

	//check how the opening progress is going
	ps2000aOpenUnitProgress(handle, progressPercent, complete);

	//here's a cool progress bar
	//is there a way to get rid of the flashing white cursor?
	while (*complete != 1)
	{
		std::cout << "[";
		unsigned pos = barWidth * ( (float)(*progressPercent) / 100.0);
		for (unsigned i = 0; i < barWidth; i++) 
		{
			if (i < pos)
			{
				std::cout << "=";
			}
			else if (i == pos)
			{
				std::cout << ">";
			}
			else
			{
				std::cout << " ";
			}
		}
		std::cout << "] " << *progressPercent << " %\r";
		std::cout.flush();
		ps2000aOpenUnitProgress(handle, progressPercent, complete);
	}
	//Explicitly print out a 100% progress bar once it's through
	for (unsigned i = 0; i < barWidth; i++)
	{
		if (i < barWidth-1)
		{
			std::cout << "=";
		}
		else if (i == barWidth-1)
		{
			std::cout << ">";
		}
		else
		{
			std::cout << " ";
		}
	}
	std::cout << "] " << *progressPercent << " %\r";
	std::cout<<std::endl;
	std::cout << "The open operation is complete." << std::endl;

	//a few common error checks
	std::cout << "Checking to see if the scope opened correctly..." << std::endl;
	if (*handle == 0)
	{
		std::cout << "ERROR: No scope detected." << std::endl;
		return -1;
	}
	if (*handle == -1) //this doesn't seem to trigger even if a scope isn't plugged in
	{
		std::cout << "ERROR: Scope failed to open." << std::endl;
		return -1;
	}
	if (*handle > 0)
	{
		std::cout << "Scope successfully opened!" << std::endl;
	}

	//Set up the input channel:
		//*handle refers to the device opened and then labeled with that pointer
		//Using Channel A
		//Enabling Channel A
		//Setting its Couple Type to AC
		//Setting its voltage range to +- 1V
	ps2000aSetChannel(*handle, PS2000A_CHANNEL_A, (int16_t)1, PS2000A_AC, PS2000A_1V, 0.0);

	//Set up Triggering
		//can use ps2000aSetSimpleTrigger(), ps2000aSetTriggerChannelConditions(), ps2000aSetTriggerChannelDirections(), or ps2000aSetTriggerChannelProperties()
		//*handle refers to the device opened and then labeled with that pointer
		//a 1 enables the trigger
		//trigger on the input from channel a
		//threshold seems to depend on what the min/max voltage range is
			//Seems to scale from -32512 to +32512
			//So +-1V, trigger at .5V would be 16256
		//threshold direction
			//can be PS2000A_ ABOVE, BELOW, RISING, FALLING, or RISING_OR_FALLING
		//delay 
			//wait 0 sampling periods before sampling after trigger is hit
		//autoTrigger_ms
			//don't trigger unless trigger condition is met
	ps2000aSetSimpleTrigger(*handle, (int16_t)1, PS2000A_CHANNEL_A, (int16_t)16256, PS2000A_ABOVE, (uint32_t)0, (int16_t)0);

	//At this point we need to collect the data. 
	//We need to select between 4 different Sampling Modes
		//Streaming Mode seems to fit the most here (data passed directly to the PC)
			//pg 26 of programmer's guide
	
	//set data buffer, pg. 77
		//*handle refers to the device opened and then labeled with that pointer
		//taking data from channel A
		//pointer to the buffer array to hold the data
		//number of spaces in the buffer array
		//segment index, since we're only holding one segment at a time it's 0 (1?)
		//downsampling mode, set to none to recieve raw data
	ps2000aSetDataBuffer(*handle, PS2000A_CHANNEL_A, buffer, nSamples, (uint32_t)0, PS2000A_RATIO_MODE_NONE);

	//Fill in parameters with these function calls, then work out a way to display the data you captured from the sweep generator

	//ps2000aRunStreaming()
		//*handle refers to the device opened and then labeled with that pointer
		//request that one time unit passes between each sample taken
		//time unit for time between samples, could go even finer to PS2000A_FS
		//autoStop = TRUE tells the scope to stop collecting samples after maxPostTriggerSamples samples are collected following the trigger event
		//downsample ratios take up two arguments, not sure why. Going to assume we don't want to downsample data for now until I ask Dr. Brown about it
		//overviewBufferSize is the number of spaces we set aside in the ps2000aSetDataBuffer call
	
	*sampleInterval = 1; //time interval between samples collected
	uint32_t maxPreTriggerSamples = ; //need to work out the math on this based off of how our signals are coming in
	uint32_t maxPostTriggerSamples = ; //^same
	uint16_t autoStop = TRUE; //I think this is how this flag is used?

	ps2000aRunStreaming(*handle, sampleInterval, PS2000A_PS, maxPreTriggerSamples, maxPostTriggerSamples, autoStop, PS2000A_RATIO_MODE_NONE, PS2000A_RATIO_MODE_NONE, nSamples);

	//ps2000aGetValuesAsync()??
	// 
	// 
	//so next we need to call ps2000aGetStreamingLatestValues to grab the next block of data, but it uses a callback function, which I am entirely unfamiliar with
		//need to figure out how this works some other time
		//once we have the data, copy it to an array in this scope, do stuff with it?
	
	//need to write function that we'll pass to ps2000aGetStreamingLatestValues
	//this function should copy the latest values into something in this scope
	//ps2000aStreamingReady function needed
		//need paramater block to push to streamingready function
		//mem copy vs. mem copy s?  

	ps2000aGetStreamingLatestValues(*handle, ); 

	ps2000aStop();
	
	ps2000aCloseUnit();



	// start with ps2000aRunStreaming()
			//Grab data either with ps2000aGetStreamingLatestValues() or alternate functions to access stored data
				//manipulation to check for double peaks here
				// use matplotlibcpp for some nice plots?
			//ps2000aCloseUnit()
	//flash LED for various parts of code running?
	std::cout << "Hello, World!" << std::endl;
	
	return 0;
}
*/
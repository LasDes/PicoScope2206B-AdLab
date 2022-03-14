/*
Project written to collect data for a muon lifetime experiment using a picoscope 2206BMSO
Code is adapted from parts of example code provided on the PicoScope github page:
https://github.com/picotech/picosdk-c-examples/blob/master/ps2000a/ps2000aCon/ps2000aCon.c
*/
#include <limits> // infinity, max value of datatypes, etc.
#include <string> // string manipulation for file naming
#include <stdio.h> // input/output stuff
#include <iostream> // input/output stuff 
#include "ps2000aApi.h" // device-specific header

// (Author's) Headers for Windows
#ifdef _WIN32
#include "windows.h"
#include <conio.h>
#include "ps2000aApi.h" //device specific header
#else
#include <sys/types.h>
#include <string.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#endif

#include <ps2000aApi.h>
#ifndef PICO_STATUS
#include <PicoStatus.h> 
#endif

// The author defines a number of C-style structs at the beginning of his file
// they seem like useful ways to manage data about the device or the data it's taking in
// so we'll just paste them here and modify them as we go

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

// Not super necessary, but helps with clarity when the buffer is allocated
typedef struct tBufferInfo
{
	UNIT* unit;
	MODE mode;
	int16_t* driverBuffer; // originally int16_t** driverBuffers, only need one buffer since we're only using one channel
} BUFFER_INFO;

uint16_t inputRanges[PS2000A_MAX_RANGES] = {
	10,
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

/*
Some definitions by the author
(Lots more in his file, whatever wasn't used
wasn't copied over to here)
*/
#define		DUAL_SCOPE		2 // used in get_info function, number of channels in a DUAL_SCOPE is 2
#define		QUAD_SCOPE		4 // used in get_info function, number of channels in a QUAD_SCOPE is 4

/*
Visual Studio had an issue with the way he defined booleans
via enumeration, we'll just define TRUE and FALSE this way
*/
#define		FALSE			0
#define		TRUE			1

// Some global variables (author's)
uint32_t		g_timebase = 0; // originally set to 8 by author, we'll just go with 0 (fastest sampling rate)
int16_t			g_oversample = 1; // not used by the two system calls that take in this variable
BOOL			g_scaleVoltages = TRUE; // indicating for print statements whether to print values in terms of ADC counts (FALSE) or in mV (TRUE)
BOOL     		g_ready = FALSE; // global ready flag set by the callback

// Some global variables (mine)
BOOL			g_firstRun = TRUE; // keep track if this is the first time the scope collects data so we can avoid some redundant prints and such
BOOL			g_cinflag = FALSE; // flag used to keep track of cin's error status after taking in user input, FALSE (no flag raised) if ok, TRUE if error indicated by cin
int16_t			g_qinit = -1; // initialization variable for 'Q' key state for global quit 
int32_t			g_trigthresh; // threshold value for our initial trigger in mV
int32_t			g_peakthresh; // threshold for our peak finding alg in mV
int64_t			g_numwavestosaved = 0; // number of waveforms to save in a given session, might want to rename
uint64_t		g_nummultipeakevents = 0; // how many multi-peak events we've recorded so far (just peak info)
FILE* g_peakfp = NULL; // file to hold peak info, making this global so it doesn't have to be passed to every function
FILE* g_errorfp = NULL; // file to hold error log, making this global so it doesn't have to be passed to every function

// Prefixes for file names for raw waveform data, peak to peak info, and error logging
std::string wavefilename = "RAW_WAVEFORM_";
std::string peakfilename = "PEAK_INFO_";
std::string errorfilename = "ERROR_LOG_";

/****************************************************************************
* PICO_STATUStoString
*
* Converts PICO_STATUS codes to strings containing their defined names
* (There might have been a better way to do this than to manually copy
* all of them into the switch statement, but oh well it's done and it works)
*
* Parameters
* - status : the PICO_STATUS error code to be converted to a string
*
* Returns
* - std::string : contains the PICO_STATUS descriptor as a string
****************************************************************************/
std::string PICO_STATUStoString(PICO_STATUS status)
{
	std::string outputstr = "";

	switch (status)
	{
	case PICO_OK:

		outputstr = "PICO_OK";
		break;

	case PICO_MAX_UNITS_OPENED:

		outputstr = "PICO_MAX_UNITS_OPENED";
		break;

	case PICO_MEMORY_FAIL:
		outputstr = "PICO_MEMORY_FAIL";
		break;

	case PICO_NOT_FOUND:
		outputstr = "PICO_NOT_FOUND";
		break;

	case PICO_FW_FAIL:
		outputstr = "PICO_FW_FAIL";
		break;

	case PICO_OPEN_OPERATION_IN_PROGRESS:
		outputstr = "PICO_OPEN_OPERATION_IN_PROGRESS";
		break;

	case PICO_OPERATION_FAILED:
		outputstr = "PICO_OPERATION_FAILED";
		break;

	case PICO_NOT_RESPONDING:
		outputstr = "PICO_NOT_RESPONDING";
		break;

	case PICO_CONFIG_FAIL:
		outputstr = "PICO_CONFIG_FAIL";
		break;

	case PICO_KERNEL_DRIVER_TOO_OLD:
		outputstr = "PICO_KERNEL_DRIVER_TOO_OLD";
		break;

	case PICO_EEPROM_CORRUPT:
		outputstr = "PICO_EEPROM_CORRUPT";
		break;

	case PICO_OS_NOT_SUPPORTED:
		outputstr = "PICO_OS_NOT_SUPPORTED";
		break;

	case PICO_INVALID_HANDLE:
		outputstr = "PICO_INVALID_HANDLE";
		break;

	case PICO_INVALID_PARAMETER:
		outputstr = "PICO_INVALID_PARAMETER";
		break;

	case PICO_INVALID_TIMEBASE:
		outputstr = "PICO_INVALID_TIMEBASE";
		break;

	case PICO_INVALID_VOLTAGE_RANGE:
		outputstr = "PICO_INVALID_VOLTAGE_RANGE";
		break;

	case PICO_INVALID_CHANNEL:
		outputstr = "PICO_INVALID_CHANNEL";
		break;

	case PICO_INVALID_TRIGGER_CHANNEL:
		outputstr = "PICO_INVALID_TRIGGER_CHANNEL";
		break;

	case PICO_INVALID_CONDITION_CHANNEL:
		outputstr = "PICO_INVALID_CONDITION_CHANNEL";
		break;

	case PICO_NO_SIGNAL_GENERATOR:
		outputstr = "PICO_NO_SIGNAL_GENERATOR";
		break;

	case PICO_STREAMING_FAILED:
		outputstr = "PICO_STREAMING_FAILED";
		break;

	case PICO_BLOCK_MODE_FAILED:
		outputstr = "PICO_BLOCK_MODE_FAILED";
		break;

	case PICO_NULL_PARAMETER:
		outputstr = "PICO_NULL_PARAMETER";
		break;

	case PICO_ETS_MODE_SET:
		outputstr = "PICO_ETS_MODE_SET";
		break;

	case PICO_DATA_NOT_AVAILABLE:
		outputstr = "PICO_DATA_NOT_AVAILABLE";
		break;

	case PICO_STRING_BUFFER_TO_SMALL:
		outputstr = "PICO_STRING_BUFFER_TO_SMALL";
		break;

	case PICO_ETS_NOT_SUPPORTED:
		outputstr = "PICO_ETS_NOT_SUPPORTED";
		break;

	case PICO_AUTO_TRIGGER_TIME_TO_SHORT:
		outputstr = "PICO_AUTO_TRIGGER_TIME_TO_SHORT";
		break;

	case PICO_BUFFER_STALL:
		outputstr = "PICO_BUFFER_STALL";
		break;

	case PICO_TOO_MANY_SAMPLES:
		outputstr = "PICO_TOO_MANY_SAMPLES";
		break;

	case PICO_TOO_MANY_SEGMENTS:
		outputstr = "PICO_TOO_MANY_SEGMENTS";
		break;

	case PICO_PULSE_WIDTH_QUALIFIER:
		outputstr = "PICO_PULSE_WIDTH_QUALIFIER";
		break;

	case PICO_DELAY:
		outputstr = "PICO_DELAY";
		break;

	case PICO_SOURCE_DETAILS:
		outputstr = "PICO_SOURCE_DETAILS";
		break;

	case PICO_CONDITIONS:
		outputstr = "PICO_CONDITIONS";
		break;

	case PICO_USER_CALLBACK:
		outputstr = "PICO_USER_CALLBACK";
		break;

	case PICO_DEVICE_SAMPLING:
		outputstr = "PICO_DEVICE_SAMPLING";
		break;

	case PICO_NO_SAMPLES_AVAILABLE:
		outputstr = "PICO_NO_SAMPLES_AVAILABLE";
		break;

	case PICO_SEGMENT_OUT_OF_RANGE:
		outputstr = "PICO_SEGMENT_OUT_OF_RANGE";
		break;

	case PICO_BUSY:
		outputstr = "PICO_BUSY";
		break;

	case PICO_STARTINDEX_INVALID:
		outputstr = "PICO_STARTINDEX_INVALID";
		break;

	case PICO_INVALID_INFO:
		outputstr = "PICO_INVALID_INFO";
		break;

	case PICO_INFO_UNAVAILABLE:
		outputstr = "PICO_INFO_UNAVAILABLE";
		break;

	case PICO_INVALID_SAMPLE_INTERVAL:
		outputstr = "PICO_INVALID_SAMPLE_INTERVAL";
		break;

	case PICO_TRIGGER_ERROR:
		outputstr = "PICO_TRIGGER_ERROR";
		break;

	case PICO_MEMORY:
		outputstr = "PICO_MEMORY";
		break;

	case PICO_SIG_GEN_PARAM:
		outputstr = "PICO_SIG_GEN_PARAM";
		break;

	case PICO_SHOTS_SWEEPS_WARNING:
		outputstr = "PICO_SHOTS_SWEEPS_WARNING";
		break;

	case PICO_SIGGEN_TRIGGER_SOURCE:
		outputstr = "PICO_SIGGEN_TRIGGER_SOURCE";
		break;

	case PICO_AUX_OUTPUT_CONFLICT:
		outputstr = "PICO_AUX_OUTPUT_CONFLICT";
		break;

	case PICO_AUX_OUTPUT_ETS_CONFLICT:
		outputstr = "PICO_AUX_OUTPUT_ETS_CONFLICT";
		break;

	case PICO_WARNING_EXT_THRESHOLD_CONFLICT:
		outputstr = "PICO_WARNING_EXT_THRESHOLD_CONFLICT";
		break;

	case PICO_WARNING_AUX_OUTPUT_CONFLICT:
		outputstr = "PICO_WARNING_AUX_OUTPUT_CONFLICT";
		break;

	case PICO_SIGGEN_OUTPUT_OVER_VOLTAGE:
		outputstr = "PICO_SIGGEN_OUTPUT_OVER_VOLTAGE";
		break;

	case PICO_DELAY_NULL:
		outputstr = "PICO_DELAY_NULL";
		break;

	case PICO_INVALID_BUFFER:
		outputstr = "PICO_INVALID_BUFFER";
		break;

	case PICO_SIGGEN_OFFSET_VOLTAGE:
		outputstr = "PICO_SIGGEN_OFFSET_VOLTAGE";
		break;

	case PICO_SIGGEN_PK_TO_PK:
		outputstr = "PICO_SIGGEN_PK_TO_PK";
		break;

	case PICO_CANCELLED:
		outputstr = "PICO_CANCELLED";
		break;

	case PICO_SEGMENT_NOT_USED:
		outputstr = "PICO_SEGMENT_NOT_USED";
		break;

	case PICO_INVALID_CALL:
		outputstr = "PICO_INVALID_CALL";
		break;

	case PICO_GET_VALUES_INTERRUPTED:
		outputstr = "PICO_GET_VALUES_INTERRUPTED";
		break;

	case PICO_NOT_USED:
		outputstr = "PICO_NOT_USED";
		break;

	case PICO_INVALID_SAMPLERATIO:
		outputstr = "PICO_INVALID_SAMPLERATIO";
		break;

	case PICO_INVALID_STATE:
		outputstr = "PICO_INVALID_STATE";
		break;

	case PICO_NOT_ENOUGH_SEGMENTS:
		outputstr = "PICO_NOT_ENOUGH_SEGMENTS";
		break;

	case PICO_DRIVER_FUNCTION:
		outputstr = "PICO_DRIVER_FUNCTION";
		break;

	case PICO_RESERVED:
		outputstr = "PICO_RESERVED";
		break;

	case PICO_INVALID_COUPLING:
		outputstr = "PICO_INVALID_COUPLING";
		break;

	case PICO_BUFFERS_NOT_SET:
		outputstr = "PICO_BUFFERS_NOT_SET";
		break;

	case PICO_RATIO_MODE_NOT_SUPPORTED:
		outputstr = "PICO_RATIO_MODE_NOT_SUPPORTED";
		break;

	case PICO_RAPID_NOT_SUPPORT_AGGREGATION:
		outputstr = "PICO_RAPID_NOT_SUPPORT_AGGREGATION";
		break;

	case PICO_INVALID_TRIGGER_PROPERTY:
		outputstr = "PICO_INVALID_TRIGGER_PROPERTY";
		break;

	case PICO_INTERFACE_NOT_CONNECTED:
		outputstr = "PICO_INTERFACE_NOT_CONNECTED";
		break;

	case PICO_RESISTANCE_AND_PROBE_NOT_ALLOWED:
		outputstr = "PICO_RESISTANCE_AND_PROBE_NOT_ALLOWED";
		break;

	case PICO_POWER_FAILED:
		outputstr = "PICO_POWER_FAILED";
		break;

	case PICO_SIGGEN_WAVEFORM_SETUP_FAILED:
		outputstr = "PICO_SIGGEN_WAVEFORM_SETUP_FAILED";
		break;

	case PICO_FPGA_FAIL:
		outputstr = "PICO_FPGA_FAIL";
		break;

	case PICO_POWER_MANAGER:
		outputstr = "PICO_POWER_MANAGER";
		break;

	case PICO_INVALID_ANALOGUE_OFFSET:
		outputstr = "PICO_INVALID_ANALOGUE_OFFSET";
		break;

	case PICO_PLL_LOCK_FAILED:
		outputstr = "PICO_PLL_LOCK_FAILED";
		break;

	case PICO_ANALOG_BOARD:
		outputstr = "PICO_ANALOG_BOARD";
		break;

	case PICO_CONFIG_FAIL_AWG:
		outputstr = "PICO_CONFIG_FAIL_AWG";
		break;

	case PICO_INITIALISE_FPGA:
		outputstr = "PICO_INITIALISE_FPGA";
		break;

	case PICO_EXTERNAL_FREQUENCY_INVALID:
		outputstr = "PICO_EXTERNAL_FREQUENCY_INVALID";
		break;

	case PICO_CLOCK_CHANGE_ERROR:
		outputstr = "PICO_CLOCK_CHANGE_ERROR";
		break;

	case PICO_TRIGGER_AND_EXTERNAL_CLOCK_CLASH:
		outputstr = "PICO_TRIGGER_AND_EXTERNAL_CLOCK_CLASH";
		break;

	case PICO_PWQ_AND_EXTERNAL_CLOCK_CLASH:
		outputstr = "PICO_PWQ_AND_EXTERNAL_CLOCK_CLASH";
		break;

	case PICO_UNABLE_TO_OPEN_SCALING_FILE:
		outputstr = "PICO_UNABLE_TO_OPEN_SCALING_FILE";
		break;

	case PICO_MEMORY_CLOCK_FREQUENCY:
		outputstr = "PICO_MEMORY_CLOCK_FREQUENCY";
		break;

	case PICO_I2C_NOT_RESPONDING:
		outputstr = "PICO_I2C_NOT_RESPONDING";
		break;

	case PICO_NO_CAPTURES_AVAILABLE:
		outputstr = "PICO_NO_CAPTURES_AVAILABLE";
		break;

	case PICO_TOO_MANY_TRIGGER_CHANNELS_IN_USE:
		outputstr = "PICO_TOO_MANY_TRIGGER_CHANNELS_IN_USE";
		break;

	case PICO_INVALID_TRIGGER_DIRECTION:
		outputstr = "PICO_INVALID_TRIGGER_DIRECTION";
		break;

	case PICO_INVALID_TRIGGER_STATES:
		outputstr = "PICO_INVALID_TRIGGER_STATES";
		break;

	case PICO_NOT_USED_IN_THIS_CAPTURE_MODE:
		outputstr = "PICO_NOT_USED_IN_THIS_CAPTURE_MODE";
		break;

	case PICO_GET_DATA_ACTIVE:
		outputstr = "PICO_GET_DATA_ACTIVE";
		break;

	case PICO_IP_NETWORKED:
		outputstr = "PICO_IP_NETWORKED";
		break;

	case PICO_INVALID_IP_ADDRESS:
		outputstr = "PICO_INVALID_IP_ADDRESS";
		break;

	case PICO_IPSOCKET_FAILED:
		outputstr = "PICO_IPSOCKET_FAILED";
		break;

	case PICO_IPSOCKET_TIMEDOUT:
		outputstr = "PICO_IPSOCKET_TIMEDOUT";
		break;

	case PICO_SETTINGS_FAILED:
		outputstr = "PICO_SETTINGS_FAILED";
		break;

	case PICO_NETWORK_FAILED:
		outputstr = "PICO_NETWORK_FAILED";
		break;

	case PICO_WS2_32_DLL_NOT_LOADED:
		outputstr = "PICO_WS2_32_DLL_NOT_LOADED";
		break;

	case PICO_INVALID_IP_PORT:
		outputstr = "PICO_INVALID_IP_PORT";
		break;

	case PICO_COUPLING_NOT_SUPPORTED:
		outputstr = "PICO_COUPLING_NOT_SUPPORTED";
		break;

	case PICO_BANDWIDTH_NOT_SUPPORTED:
		outputstr = "PICO_BANDWIDTH_NOT_SUPPORTED";
		break;

	case PICO_INVALID_BANDWIDTH:
		outputstr = "PICO_INVALID_BANDWIDTH";
		break;

	case PICO_AWG_NOT_SUPPORTED:
		outputstr = "PICO_AWG_NOT_SUPPORTED";
		break;

	case PICO_ETS_NOT_RUNNING:
		outputstr = "PICO_ETS_NOT_RUNNING";
		break;

	case PICO_SIG_GEN_WHITENOISE_NOT_SUPPORTED:
		outputstr = "PICO_SIG_GEN_WHITENOISE_NOT_SUPPORTED";
		break;

	case PICO_SIG_GEN_WAVETYPE_NOT_SUPPORTED:
		outputstr = "PICO_SIG_GEN_WAVETYPE_NOT_SUPPORTED";
		break;

	case PICO_INVALID_DIGITAL_PORT:
		outputstr = "PICO_INVALID_DIGITAL_PORT";
		break;

	case PICO_INVALID_DIGITAL_CHANNEL:
		outputstr = "PICO_INVALID_DIGITAL_CHANNEL";
		break;

	case PICO_INVALID_DIGITAL_TRIGGER_DIRECTION:
		outputstr = "PICO_INVALID_DIGITAL_TRIGGER_DIRECTION";
		break;

	case PICO_SIG_GEN_PRBS_NOT_SUPPORTED:
		outputstr = "PICO_SIG_GEN_PRBS_NOT_SUPPORTED";
		break;

	case PICO_ETS_NOT_AVAILABLE_WITH_LOGIC_CHANNELS:
		outputstr = "PICO_ETS_NOT_AVAILABLE_WITH_LOGIC_CHANNELS";
		break;

	case PICO_WARNING_REPEAT_VALUE:
		outputstr = "PICO_WARNING_REPEAT_VALUE";
		break;

	case PICO_POWER_SUPPLY_CONNECTED:
		outputstr = "PICO_POWER_SUPPLY_CONNECTED";
		break;

	case PICO_POWER_SUPPLY_NOT_CONNECTED:
		outputstr = "PICO_POWER_SUPPLY_NOT_CONNECTED";
		break;

	case PICO_POWER_SUPPLY_REQUEST_INVALID:
		outputstr = "PICO_POWER_SUPPLY_REQUEST_INVALID";
		break;

	case PICO_POWER_SUPPLY_UNDERVOLTAGE:
		outputstr = "PICO_POWER_SUPPLY_UNDERVOLTAGE";
		break;

	case PICO_CAPTURING_DATA:
		outputstr = "PICO_CAPTURING_DATA";
		break;

	case PICO_USB3_0_DEVICE_NON_USB3_0_PORT:
		outputstr = "PICO_USB3_0_DEVICE_NON_USB3_0_PORT";
		break;

	case PICO_NOT_SUPPORTED_BY_THIS_DEVICE:
		outputstr = "PICO_NOT_SUPPORTED_BY_THIS_DEVICE";
		break;

	case PICO_INVALID_DEVICE_RESOLUTION:
		outputstr = "PICO_INVALID_DEVICE_RESOLUTION";
		break;

	case PICO_INVALID_NUMBER_CHANNELS_FOR_RESOLUTION:
		outputstr = "PICO_INVALID_NUMBER_CHANNELS_FOR_RESOLUTION";
		break;

	case PICO_CHANNEL_DISABLED_DUE_TO_USB_POWERED:
		outputstr = "PICO_CHANNEL_DISABLED_DUE_TO_USB_POWERED";
		break;

	case PICO_SIGGEN_DC_VOLTAGE_NOT_CONFIGURABLE:
		outputstr = "PICO_SIGGEN_DC_VOLTAGE_NOT_CONFIGURABLE";
		break;

	case PICO_NO_TRIGGER_ENABLED_FOR_TRIGGER_IN_PRE_TRIG:
		outputstr = "PICO_NO_TRIGGER_ENABLED_FOR_TRIGGER_IN_PRE_TRIG";
		break;

	case PICO_TRIGGER_WITHIN_PRE_TRIG_NOT_ARMED:
		outputstr = "PICO_TRIGGER_WITHIN_PRE_TRIG_NOT_ARMED";
		break;

	case PICO_TRIGGER_WITHIN_PRE_NOT_ALLOWED_WITH_DELAY:
		outputstr = "PICO_TRIGGER_WITHIN_PRE_NOT_ALLOWED_WITH_DELAY";
		break;

	case PICO_TRIGGER_INDEX_UNAVAILABLE:
		outputstr = "PICO_TRIGGER_INDEX_UNAVAILABLE";
		break;

	case PICO_AWG_CLOCK_FREQUENCY:
		outputstr = "PICO_AWG_CLOCK_FREQUENCY";
		break;

	case PICO_TOO_MANY_CHANNELS_IN_USE:
		outputstr = "PICO_TOO_MANY_CHANNELS_IN_USE";
		break;

	case PICO_NULL_CONDITIONS:
		outputstr = "PICO_NULL_CONDITIONS";
		break;

	case PICO_DUPLICATE_CONDITION_SOURCE:
		outputstr = "PICO_DUPLICATE_CONDITION_SOURCE";
		break;

	case PICO_INVALID_CONDITION_INFO:
		outputstr = "PICO_INVALID_CONDITION_INFO";
		break;

	case PICO_SETTINGS_READ_FAILED:
		outputstr = "PICO_SETTINGS_READ_FAILED";
		break;

	case PICO_SETTINGS_WRITE_FAILED:
		outputstr = "PICO_SETTINGS_WRITE_FAILED";
		break;

	case PICO_ARGUMENT_OUT_OF_RANGE:
		outputstr = "PICO_ARGUMENT_OUT_OF_RANGE";
		break;

	case PICO_HARDWARE_VERSION_NOT_SUPPORTED:
		outputstr = "PICO_HARDWARE_VERSION_NOT_SUPPORTED";
		break;

	case PICO_DIGITAL_HARDWARE_VERSION_NOT_SUPPORTED:
		outputstr = "PICO_DIGITAL_HARDWARE_VERSION_NOT_SUPPORTED";
		break;

	case PICO_ANALOGUE_HARDWARE_VERSION_NOT_SUPPORTED:
		outputstr = "PICO_ANALOGUE_HARDWARE_VERSION_NOT_SUPPORTED";
		break;

	case PICO_UNABLE_TO_CONVERT_TO_RESISTANCE:
		outputstr = "PICO_UNABLE_TO_CONVERT_TO_RESISTANCE";
		break;

	case PICO_DUPLICATED_CHANNEL:
		outputstr = "PICO_DUPLICATED_CHANNEL";
		break;

	case PICO_INVALID_RESISTANCE_CONVERSION:
		outputstr = "PICO_INVALID_RESISTANCE_CONVERSION";
		break;

	case PICO_INVALID_VALUE_IN_MAX_BUFFER:
		outputstr = "PICO_INVALID_VALUE_IN_MAX_BUFFER";
		break;

	case PICO_INVALID_VALUE_IN_MIN_BUFFER:
		outputstr = "PICO_INVALID_VALUE_IN_MIN_BUFFER";
		break;

	case PICO_SIGGEN_FREQUENCY_OUT_OF_RANGE:
		outputstr = "PICO_SIGGEN_FREQUENCY_OUT_OF_RANGE";
		break;

	case PICO_EEPROM2_CORRUPT:
		outputstr = "PICO_EEPROM2_CORRUPT";
		break;

	case PICO_EEPROM2_FAIL:
		outputstr = "PICO_EEPROM2_FAIL";
		break;

	case PICO_SERIAL_BUFFER_TOO_SMALL:
		outputstr = "PICO_SERIAL_BUFFER_TOO_SMALL";
		break;

	case PICO_SIGGEN_TRIGGER_AND_EXTERNAL_CLOCK_CLASH:
		outputstr = "PICO_SIGGEN_TRIGGER_AND_EXTERNAL_CLOCK_CLASH";
		break;

	case PICO_WARNING_SIGGEN_AUXIO_TRIGGER_DISABLED:
		outputstr = "PICO_WARNING_SIGGEN_AUXIO_TRIGGER_DISABLED";
		break;

	case PICO_SIGGEN_GATING_AUXIO_NOT_AVAILABLE:
		outputstr = "PICO_SIGGEN_GATING_AUXIO_NOT_AVAILABLE";
		break;

	case PICO_SIGGEN_GATING_AUXIO_ENABLED:
		outputstr = "PICO_SIGGEN_GATING_AUXIO_ENABLED";
		break;

	case PICO_RESOURCE_ERROR:
		outputstr = "PICO_RESOURCE_ERROR";
		break;

	case PICO_TEMPERATURE_TYPE_INVALID:
		outputstr = "PICO_TEMPERATURE_TYPE_INVALID";
		break;

	case PICO_TEMPERATURE_TYPE_NOT_SUPPORTED:
		outputstr = "PICO_TEMPERATURE_TYPE_NOT_SUPPORTED";
		break;

	case PICO_TIMEOUT:
		outputstr = "PICO_TIMEOUT";
		break;

	case PICO_DEVICE_NOT_FUNCTIONING:
		outputstr = "PICO_DEVICE_NOT_FUNCTIONING";
		break;

	case PICO_INTERNAL_ERROR:
		outputstr = "PICO_INTERNAL_ERROR";
		break;

	case PICO_MULTIPLE_DEVICES_FOUND:
		outputstr = "PICO_MULTIPLE_DEVICES_FOUND";
		break;

	case PICO_WARNING_NUMBER_OF_SEGMENTS_REDUCED:
		outputstr = "PICO_WARNING_NUMBER_OF_SEGMENTS_REDUCED";
		break;

	case PICO_CAL_PINS_STATES:
		outputstr = "PICO_CAL_PINS_STATES";
		break;

	case PICO_CAL_PINS_FREQUENCY:
		outputstr = "PICO_CAL_PINS_FREQUENCY";
		break;

	case PICO_CAL_PINS_AMPLITUDE:
		outputstr = "PICO_CAL_PINS_AMPLITUDE";
		break;

	case PICO_CAL_PINS_WAVETYPE:
		outputstr = "PICO_CAL_PINS_WAVETYPE";
		break;

	case PICO_CAL_PINS_OFFSET:
		outputstr = "PICO_CAL_PINS_OFFSET";
		break;

	case PICO_PROBE_FAULT:
		outputstr = "PICO_PROBE_FAULT";
		break;

	case PICO_PROBE_IDENTITY_UNKNOWN:
		outputstr = "PICO_PROBE_IDENTITY_UNKNOWN";
		break;

	case PICO_PROBE_POWER_DC_POWER_SUPPLY_REQUIRED:
		outputstr = "PICO_PROBE_POWER_DC_POWER_SUPPLY_REQUIRED";
		break;

	case PICO_PROBE_NOT_POWERED_WITH_DC_POWER_SUPPLY:
		outputstr = "PICO_PROBE_NOT_POWERED_WITH_DC_POWER_SUPPLY";
		break;

	case PICO_PROBE_CONFIG_FAILURE:
		outputstr = "PICO_PROBE_CONFIG_FAILURE";
		break;

	case PICO_PROBE_INTERACTION_CALLBACK:
		outputstr = "PICO_PROBE_INTERACTION_CALLBACK";
		break;

	case PICO_UNKNOWN_INTELLIGENT_PROBE:
		outputstr = "PICO_UNKNOWN_INTELLIGENT_PROBE";
		break;

	case PICO_INTELLIGENT_PROBE_CORRUPT:
		outputstr = "PICO_INTELLIGENT_PROBE_CORRUPT";
		break;

	case PICO_PROBE_COLLECTION_NOT_STARTED:
		outputstr = "PICO_PROBE_COLLECTION_NOT_STARTED";
		break;

	case PICO_PROBE_POWER_CONSUMPTION_EXCEEDED:
		outputstr = "PICO_PROBE_POWER_CONSUMPTION_EXCEEDED";
		break;

	case PICO_WARNING_PROBE_CHANNEL_OUT_OF_SYNC:
		outputstr = "PICO_WARNING_PROBE_CHANNEL_OUT_OF_SYNC";
		break;

	case PICO_ENDPOINT_MISSING:
		outputstr = "PICO_ENDPOINT_MISSING";
		break;

	case PICO_UNKNOWN_ENDPOINT_REQUEST:
		outputstr = "PICO_UNKNOWN_ENDPOINT_REQUEST";
		break;

	case PICO_ADC_TYPE_ERROR:
		outputstr = "PICO_ADC_TYPE_ERROR";
		break;

	case PICO_FPGA2_FAILED:
		outputstr = "PICO_FPGA2_FAILED";
		break;

	case PICO_FPGA2_DEVICE_STATUS:
		outputstr = "PICO_FPGA2_DEVICE_STATUS";
		break;

	case PICO_ENABLE_PROGRAM_FPGA2_FAILED:
		outputstr = "PICO_ENABLE_PROGRAM_FPGA2_FAILED";
		break;

	case PICO_NO_CHANNELS_OR_PORTS_ENABLED:
		outputstr = "PICO_NO_CHANNELS_OR_PORTS_ENABLED";
		break;

	case PICO_INVALID_RATIO_MODE:
		outputstr = "PICO_INVALID_RATIO_MODE";
		break;

	case PICO_READS_NOT_SUPPORTED_IN_CURRENT_CAPTURE_MODE:
		outputstr = "PICO_READS_NOT_SUPPORTED_IN_CURRENT_CAPTURE_MODE";
		break;

	case PICO_TRIGGER_READ_SELECTION_CHECK_FAILED:
		outputstr = "PICO_TRIGGER_READ_SELECTION_CHECK_FAILED";
		break;

	case PICO_DATA_READ1_SELECTION_CHECK_FAILED:
		outputstr = "PICO_DATA_READ1_SELECTION_CHECK_FAILED";
		break;

	case PICO_DATA_READ2_SELECTION_CHECK_FAILED:
		outputstr = "PICO_DATA_READ2_SELECTION_CHECK_FAILED";
		break;

	case PICO_DATA_READ3_SELECTION_CHECK_FAILED:
		outputstr = "PICO_DATA_READ3_SELECTION_CHECK_FAILED";
		break;

	case PICO_READ_SELECTION_OUT_OF_RANGE:
		outputstr = "PICO_READ_SELECTION_OUT_OF_RANGE";
		break;

	case PICO_MULTIPLE_RATIO_MODES:
		outputstr = "PICO_MULTIPLE_RATIO_MODES";
		break;

	case PICO_NO_SAMPLES_READ:
		outputstr = "PICO_NO_SAMPLES_READ";
		break;

	case PICO_RATIO_MODE_NOT_REQUESTED:
		outputstr = "PICO_RATIO_MODE_NOT_REQUESTED";
		break;

	case PICO_NO_USER_READ_REQUESTS_SET:
		outputstr = "PICO_NO_USER_READ_REQUESTS_SET";
		break;

	case PICO_ZERO_SAMPLES_INVALID:
		outputstr = "PICO_ZERO_SAMPLES_INVALID";
		break;

	case PICO_ANALOGUE_HARDWARE_MISSING:
		outputstr = "PICO_ANALOGUE_HARDWARE_MISSING";
		break;

	case PICO_ANALOGUE_HARDWARE_PINS:
		outputstr = "PICO_ANALOGUE_HARDWARE_PINS";
		break;

	case PICO_ANALOGUE_HARDWARE_SMPS_FAULT:
		outputstr = "PICO_ANALOGUE_HARDWARE_SMPS_FAULT";
		break;

	case PICO_DIGITAL_ANALOGUE_HARDWARE_CONFLICT:
		outputstr = "PICO_DIGITAL_ANALOGUE_HARDWARE_CONFLICT";
		break;

	case PICO_RATIO_MODE_BUFFER_NOT_SET:
		outputstr = "PICO_RATIO_MODE_BUFFER_NOT_SET";
		break;

	case PICO_RESOLUTION_NOT_SUPPORTED_BY_VARIANT:
		outputstr = "PICO_RESOLUTION_NOT_SUPPORTED_BY_VARIANT";
		break;

	case PICO_THRESHOLD_OUT_OF_RANGE:
		outputstr = "PICO_THRESHOLD_OUT_OF_RANGE";
		break;

	case PICO_INVALID_SIMPLE_TRIGGER_DIRECTION:
		outputstr = "PICO_INVALID_SIMPLE_TRIGGER_DIRECTION";
		break;

	case PICO_AUX_NOT_SUPPORTED:
		outputstr = "PICO_AUX_NOT_SUPPORTED";
		break;

	case PICO_NULL_DIRECTIONS:
		outputstr = "PICO_NULL_DIRECTIONS";
		break;

	case PICO_NULL_CHANNEL_PROPERTIES:
		outputstr = "PICO_NULL_CHANNEL_PROPERTIES";
		break;

	case PICO_TRIGGER_CHANNEL_NOT_ENABLED:
		outputstr = "PICO_TRIGGER_CHANNEL_NOT_ENABLED";
		break;

	case PICO_CONDITION_HAS_NO_TRIGGER_PROPERTY:
		outputstr = "PICO_CONDITION_HAS_NO_TRIGGER_PROPERTY";
		break;

	case PICO_RATIO_MODE_TRIGGER_MASKING_INVALID:
		outputstr = "PICO_RATIO_MODE_TRIGGER_MASKING_INVALID";
		break;

	case PICO_TRIGGER_DATA_REQUIRES_MIN_BUFFER_SIZE_OF_40_SAMPLES:
		outputstr = "PICO_TRIGGER_DATA_REQUIRES_MIN_BUFFER_SIZE_OF_40_SAMPLES";
		break;

	case PICO_NO_OF_CAPTURES_OUT_OF_RANGE:
		outputstr = "PICO_NO_OF_CAPTURES_OUT_OF_RANGE";
		break;

	case PICO_RATIO_MODE_SEGMENT_HEADER_DOES_NOT_REQUIRE_BUFFERS:
		outputstr = "PICO_RATIO_MODE_SEGMENT_HEADER_DOES_NOT_REQUIRE_BUFFERS";
		break;

	case PICO_FOR_SEGMENT_HEADER_USE_GETTRIGGERINFO:
		outputstr = "PICO_FOR_SEGMENT_HEADER_USE_GETTRIGGERINFO";
		break;

	case PICO_READ_NOT_SET:
		outputstr = "PICO_READ_NOT_SET";
		break;

	case PICO_ADC_SETTING_MISMATCH:
		outputstr = "PICO_ADC_SETTING_MISMATCH";
		break;

	case PICO_DATATYPE_INVALID:
		outputstr = "PICO_DATATYPE_INVALID";
		break;

	case PICO_RATIO_MODE_DOES_NOT_SUPPORT_DATATYPE:
		outputstr = "PICO_RATIO_MODE_DOES_NOT_SUPPORT_DATATYPE";
		break;

	case PICO_CHANNEL_COMBINATION_NOT_VALID_IN_THIS_RESOLUTION:
		outputstr = "PICO_CHANNEL_COMBINATION_NOT_VALID_IN_THIS_RESOLUTION";
		break;

	case PICO_USE_8BIT_RESOLUTION:
		outputstr = "PICO_USE_8BIT_RESOLUTION";
		break;

	case PICO_AGGREGATE_BUFFERS_SAME_POINTER:
		outputstr = "PICO_AGGREGATE_BUFFERS_SAME_POINTER";
		break;

	case PICO_OVERLAPPED_READ_VALUES_OUT_OF_RANGE:
		outputstr = "PICO_OVERLAPPED_READ_VALUES_OUT_OF_RANGE";
		break;

	case PICO_OVERLAPPED_READ_SEGMENTS_OUT_OF_RANGE:
		outputstr = "PICO_OVERLAPPED_READ_SEGMENTS_OUT_OF_RANGE";
		break;

	case PICO_CHANNELFLAGSCOMBINATIONS_ARRAY_SIZE_TOO_SMALL:
		outputstr = "PICO_CHANNELFLAGSCOMBINATIONS_ARRAY_SIZE_TOO_SMALL";
		break;

	case PICO_CAPTURES_EXCEEDS_NO_OF_SUPPORTED_SEGMENTS:
		outputstr = "PICO_CAPTURES_EXCEEDS_NO_OF_SUPPORTED_SEGMENTS";
		break;

	case PICO_TIME_UNITS_OUT_OF_RANGE:
		outputstr = "PICO_TIME_UNITS_OUT_OF_RANGE";
		break;

	case PICO_NO_SAMPLES_REQUESTED:
		outputstr = "PICO_NO_SAMPLES_REQUESTED";
		break;

	case PICO_INVALID_ACTION:
		outputstr = "PICO_INVALID_ACTION";
		break;

	case PICO_NO_OF_SAMPLES_NEED_TO_BE_EQUAL_WHEN_ADDING_BUFFERS:
		outputstr = "PICO_NO_OF_SAMPLES_NEED_TO_BE_EQUAL_WHEN_ADDING_BUFFERS";
		break;

	case PICO_WAITING_FOR_DATA_BUFFERS:
		outputstr = "PICO_WAITING_FOR_DATA_BUFFERS";
		break;

	case PICO_STREAMING_ONLY_SUPPORTS_ONE_READ:
		outputstr = "PICO_STREAMING_ONLY_SUPPORTS_ONE_READ";
		break;

	case PICO_CLEAR_DATA_BUFFER_INVALID:
		outputstr = "PICO_CLEAR_DATA_BUFFER_INVALID";
		break;

	case PICO_INVALID_ACTION_FLAGS_COMBINATION:
		outputstr = "PICO_INVALID_ACTION_FLAGS_COMBINATION";
		break;

	case PICO_BOTH_MIN_AND_MAX_NULL_BUFFERS_CANNOT_BE_ADDED:
		outputstr = "PICO_BOTH_MIN_AND_MAX_NULL_BUFFERS_CANNOT_BE_ADDED";
		break;

	case PICO_CONFLICT_IN_SET_DATA_BUFFERS_CALL_REMOVE_DATA_BUFFER_TO_RESET:
		outputstr = "PICO_CONFLICT_IN_SET_DATA_BUFFERS_CALL_REMOVE_DATA_BUFFER_TO_RESET";
		break;

	case PICO_REMOVING_DATA_BUFFER_ENTRIES_NOT_ALLOWED_WHILE_DATA_PROCESSING:
		outputstr = "PICO_REMOVING_DATA_BUFFER_ENTRIES_NOT_ALLOWED_WHILE_DATA_PROCESSING";
		break;

	case PICO_CYUSB_REQUEST_FAILED:
		outputstr = "PICO_CYUSB_REQUEST_FAILED";
		break;

	case PICO_STREAMING_DATA_REQUIRED:
		outputstr = "PICO_STREAMING_DATA_REQUIRED";
		break;

	case PICO_INVALID_NUMBER_OF_SAMPLES:
		outputstr = "PICO_INVALID_NUMBER_OF_SAMPLES";
		break;

	case PICO_INVALID_DISTRIBUTION:
		outputstr = "PICO_INVALID_DISTRIBUTION";
		break;

	case PICO_BUFFER_LENGTH_GREATER_THAN_INT32_T:
		outputstr = "PICO_BUFFER_LENGTH_GREATER_THAN_INT32_T";
		break;

	case PICO_PLL_MUX_OUT_FAILED:
		outputstr = "PICO_PLL_MUX_OUT_FAILED";
		break;

	case PICO_ONE_PULSE_WIDTH_DIRECTION_ALLOWED:
		outputstr = "PICO_ONE_PULSE_WIDTH_DIRECTION_ALLOWED";
		break;

	case PICO_EXTERNAL_TRIGGER_NOT_SUPPORTED:
		outputstr = "PICO_EXTERNAL_TRIGGER_NOT_SUPPORTED";
		break;

	case PICO_NO_TRIGGER_CONDITIONS_SET:
		outputstr = "PICO_NO_TRIGGER_CONDITIONS_SET";
		break;

	case PICO_NO_OF_CHANNEL_TRIGGER_PROPERTIES_OUT_OF_RANGE:
		outputstr = "PICO_NO_OF_CHANNEL_TRIGGER_PROPERTIES_OUT_OF_RANGE";
		break;

	case PICO_PROBE_COMPONENT_ERROR:
		outputstr = "PICO_PROBE_COMPONENT_ERROR";
		break;

	case PICO_INVALID_TRIGGER_CHANNEL_FOR_ETS:
		outputstr = "PICO_INVALID_TRIGGER_CHANNEL_FOR_ETS";
		break;

	case PICO_NOT_AVAILABLE_WHEN_STREAMING_IS_RUNNING:
		outputstr = "PICO_NOT_AVAILABLE_WHEN_STREAMING_IS_RUNNING";
		break;

	case PICO_INVALID_TRIGGER_WITHIN_PRE_TRIGGER_STATE:
		outputstr = "PICO_INVALID_TRIGGER_WITHIN_PRE_TRIGGER_STATE";
		break;

	case PICO_ZERO_NUMBER_OF_CAPTURES_INVALID:
		outputstr = "PICO_ZERO_NUMBER_OF_CAPTURES_INVALID";
		break;

	case PICO_TRIGGER_DELAY_OUT_OF_RANGE:
		outputstr = "PICO_TRIGGER_DELAY_OUT_OF_RANGE";
		break;

	case PICO_INVALID_THRESHOLD_DIRECTION:
		outputstr = "PICO_INVALID_THRESHOLD_DIRECTION";
		break;

	case PICO_INVALID_THRESHOLD_MODE:
		outputstr = "PICO_INVALID_THRESHOLD_MODE";
		break;

	case PICO_INVALID_VARIANT:
		outputstr = "PICO_INVALID_VARIANT";
		break;

	case PICO_MEMORY_MODULE_ERROR:
		outputstr = "PICO_MEMORY_MODULE_ERROR";
		break;

	case PICO_PULSE_WIDTH_QUALIFIER_LOWER_UPPER_CONFILCT:
		outputstr = "PICO_PULSE_WIDTH_QUALIFIER_LOWER_UPPER_CONFILCT";
		break;

	case PICO_PULSE_WIDTH_QUALIFIER_TYPE:
		outputstr = "PICO_PULSE_WIDTH_QUALIFIER_TYPE";
		break;

	case PICO_PULSE_WIDTH_QUALIFIER_DIRECTION:
		outputstr = "PICO_PULSE_WIDTH_QUALIFIER_DIRECTION";
		break;

	case PICO_THRESHOLD_MODE_OUT_OF_RANGE:
		outputstr = "PICO_THRESHOLD_MODE_OUT_OF_RANGE";
		break;

	case PICO_TRIGGER_AND_PULSEWIDTH_DIRECTION_IN_CONFLICT:
		outputstr = "PICO_TRIGGER_AND_PULSEWIDTH_DIRECTION_IN_CONFLICT";
		break;

	case PICO_THRESHOLD_UPPER_LOWER_MISMATCH:
		outputstr = "PICO_THRESHOLD_UPPER_LOWER_MISMATCH";
		break;

	case PICO_PULSE_WIDTH_LOWER_OUT_OF_RANGE:
		outputstr = "PICO_PULSE_WIDTH_LOWER_OUT_OF_RANGE";
		break;

	case PICO_PULSE_WIDTH_UPPER_OUT_OF_RANGE:
		outputstr = "PICO_PULSE_WIDTH_UPPER_OUT_OF_RANGE";
		break;

	case PICO_FRONT_PANEL_ERROR:
		outputstr = "PICO_FRONT_PANEL_ERROR";
		break;

	case PICO_FRONT_PANEL_MODE:
		outputstr = "PICO_FRONT_PANEL_MODE";
		break;

	case PICO_FRONT_PANEL_FEATURE:
		outputstr = "PICO_FRONT_PANEL_FEATURE";
		break;

	case PICO_NO_PULSE_WIDTH_CONDITIONS_SET:
		outputstr = "PICO_NO_PULSE_WIDTH_CONDITIONS_SET";
		break;

	case PICO_TRIGGER_PORT_NOT_ENABLED:
		outputstr = "PICO_TRIGGER_PORT_NOT_ENABLED";
		break;

	case PICO_DIGITAL_DIRECTION_NOT_SET:
		outputstr = "PICO_DIGITAL_DIRECTION_NOT_SET";
		break;

	case PICO_I2C_DEVICE_INVALID_READ_COMMAND:
		outputstr = "PICO_I2C_DEVICE_INVALID_READ_COMMAND";
		break;

	case PICO_I2C_DEVICE_INVALID_RESPONSE:
		outputstr = "PICO_I2C_DEVICE_INVALID_RESPONSE";
		break;

	case PICO_I2C_DEVICE_INVALID_WRITE_COMMAND:
		outputstr = "PICO_I2C_DEVICE_INVALID_WRITE_COMMAND";
		break;

	case PICO_I2C_DEVICE_ARGUMENT_OUT_OF_RANGE:
		outputstr = "PICO_I2C_DEVICE_ARGUMENT_OUT_OF_RANGE";
		break;

	case PICO_I2C_DEVICE_MODE:
		outputstr = "PICO_I2C_DEVICE_MODE";
		break;

	case PICO_I2C_DEVICE_SETUP_FAILED:
		outputstr = "PICO_I2C_DEVICE_SETUP_FAILED";
		break;

	case PICO_I2C_DEVICE_FEATURE:
		outputstr = "PICO_I2C_DEVICE_FEATURE";
		break;

	case PICO_I2C_DEVICE_VALIDATION_FAILED:
		outputstr = "PICO_I2C_DEVICE_VALIDATION_FAILED";
		break;

	case PICO_INTERNAL_HEADER_ERROR:
		outputstr = "PICO_INTERNAL_HEADER_ERROR";
		break;

	case PICO_FAILED_TO_WRITE_HARDWARE_FAULT:
		outputstr = "PICO_FAILED_TO_WRITE_HARDWARE_FAULT";
		break;

	case PICO_MSO_TOO_MANY_EDGE_TRANSITIONS_WHEN_USING_PULSE_WIDTH:
		outputstr = "PICO_MSO_TOO_MANY_EDGE_TRANSITIONS_WHEN_USING_PULSE_WIDTH";
		break;

	case PICO_INVALID_PROBE_LED_POSITION:
		outputstr = "PICO_INVALID_PROBE_LED_POSITION";
		break;

	case PICO_PROBE_LED_POSITION_NOT_SUPPORTED:
		outputstr = "PICO_PROBE_LED_POSITION_NOT_SUPPORTED";
		break;

	case PICO_DUPLICATE_PROBE_CHANNEL_LED_POSITION:
		outputstr = "PICO_DUPLICATE_PROBE_CHANNEL_LED_POSITION";
		break;

	case PICO_PROBE_LED_FAILURE:
		outputstr = "PICO_PROBE_LED_FAILURE";
		break;

	case PICO_PROBE_NOT_SUPPORTED_BY_THIS_DEVICE:
		outputstr = "PICO_PROBE_NOT_SUPPORTED_BY_THIS_DEVICE";
		break;

	case PICO_INVALID_PROBE_NAME:
		outputstr = "PICO_INVALID_PROBE_NAME";
		break;

	case PICO_NO_PROBE_COLOUR_SETTINGS:
		outputstr = "PICO_NO_PROBE_COLOUR_SETTINGS";
		break;

	case PICO_NO_PROBE_CONNECTED_ON_REQUESTED_CHANNEL:
		outputstr = "PICO_NO_PROBE_CONNECTED_ON_REQUESTED_CHANNEL";
		break;

	case PICO_PROBE_DOES_NOT_REQUIRE_CALIBRATION:
		outputstr = "PICO_PROBE_DOES_NOT_REQUIRE_CALIBRATION";
		break;

	case PICO_PROBE_CALIBRATION_FAILED:
		outputstr = "PICO_PROBE_CALIBRATION_FAILED";
		break;

	case PICO_PROBE_VERSION_ERROR:
		outputstr = "PICO_PROBE_VERSION_ERROR";
		break;

	case PICO_AUTO_TRIGGER_TIME_TOO_LONG:
		outputstr = "PICO_AUTO_TRIGGER_TIME_TOO_LONG";
		break;

	case PICO_MSO_POD_VALIDATION_FAILED:
		outputstr = "PICO_MSO_POD_VALIDATION_FAILED";
		break;

	case PICO_NO_MSO_POD_CONNECTED:
		outputstr = "PICO_NO_MSO_POD_CONNECTED";
		break;

	case PICO_DIGITAL_PORT_HYSTERESIS_OUT_OF_RANGE:
		outputstr = "PICO_DIGITAL_PORT_HYSTERESIS_OUT_OF_RANGE";
		break;

	case PICO_MSO_POD_FAILED_UNIT:
		outputstr = "PICO_MSO_POD_FAILED_UNIT";
		break;

	case PICO_ATTENUATION_FAILED:
		outputstr = "PICO_ATTENUATION_FAILED";
		break;

	case PICO_DC_50OHM_OVERVOLTAGE_TRIPPED:
		outputstr = "PICO_DC_50OHM_OVERVOLTAGE_TRIPPED";
		break;

	case PICO_NOT_RESPONDING_OVERHEATED:
		outputstr = "PICO_NOT_RESPONDING_OVERHEATED";
		break;

	case PICO_HARDWARE_CAPTURE_TIMEOUT:
		outputstr = "PICO_HARDWARE_CAPTURE_TIMEOUT";
		break;

	case PICO_HARDWARE_READY_TIMEOUT:
		outputstr = "PICO_HARDWARE_READY_TIMEOUT";
		break;

	case PICO_HARDWARE_CAPTURING_CALL_STOP:
		outputstr = "PICO_HARDWARE_CAPTURING_CALL_STOP";
		break;

	case PICO_TOO_FEW_REQUESTED_STREAMING_SAMPLES:
		outputstr = "PICO_TOO_FEW_REQUESTED_STREAMING_SAMPLES";
		break;

	case PICO_STREAMING_REREAD_DATA_NOT_AVAILABLE:
		outputstr = "PICO_STREAMING_REREAD_DATA_NOT_AVAILABLE";
		break;

	case PICO_STREAMING_COMBINATION_OF_RAW_DATA_AND_ONE_AGGREGATION_DATA_TYPE_ALLOWED:
		outputstr = "PICO_STREAMING_COMBINATION_OF_RAW_DATA_AND_ONE_AGGREGATION_DATA_TYPE_ALLOWED";
		break;

	case PICO_DEVICE_TIME_STAMP_RESET:
		outputstr = "PICO_DEVICE_TIME_STAMP_RESET";
		break;

	case PICO_TRIGGER_TIME_NOT_REQUESTED:
		outputstr = "PICO_TRIGGER_TIME_NOT_REQUESTED";
		break;

	case PICO_TRIGGER_TIME_BUFFER_NOT_SET:
		outputstr = "PICO_TRIGGER_TIME_BUFFER_NOT_SET";
		break;

	case PICO_TRIGGER_TIME_FAILED_TO_CALCULATE:
		outputstr = "PICO_TRIGGER_TIME_FAILED_TO_CALCULATE";
		break;

	case PICO_TRIGGER_WITHIN_A_PRE_TRIGGER_FAILED_TO_CALCULATE:
		outputstr = "PICO_TRIGGER_WITHIN_A_PRE_TRIGGER_FAILED_TO_CALCULATE";
		break;

	case PICO_TRIGGER_TIME_STAMP_NOT_REQUESTED:
		outputstr = "PICO_TRIGGER_TIME_STAMP_NOT_REQUESTED";
		break;

	case PICO_RATIO_MODE_TRIGGER_DATA_FOR_TIME_CALCULATION_DOES_NOT_REQUIRE_BUFFERS:
		outputstr = "PICO_RATIO_MODE_TRIGGER_DATA_FOR_TIME_CALCULATION_DOES_NOT_REQUIRE_BUFFERS";
		break;

	case PICO_RATIO_MODE_TRIGGER_DATA_FOR_TIME_CALCULATION_DOES_NOT_HAVE_BUFFERS:
		outputstr = "PICO_RATIO_MODE_TRIGGER_DATA_FOR_TIME_CALCULATION_DOES_NOT_HAVE_BUFFERS";
		break;

	case PICO_RATIO_MODE_TRIGGER_DATA_FOR_TIME_CALCULATION_USE_GETTRIGGERINFO:
		outputstr = "PICO_RATIO_MODE_TRIGGER_DATA_FOR_TIME_CALCULATION_USE_GETTRIGGERINFO";
		break;

	case PICO_STREAMING_DOES_NOT_SUPPORT_TRIGGER_RATIO_MODES:
		outputstr = "PICO_STREAMING_DOES_NOT_SUPPORT_TRIGGER_RATIO_MODES";
		break;

	case PICO_USE_THE_TRIGGER_READ:
		outputstr = "PICO_USE_THE_TRIGGER_READ";
		break;

	case PICO_USE_A_DATA_READ:
		outputstr = "PICO_USE_A_DATA_READ";
		break;

	case PICO_TRIGGER_READ_REQUIRES_INT16_T_DATA_TYPE:
		outputstr = "PICO_TRIGGER_READ_REQUIRES_INT16_T_DATA_TYPE";
		break;

	case PICO_SIGGEN_SETTINGS_MISMATCH:
		outputstr = "PICO_SIGGEN_SETTINGS_MISMATCH";
		break;

	case PICO_SIGGEN_SETTINGS_CHANGED_CALL_APPLY:
		outputstr = "PICO_SIGGEN_SETTINGS_CHANGED_CALL_APPLY";
		break;

	case PICO_SIGGEN_WAVETYPE_NOT_SUPPORTED:
		outputstr = "PICO_SIGGEN_WAVETYPE_NOT_SUPPORTED";
		break;

	case PICO_SIGGEN_TRIGGERTYPE_NOT_SUPPORTED:
		outputstr = "PICO_SIGGEN_TRIGGERTYPE_NOT_SUPPORTED";
		break;

	case PICO_SIGGEN_TRIGGERSOURCE_NOT_SUPPORTED:
		outputstr = "PICO_SIGGEN_TRIGGERSOURCE_NOT_SUPPORTED";
		break;

	case PICO_SIGGEN_FILTER_STATE_NOT_SUPPORTED:
		outputstr = "PICO_SIGGEN_FILTER_STATE_NOT_SUPPORTED";
		break;

	case PICO_SIGGEN_NULL_PARAMETER:
		outputstr = "PICO_SIGGEN_NULL_PARAMETER";
		break;

	case PICO_SIGGEN_EMPTY_BUFFER_SUPPLIED:
		outputstr = "PICO_SIGGEN_EMPTY_BUFFER_SUPPLIED";
		break;

	case PICO_SIGGEN_RANGE_NOT_SUPPLIED:
		outputstr = "PICO_SIGGEN_RANGE_NOT_SUPPLIED";
		break;

	case PICO_SIGGEN_BUFFER_NOT_SUPPLIED:
		outputstr = "PICO_SIGGEN_BUFFER_NOT_SUPPLIED";
		break;

	case PICO_SIGGEN_FREQUENCY_NOT_SUPPLIED:
		outputstr = "PICO_SIGGEN_FREQUENCY_NOT_SUPPLIED";
		break;

	case PICO_SIGGEN_SWEEP_INFO_NOT_SUPPLIED:
		outputstr = "PICO_SIGGEN_SWEEP_INFO_NOT_SUPPLIED";
		break;

	case PICO_SIGGEN_TRIGGER_INFO_NOT_SUPPLIED:
		outputstr = "PICO_SIGGEN_TRIGGER_INFO_NOT_SUPPLIED";
		break;

	case PICO_SIGGEN_CLOCK_FREQ_NOT_SUPPLIED:
		outputstr = "PICO_SIGGEN_CLOCK_FREQ_NOT_SUPPLIED";
		break;

	case PICO_SIGGEN_TOO_MANY_SAMPLES:
		outputstr = "PICO_SIGGEN_TOO_MANY_SAMPLES";
		break;

	case PICO_SIGGEN_DUTYCYCLE_OUT_OF_RANGE:
		outputstr = "PICO_SIGGEN_DUTYCYCLE_OUT_OF_RANGE";
		break;

	case PICO_SIGGEN_CYCLES_OUT_OF_RANGE:
		outputstr = "PICO_SIGGEN_CYCLES_OUT_OF_RANGE";
		break;

	case PICO_SIGGEN_PRESCALE_OUT_OF_RANGE:
		outputstr = "PICO_SIGGEN_PRESCALE_OUT_OF_RANGE";
		break;

	case PICO_SIGGEN_SWEEPTYPE_INVALID:
		outputstr = "PICO_SIGGEN_SWEEPTYPE_INVALID";
		break;

	case PICO_SIGGEN_SWEEP_WAVETYPE_MISMATCH:
		outputstr = "PICO_SIGGEN_SWEEP_WAVETYPE_MISMATCH";
		break;

	case PICO_SIGGEN_INVALID_SWEEP_PARAMETERS:
		outputstr = "PICO_SIGGEN_INVALID_SWEEP_PARAMETERS";
		break;

	case PICO_SIGGEN_SWEEP_PRESCALE_NOT_SUPPORTED:
		outputstr = "PICO_SIGGEN_SWEEP_PRESCALE_NOT_SUPPORTED";
		break;

	case PICO_AWG_OVER_VOLTAGE_RANGE:
		outputstr = "PICO_AWG_OVER_VOLTAGE_RANGE";
		break;

	case PICO_NOT_LOCKED_TO_REFERENCE_FREQUENCY:
		outputstr = "PICO_NOT_LOCKED_TO_REFERENCE_FREQUENCY";
		break;

	case PICO_PERMISSIONS_ERROR:
		outputstr = "PICO_PERMISSIONS_ERROR";
		break;

	case PICO_PORTS_WITHOUT_ANALOGUE_CHANNELS_ONLY_ALLOWED_IN_8BIT_RESOLUTION:
		outputstr = "PICO_PORTS_WITHOUT_ANALOGUE_CHANNELS_ONLY_ALLOWED_IN_8BIT_RESOLUTION";
		break;

	case PICO_ANALOGUE_FRONTEND_MISSING:
		outputstr = "PICO_ANALOGUE_FRONTEND_MISSING";
		break;

	case PICO_FRONT_PANEL_MISSING:
		outputstr = "PICO_FRONT_PANEL_MISSING";
		break;

	case PICO_ANALOGUE_FRONTEND_AND_FRONT_PANEL_MISSING:
		outputstr = "PICO_ANALOGUE_FRONTEND_AND_FRONT_PANEL_MISSING";
		break;

	case PICO_DIGITAL_BOARD_HARDWARE_ERROR:
		outputstr = "PICO_DIGITAL_BOARD_HARDWARE_ERROR";
		break;

	case PICO_FIRMWARE_UPDATE_REQUIRED_TO_USE_DEVICE_WITH_THIS_DRIVER:
		outputstr = "PICO_FIRMWARE_UPDATE_REQUIRED_TO_USE_DEVICE_WITH_THIS_DRIVER";
		break;

	case PICO_UPDATE_REQUIRED_NULL:
		outputstr = "PICO_UPDATE_REQUIRED_NULL";
		break;

	case PICO_FIRMWARE_UP_TO_DATE:
		outputstr = "PICO_FIRMWARE_UP_TO_DATE";
		break;

	case PICO_FLASH_FAIL:
		outputstr = "PICO_FLASH_FAIL";
		break;

	case PICO_INTERNAL_ERROR_FIRMWARE_LENGTH_INVALID:
		outputstr = "PICO_INTERNAL_ERROR_FIRMWARE_LENGTH_INVALID";
		break;

	case PICO_INTERNAL_ERROR_FIRMWARE_NULL:
		outputstr = "PICO_INTERNAL_ERROR_FIRMWARE_NULL";
		break;

	case PICO_FIRMWARE_FAILED_TO_BE_CHANGED:
		outputstr = "PICO_FIRMWARE_FAILED_TO_BE_CHANGED";
		break;

	case PICO_FIRMWARE_FAILED_TO_RELOAD:
		outputstr = "PICO_FIRMWARE_FAILED_TO_RELOAD";
		break;

	case PICO_FIRMWARE_FAILED_TO_BE_UPDATE:
		outputstr = "PICO_FIRMWARE_FAILED_TO_BE_UPDATE";
		break;

	case PICO_FIRMWARE_VERSION_OUT_OF_RANGE:
		outputstr = "PICO_FIRMWARE_VERSION_OUT_OF_RANGE";
		break;

	case PICO_NO_APPS_AVAILABLE:
		outputstr = "PICO_NO_APPS_AVAILABLE";
		break;

	case PICO_UNSUPPORTED_APP:
		outputstr = "PICO_UNSUPPORTED_APP";
		break;

	case PICO_ADC_POWERED_DOWN:
		outputstr = "PICO_ADC_POWERED_DOWN";
		break;

	case PICO_WATCHDOGTIMER:
		outputstr = "PICO_WATCHDOGTIMER";
		break;

	case PICO_IPP_NOT_FOUND:
		outputstr = "PICO_IPP_NOT_FOUND";
		break;

	case PICO_IPP_NO_FUNCTION:
		outputstr = "PICO_IPP_NO_FUNCTION";
		break;

	case PICO_IPP_ERROR:
		outputstr = "PICO_IPP_ERROR";
		break;

	case PICO_SHADOW_CAL_NOT_AVAILABLE:
		outputstr = "PICO_SHADOW_CAL_NOT_AVAILABLE";
		break;

	case PICO_SHADOW_CAL_DISABLED:
		outputstr = "PICO_SHADOW_CAL_DISABLED";
		break;

	case PICO_SHADOW_CAL_ERROR:
		outputstr = "PICO_SHADOW_CAL_ERROR";
		break;

	case PICO_SHADOW_CAL_CORRUPT:
		outputstr = "PICO_SHADOW_CAL_CORRUPT";
		break;

	case PICO_DEVICE_MEMORY_OVERFLOW:
		outputstr = "PICO_DEVICE_MEMORY_OVERFLOW";
		break;

	case PICO_ADC_TEST_FAILURE:
		outputstr = "PICO_ADC_TEST_FAILURE";
		break;

	case PICO_RESERVED_1:
		outputstr = "PICO_RESERVED_1";
		break;

	case PICO_SOURCE_NOT_READY:
		outputstr = "PICO_SOURCE_NOT_READY";
		break;

	case PICO_SOURCE_INVALID_BAUD_RATE:
		outputstr = "PICO_SOURCE_INVALID_BAUD_RATE";
		break;

	case PICO_SOURCE_NOT_OPENED_FOR_WRITE:
		outputstr = "PICO_SOURCE_NOT_OPENED_FOR_WRITE";
		break;

	case PICO_SOURCE_FAILED_TO_WRITE_DEVICE:
		outputstr = "PICO_SOURCE_FAILED_TO_WRITE_DEVICE";
		break;

	case PICO_SOURCE_EEPROM_FAIL:
		outputstr = "PICO_SOURCE_EEPROM_FAIL";
		break;

	case PICO_SOURCE_EEPROM_NOT_PRESENT:
		outputstr = "PICO_SOURCE_EEPROM_NOT_PRESENT";
		break;

	case PICO_SOURCE_EEPROM_NOT_PROGRAMMED:
		outputstr = "PICO_SOURCE_EEPROM_NOT_PROGRAMMED";
		break;

	case PICO_SOURCE_LIST_NOT_READY:
		outputstr = "PICO_SOURCE_LIST_NOT_READY";
		break;

	case PICO_SOURCE_FTD2XX_NOT_FOUND:
		outputstr = "PICO_SOURCE_FTD2XX_NOT_FOUND";
		break;

	case PICO_SOURCE_FTD2XX_NO_FUNCTION:
		outputstr = "PICO_SOURCE_FTD2XX_NO_FUNCTION";
		break;

	default:

		outputstr = "UNKNOWN_ERROR";
	}

	return outputstr;
}

/****************************************************************************
* cinReset
*
* Used for clearing the input buffer after taking in user input using
* std::cin
* First calls cin.clear()
* Then calls std::cin.ignore() to help with clearing the input buffer for future
* inputs
* "Pushes" the max macro before calling std::cin.ignore() so that
* std::numeric_limits<std::streamsize>::max() gets read correctly, otherwise gets
* read as the max macro which just returns the largest of two arguments passed
*
* Parameters
* - none
*
* Returns
* - none
****************************************************************************/
inline void cinReset()
{
	std::cin.clear();
#pragma push_macro("max")
#undef max
	std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
#pragma pop_macro("max")
}

/****************************************************************************
* _kbhitinit
*
* Initializes the _kbhit routine by grabbing the current state of the `Q` key
* GetKeyState returns a short int, which is 2 bytes
*	- Looking at the windows documentation...
*		- If the high-order bit is 1, the key is down; otherwise, it is up.
*		- If the low-order bit is 1, the key is toggled
*			-(toggled/ untoggled is what we care about)
*		- 'Q' key for quit
*
* Parameters
* - none
*
* Returns
* - int16_t : indicates the initial state of the 'Q' key (toggled/ untoggled)
****************************************************************************/
inline int16_t _kbhitinit()
{
	return (GetKeyState('Q') & 0x0001);
}

/****************************************************************************
* _kbhitpoll
*
* Checks if the Q key has been toggled from its inital value of init after
* the _kbhitinit function has been called
*	- might be able to fool with quick double presses
*	- just grab lowest order bit, then compare with initial state
*
* Parameters
* - init : the initial value of the key's toggle state
*
* Returns
* - BOOL : indicates whether the key has been toggled (hasn't been toggled
* returns FALSE, otherwise returns TRUE)
****************************************************************************/
inline BOOL _kbhitpoll(int16_t init)
{
	if ((GetKeyState('Q') & 0x0001) == init)
	{
		return FALSE;
	}
	return TRUE;
}

/****************************************************************************
* adc_to_mv
*
* Convert an 16-bit ADC count into millivolts
*	-seems to take in params as int32_t's (even tho they're just 16's)
*	to avoid potential overflow with multiplication
*	-Max value of int16_t is 32,767, so casting back to an int16_t at the end
*	should cause no issues since our scope only goes up to ±10,000mV
*		-If our scope went to the largest range of ±50,000mV, in which case we'd
*		have to return the value as a int32_t (but then the data handling process
*		would have to be rewritten for int32_t's, so this isn't much of a concern
*		for this application
*
* Parameters
* - raw : raw ADC count to be converted
* - ch : channel being used, in order to find the select voltage range to
	convert into
* - unit : pointer to the UNIT structure, where the handle is stored
*
* Returns
* - int16_t : contains the adc count converted to mV
****************************************************************************/
inline int16_t adc_to_mv(int32_t raw, int32_t ch, UNIT* unit)
{
	return (int16_t)((raw * inputRanges[ch]) / unit->maxValue);
}


/****************************************************************************
* mv_to_adc
*
* Convert a millivolt value into a 16-bit ADC count
*	- useful for setting trigger thresholds
*
* Parameters
* - mv : Voltage value in mv to be converted
* - ch : channel being used, in order to find the select voltage range to
	convert into
* - unit : pointer to the UNIT structure, where the handle is stored
*
* Returns
* - int16_t : contains the mV converted to an equivalent ADC count under the
* device's current settings
****************************************************************************/
inline int16_t mv_to_adc(int16_t mv, int16_t ch, UNIT* unit)
{
	return (mv * unit->maxValue) / inputRanges[ch];
}

/****************************************************************************
* timeInfoFunc
*
* Returns a string containing info about the current data and time using
* some of the Windows API functions
* - useful for print statements
*
* Parameters
* - none
*
* Returns
* - std::string : contains labels and time information for the current year,
* month, day, hour, minute, and second
****************************************************************************/
std::string timeInfotoString()
{
	std::string accum;

	SYSTEMTIME SystemTime; // Windows variable for storing date/time info
	GetLocalTime(&SystemTime); //grab the current date/time info

	accum += "Year_"; // year label
	accum += std::to_string(SystemTime.wYear); // Windows API call to grab year value
	accum += "_Month_"; // month label
	accum += std::to_string(SystemTime.wMonth); // Windows API call to grab month value
	accum += "_Day_"; // day label
	accum += std::to_string(SystemTime.wDay); // Windows API call to grab day value
	accum += "_Hour_"; // hour label
	accum += std::to_string(SystemTime.wHour); // Windows API call to grab hour value
	accum += "_Min_"; // minute label
	accum += std::to_string(SystemTime.wMinute); // Windows API call to grab minute value
	accum += "_Sec_"; // second label
	accum += std::to_string(SystemTime.wSecond); // Windows API call to grab second value

	return accum;
}

/****************************************************************************
* picoerrortoString
*
* Takes in a pico status
* Returns an empty string if the status is PICO_OK
* Returns an error statement (as a string) if otherwise, formatted as:
*	- [linenumber] callingscope::calledfunction ------ pico error name (pico error code)
*
* Parameters
* - status : the PICO_STATUS returned by whatever pico library function
*	(calledfunction) we called
* - linenumber : line number in source file at which the pico lbrary function
*	that returned the error was called
* - callingscope : the function in which the pico library function
*	(calledfunction) was called
* - calledfunction : the pico library function we called that returned the
*	PICO_STATUS status
*
* Returns
* - std::string : contains the line in the source file where the error occurred,
* the calling scope of the function that caused the error, the function in that
* scope that returned an error, a print of the error, and the error code
*	- If status is PICO_OK, an empty string is returned
****************************************************************************/
std::string picoerrortoString(PICO_STATUS status, int linenumber, std::string callingscope, std::string calledfunction)
{
	// allocating space for 256 chars is a bit arbitrary, but the callingscope functions can be arbitrarily(ish) 
	// long so we'll leave a reasonable amount of space to play with
	char temp[256];

	// write the desired print statement to a string
	sprintf_s(temp, (status) ? "[%d] %s::%s ------ %s (0x%08lx)\n" : "", linenumber, callingscope.c_str(), calledfunction.c_str(), PICO_STATUStoString(status).c_str(), status);

	// conversion between array of chars to explicit std::string
	std::string statement = temp;
	return statement;
}

/****************************************************************************
* picoerrorLog
*
* Logs errors returned by pico library functions to a specified file
*	- logs both the time the error occurs as well as the normal error info,
*	  generated by picoerrortoString()
*
* Parameters
* - errorlogfp : file pointer to the file where we're logging errors
* - status : the PICO_STATUS returned by whatever pico library function
*	(calledfunction) we called
* - linenumber : line number in source file at which the pico lbrary function
*	that returned the error was called
* - callingscope : the function in which the pico library function
*	(calledfunction) was called
* - calledfunction : the pico library function we called that returned the
*	PICO_STATUS status
*
* Returns
* - BOOL : to indicate whether or not the error was logged
		- TRUE indicates an error was logged
		- FALSE indicates nothing was logged
****************************************************************************/
BOOL picoerrorLog(FILE* errorlogfp, PICO_STATUS status, int linenumber, std::string callingscope, std::string calledfunction)
{
	if (status != PICO_OK && errorlogfp != NULL)
	{
		fprintf(errorlogfp, "%s\n%s\n\n", timeInfotoString().c_str(), picoerrortoString(status, linenumber, callingscope, calledfunction).c_str());
		return TRUE;
	}
	return FALSE;
}

/****************************************************************************
* timeUnitsToString
*
* Converts PS2000A_TIME_UNITS enumeration to string
*	- When using this with printf, a %s in the format string works
*
* Parameters
* - timeUnits : enumerated pico time unit data type
*
* Returns
* - int8_t* : pointer to a char stored as an int8_t (with the second letter
* following) representing the time unit as its metric prefix
****************************************************************************/
inline int8_t* timeUnitsToString(PS2000A_TIME_UNITS timeUnits)
{
	int8_t* timeUnitsStr = (int8_t*)"invalid time unit";

	switch (timeUnits)
	{
	case PS2000A_FS: // femtoseconds

		timeUnitsStr = (int8_t*)"fs";
		break;

	case PS2000A_PS: // picoseconds

		timeUnitsStr = (int8_t*)"ps";
		break;

	case PS2000A_NS: // nanoseconds

		timeUnitsStr = (int8_t*)"ns";
		break;

	case PS2000A_US: // microseconds

		timeUnitsStr = (int8_t*)"us"; //not sure how to get windows to print out "mu" character, very frustratiung
		break;

	case PS2000A_MS: // milliseconds

		timeUnitsStr = (int8_t*)"ms";
		break;

	case PS2000A_S: // seconds

		timeUnitsStr = (int8_t*)"s";
		break;

	default: // invalid input

		timeUnitsStr = (int8_t*)"invalid time unit";
	}

	return timeUnitsStr;
}

/****************************************************************************
* timeUnitsToValue
*
* Completes a unit conversion from 1 second to the supplied time unit in
* form of PS2000A_TIME_UNITS enumeration
*	- i.e. an argument of PS2000A_NS returns 10^9
*
* Parameters
* - timeUnits : enumerated pico time unit data type to be converted
*
* Returns
* - uint64_t : contains 1 second converted to the time unit passed in as an
* argument
****************************************************************************/
inline uint64_t timeUnitsToValue(PS2000A_TIME_UNITS timeUnits)
{
	uint64_t timeUnitsVal;

	switch (timeUnits)
	{
	case PS2000A_FS: // femtoseconds

		timeUnitsVal = (uint64_t)std::pow(10, 15);
		break;

	case PS2000A_PS: // picoseconds

		timeUnitsVal = (uint64_t)std::pow(10, 12);
		break;

	case PS2000A_NS: // nanoseconds

		timeUnitsVal = (uint64_t)std::pow(10, 9);
		break;

	case PS2000A_US: // microseconds

		timeUnitsVal = (uint64_t)std::pow(10, 6);
		break;

	case PS2000A_MS: // milliseconds

		timeUnitsVal = (uint64_t)std::pow(10, 3);
		break;

	case PS2000A_S: // seconds

		timeUnitsVal = (uint64_t)std::pow(10, 0);
		break;

	default:

		timeUnitsVal = 0; //invalid input
	}

	return timeUnitsVal;
}

/****************************************************************************
* ClearDataBuffers
*
* stops GetData writing values to memory that has been released
*
* Parameters
* - unit : pointer to the UNIT structure, where the handle is stored
*
* Returns
* - PICO_STATUS : to indicate success, or if an error occurred
****************************************************************************/
PICO_STATUS ClearDataBuffers(UNIT* unit)
{
	PICO_STATUS status = PICO_OK;

	for (int16_t i = 0; i < unit->channelCount; i++)
	{
		// for each channel, set a NULL pointer to the location of the buffer, set buffer size to 0, and segment index to 0
		if ((status = ps2000aSetDataBuffer(unit->handle, (PS2000A_CHANNEL)(i), NULL, 0, 0, PS2000A_RATIO_MODE_NONE)) != PICO_OK)
		{
			printf("%s", picoerrortoString(status, __LINE__, __func__, "ps2000aSetDataBuffer").c_str());
			picoerrorLog(g_errorfp, status, __LINE__, __func__, "ps2000aSetDataBuffer");
		}
	}

	return status;
}

/****************************************************************************
* SetTrigger
*
* Parameters
* - *unit               - pointer to the UNIT structure
* - *channelProperties  - pointer to the PS2000A_TRIGGER_CHANNEL_PROPERTIES
*						  structure
* - nChannelProperties  - the number of PS2000A_TRIGGER_CHANNEL_PROPERTIES
*						  elements in channelProperties
* - *triggerConditions  - pointer to the PS2000A_TRIGGER_CONDITIONS structure
* - nTriggerConditions  - the number of PS2000A_TRIGGER_CONDITIONS elements
*						  in triggerConditions
* - *directions         - pointer to the TRIGGER_DIRECTIONS structure
* - *pwq                - pointer to the pwq (Pulse Width Qualifier) structure
* - delay               - Delay time between trigger & first sample
* - auxOutputEnable     - Not used
* - autoTriggerMs       - timeout period if no trigger occurrs
* - *digitalDirections  - pointer to the PS2000A_DIGITAL_CHANNEL_DIRECTIONS
*						  structure
* - nDigitalDirections  - the number of PS2000A_DIGITAL_CHANNEL_DIRECTIONS
*						  elements in digitalDirections
*
* Returns
* - PICO_STATUS			- to show success or if an error occurred
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

	// take in trigger properties in structs as the arguments...
	// set the trigger up with these properties using the various pico
	// library functions

	if ((status = ps2000aSetTriggerChannelProperties(unit->handle,
		channelProperties,
		nChannelProperties,
		auxOutputEnabled,
		autoTriggerMs)) != PICO_OK)
	{
		printf("%s", picoerrortoString(status, __LINE__, __func__, "ps2000aSetTriggerChannelProperties").c_str());
		picoerrorLog(g_errorfp, status, __LINE__, __func__, "ps2000aSetTriggerChannelProperties");
		return status;
	}

	if ((status = ps2000aSetTriggerChannelConditions(unit->handle, triggerConditions, nTriggerConditions)) != PICO_OK)
	{
		printf("%s", picoerrortoString(status, __LINE__, __func__, "ps2000aSetTriggerChannelConditions").c_str());
		picoerrorLog(g_errorfp, status, __LINE__, __func__, "ps2000aSetTriggerChannelConditions");
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
		printf("%s", picoerrortoString(status, __LINE__, __func__, "ps2000aSetTriggerChannelDirections").c_str());
		picoerrorLog(g_errorfp, status, __LINE__, __func__, "ps2000aSetTriggerChannelDirections");
		return status;
	}

	if ((status = ps2000aSetTriggerDelay(unit->handle, delay)) != PICO_OK)
	{
		printf("%s", picoerrortoString(status, __LINE__, __func__, "ps2000aSetTriggerDelay").c_str());
		picoerrorLog(g_errorfp, status, __LINE__, __func__, "ps2000aSetTriggerDelay");
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
		printf("%s", picoerrortoString(status, __LINE__, __func__, "ps2000aSetPulseWidthQualifier").c_str());
		picoerrorLog(g_errorfp, status, __LINE__, __func__, "ps2000aSetPulseWidthQualifier");
		return status;
	}

	return status;
}

/****************************************************************************
* SetDefaults - restore default settings
*
* Parameters
* - unit : pointer to the UNIT structure, where the handle is stored
*
* Returns
* - PICO_STATUS : to indicate success, or if an error occurred
****************************************************************************/
PICO_STATUS SetDefaults(UNIT* unit)
{
	PICO_STATUS status;
	float analogOffset = 0.0; // no need, data is centered about 0

	// Turn off ETS
	if (status = ps2000aSetEts(unit->handle, PS2000A_ETS_OFF, 0, 0, NULL) != PICO_OK)
	{
		printf("%s", picoerrortoString(status, __LINE__, __func__, "ps2000aSetEts").c_str());
		picoerrorLog(g_errorfp, status, __LINE__, __func__, "ps2000aSetEts");
		return status;
	}

	for (int16_t i = 0; i < unit->channelCount; i++) // reset channels to most recent settings
	{
		status = ps2000aSetChannel(unit->handle, (PS2000A_CHANNEL)(i),
			unit->channelSettings[i].enabled,
			(PS2000A_COUPLING)unit->channelSettings[i].DCcoupled,
			(PS2000A_RANGE)unit->channelSettings[i].range, analogOffset);
		if (status != PICO_OK)
		{
			printf("%s", picoerrortoString(status, __LINE__, __func__, "ps2000aSetChannel").c_str());
			picoerrorLog(g_errorfp, status, __LINE__, __func__, "ps2000aSetChannel");
			return status;
		}
	}
	return status;
}

/****************************************************************************
* Callback
*
* used by ps2000a data block collection calls, on receipt of data.
*	- used by ps2000aRunBlock in this case
* used to set global flags etc checked by user routines
*
* Parameters
* - handle : handle used to refer to the pico device being used (not  needed
*			 here for our application)
* - status : PICO_STATUS indicating whether or not there's an issue with the
*	device
* - pParameter : pointer passed from ps2000aRunBlock so that this function
*	can pass back arbitrary data to the calling space
*		- not used in this case, NULL pointer passed
*
* Returns
* - none
****************************************************************************/
void __stdcall CallBackBlock(int16_t handle, PICO_STATUS status, void* pParameter)
{
	if (status != PICO_CANCELLED)
	{
		g_ready = TRUE;
	}
}

/****************************************************************************
* MovingAverage :
*
* - Used for data processing with the peak finding routines
* - returns the average of the 5 inputted int16_t's
*
* Parameters
* - a,b,c,d,e : int16_t values to be averaged
*
* Returns
* - int16_t : contains the average of the 5 values passed as arguments
****************************************************************************/
inline int16_t MovingAverage(int16_t a, int16_t b, int16_t c, int16_t d, int16_t e)
{
	return (int16_t)((float_t)((int32_t)a + (int32_t)b + (int32_t)c + (int32_t)d + (int32_t)e) / 5.0);
}
/*
// replace with? (more general)
int16_t MovingAverage(int32_t num, int16_t* array)
{
	int16_t accum = 0;
	for(uint32_t i = 0; i < num; i++)
	{
		accum += array[i];
	}
	return (int16_t)((float_t)(accum) / 5.0);
}
*/

/****************************************************************************
* ArrayAvg :
*
* - Used for data processing with the peak finding routines
* - returns the average of all the points in the buffer of int16_t's as a
* float_t
*
* Parameters
* - buffer : pointer to the start of the the buffer/array of int16_t's to
* be averaged
* - sampleCount : the number of entries in the buffer
*
* Returns
* - float_t : contains the average of the first sampleCount entries in the
* buffer
****************************************************************************/
float_t ArrayAvg(int16_t buffer[], uint32_t sampleCount)
{
	int32_t accumulator = 0;

	for (uint32_t i = 0; i < sampleCount; i++)
	{
		accumulator += (int32_t)buffer[i];
	}

	return (float_t)accumulator / (float_t)sampleCount;
}

/****************************************************************************
* BlockPeakFinding

* - smooths the input signal using a moving average technique, then finds and
* returns the indices of the found peaks in the buffer array
* - moving average ignored for first and last few points
* - ONLY WORKS FOR DOWNWARD PEAKS (concave up)
* - Would have to write a similar (but ultimately separate) routine for
* upward (concave down) peak finding
*	-unless you just flipped the data but whatever same difference
* - Based off of a combination of the smoothing algorithm and multiple peak
* finding algorithm from https://www.baeldung.com/cs/signal-peak-detection

* Parameters
* - buffer : the buffer array where the data is stored
* - sampleCount : the number of samples in the buffer
* - unit : pointer to the UNIT structure, where the handle is stored
*
* Returns
* - uint32_t* : pointer to buffer with 10 uint32_t's. The first entry in the
* buffer contains the number of peaks found by the algorithm (1,2,3,...)
* The remaining entries either contain the index of the detected peak
* (peak 1's index is stored in buffer[1], the second peak in buffer[2], etc.)
* or are left blank
****************************************************************************/
uint32_t* BlockPeakFinding(int16_t buffer[], uint32_t sampleCount, UNIT* unit)
{
	uint32_t* indices = NULL;
	indices = (uint32_t*)malloc(10 * sizeof(uint32_t)); //first entry gets numpeaks, subsequent ones get index of the peak from the waveform
	uint16_t numpeaks = 0;
	int16_t* smoothbuffer = NULL;
	smoothbuffer = (int16_t*)malloc(sampleCount * sizeof(int16_t));
	/*
	* TRYING TO ALLOCATE SMOOTHBUFFER WITH FLOATS
	* CAUSES THE OS TO NOT GIVE YOU ALL THE MEMORY (I think)
	* YOU ASKED FOR, BUT IT DOESN'T TELL YOU THAT HAPPENED
	* SO WHEN YOU TRY TO FREE IT THE PROGRAM BREAKS BECAUSE
	* YOU'RE TRYING TO FREE MEMORY YOU NEVER HAD ACCESS TO
	*/
	// this comment brought to you by a Frustrating Afternoon™
	// various ways of trying to allocate float array (ignore)
	// float_t* smoothbuffer; 
	// smoothbuffer = (float_t*)malloc(sampleCount * sizeof(float_t));
	// float_t* smoothbuffer = new float_t(sampleCount * sizeof(float_t));
	// std::vector<float_t> smoothbuffer(sampleCount);
	int16_t peakValue = std::numeric_limits<int16_t>::infinity();
	int32_t peakIndex = -1;
	float_t baseline;
	// g_peakthresh needed to filter out some of the noise, a bit of a duct tape solution but it works
	int16_t thresh = mv_to_adc(g_peakthresh, unit->channelSettings[PS2000A_CHANNEL_A].range, unit);

	// just copy the ends in since we can't average on both sides of them
	smoothbuffer[0] = buffer[0];
	smoothbuffer[1] = buffer[1];
	smoothbuffer[sampleCount] = buffer[sampleCount];
	smoothbuffer[sampleCount - 1] = buffer[sampleCount - 1];

	printf("Searching for peaks within the most recent sample...");

	// now average out the rest of the points
	for (uint32_t i = 2; i < sampleCount - 1; i++)
	{
		smoothbuffer[i] = MovingAverage(buffer[i - 2], buffer[i - 1], buffer[i], buffer[i + 1], buffer[i + 2]);
	}

	baseline = ArrayAvg(buffer, sampleCount); // do we want this average or the average of the smoothed signal-> Probably not a huge difference

	for (uint32_t i = 2; i < sampleCount - 1; i++)
	{
		if (smoothbuffer[i] < baseline)
		{
			if (smoothbuffer[i] < peakValue && smoothbuffer[i] < thresh)
			{
				peakIndex = i;
				peakValue = smoothbuffer[i];
			}
		}
		else if ((smoothbuffer[i] > baseline && peakIndex != -1) && indices != NULL)
		{
			indices[0] = ++numpeaks; // store number of found peaks in the array's first entry
			indices[numpeaks] = peakIndex; // all the other peak index values follow in the array
			peakIndex = -1; // reset this so we can find the next one (if there is one)
			peakValue = std::numeric_limits<int16_t>::infinity(); // reset this too
		}
		if (numpeaks > 8) // in practice we shouldn't need to find more than 2 peaks
		{
			printf("\nMaximum number of peaks (9) detected! Stopping the search now.\n");
			printf("If this search is classifying \"noise\" as peaks, consider either raising the peak detection threshold.\n");
			free(smoothbuffer); // no memory leaks
			return indices;
		}
	}

	if (peakIndex != -1 && indices != NULL)
	{
		indices[0] = ++numpeaks;
		indices[numpeaks] = peakIndex;
	}

	printf((numpeaks == 1) ? "%d peak detected.\n" : "%d peaks detected.\n", numpeaks);
	free(smoothbuffer); // no memory leaks
	return indices;
}

/****************************************************************************
* BlockPeaktoPeak

* - Calls BlockPeakFinding to find peaks in a block of data, then finds
* the peak to peak times for the returned peaks
* - Assumes a downwards peak characteristic of the scintillating bar

* Parameters
* - buffer : the buffer array where the data is stored
* - sampleCount : the number of samples in the buffer
* - sampleInterval : the amount of time (in ns) per sample
*
* Returns
* - uint32_t* : pointer to a buffer of 10 uint32_t's. See comments for
* BlockPeakFinding
****************************************************************************/
uint32_t* BlockPeaktoPeak(UNIT* unit, int16_t buffer[], uint32_t sampleCount, uint32_t sampleInterval, uint32_t downsampleratio)
{
	uint32_t peaktopeak = -1;
	uint32_t* indices = NULL;
	uint16_t numpeaks = 0;

	indices = BlockPeakFinding(buffer, sampleCount, unit); // get the results from the peak finding algorithm
	numpeaks = indices[0]; // first entry is numpeaks

	printf(numpeaks > 1 ? "Calculating peak to peak values...\n" : "");

	// Can leave prints in for easy checking of data taking process, shouldn't slow down program too much
	printf("Peak #, Time (ns), ADC Count, Voltage (mV)\n");
	for (uint16_t i = 1; i < numpeaks + 1; i++)
	{
		// prints with a different number of spaces depending on the ADC value so
		// that the mV value properly aligns with its column label
		// casting down sampleInterval and downsampleratio shouldn't be a huge deal, 
		// should be a relatively small number
		printf((std::abs(buffer[indices[i]] * (int16_t)sampleInterval * (int16_t)downsampleratio) >= 10000) ?
			"%d       %u        %d     %d\n" : "%d       %u        %d      %d\n",
			i, indices[i] * sampleInterval, buffer[indices[i]], adc_to_mv(buffer[indices[i]], unit->channelSettings[PS2000A_CHANNEL_A].range, unit));
	}

	for (uint16_t i = 1; i < numpeaks + 1; i++)
	{
		for (uint16_t j = i + 1; j < numpeaks + 1; j++)
		{
			printf("Peak %d to Peak %d: %d ns\n", i, j, (indices[j] - indices[i]) * sampleInterval * downsampleratio);
		}
	}

	return indices;
}

/****************************************************************************
* BlockDataHandler

* - Used by all block data routines
* - acquires data (user sets trigger mode before calling),
*   and saves all to a .csv file (if specified by numwavestosaved)

* Parameters
* - unit : pointer to the UNIT structure, where the handle is stored
* - feakfp : pointer to the file where peak info (not waveforms) are written to
* - offset : the offset into the data buffer to start the display's slice. (normally 0)
* - mode : ANALOGUE, DIGITAL, AGGREGATED, MIXED - just used ANALOGUE here
* - etsModeSet: whether or not ETS Mode (see programmer's guide) is
	turned on (turned off for our application)
*
* Returns
* - PICO_STATUS : to indicate success, or if an error occurred
****************************************************************************/
PICO_STATUS BlockDataHandler(UNIT* unit, int32_t offset, MODE mode, int16_t etsModeSet)
{
	PICO_STATUS status = PICO_OK;
	uint32_t segmentIndex = 0;
	int32_t timeIntervalNanoseconds;
	int32_t posttriggersampleCount = 0;
	int32_t sampleCount = 0;
	int32_t pretriggersampleCount = 0;
	int32_t maxSamples = 0;
	uint32_t downsampleratio = 1;
	uint16_t numpeaks = 0;
	uint32_t* indices = NULL; // array to hold numpeaks and the indices of such peaks
	BOOL lasttosave = FALSE; // indicates if the waveform just saved was the last one to be saved
	FILE* wavefp = NULL;
	int16_t qinit = -1; // stores 'Q' key toggle state for quitting
	PS2000A_RATIO_MODE ratioMode = PS2000A_RATIO_MODE_NONE; // Don't want any downsampling
	int16_t* buffer = NULL; // buffer where the device will dump its block of data to
	tBufferInfo BufferInfo = {
		unit,
		mode,
		buffer
	};


	if ((status = ps2000aMemorySegments(unit->handle, (uint32_t)1, &maxSamples)) != PICO_OK)
	{
		printf("Error occured while partitioning the device's internal memory.\n");
		printf("%s", picoerrortoString(status, __LINE__, __func__, "ps2000aMemorySegments").c_str());
		picoerrorLog(g_errorfp, status, __LINE__, __func__, "ps2000aMemorySegments");
		return status;
	}

	// Max sample count for the device is 33,554,342
	pretriggersampleCount = 100;
	posttriggersampleCount = 50000;
	sampleCount = pretriggersampleCount + posttriggersampleCount;
	if (sampleCount > maxSamples)
	{
		sampleCount = maxSamples;
		pretriggersampleCount = 1000;
		posttriggersampleCount = maxSamples - 1000;
		printf("Requested sampleCount exceeds the device's internal memory.\n");
		printf("Setting posttriggersampleCount to %d - 1000, and pretriggersampleCount to 1000.\n", maxSamples);
	}

	if (mode == ANALOGUE)
	{
		BufferInfo.driverBuffer = (int16_t*)malloc(sampleCount * sizeof(int16_t));
		if ((status = ps2000aSetDataBuffer(unit->handle, PS2000A_CHANNEL_A, BufferInfo.driverBuffer, sampleCount, segmentIndex, ratioMode)) != PICO_OK)
		{
			printf("%s", picoerrortoString(status, __LINE__, __func__, "ps2000aSetDataBuffer").c_str());
			picoerrorLog(g_errorfp, status, __LINE__, __func__, "ps2000aSetDataBuffer");
			return status;
		}
	}

	// Validate the current timebase index, and find the maximum number of samples and the time interval (in nanoseconds)
	while ((status = ps2000aGetTimebase(unit->handle, g_timebase, sampleCount, &timeIntervalNanoseconds, g_oversample, &maxSamples, 0)) != PICO_OK)
	{
		g_timebase++; // this should never get incremented and just stay at 0 (so long as we're using single channel block mode)
	}

	// Start it collecting, then wait for completion
	g_ready = FALSE;
	if ((status = ps2000aRunBlock(unit->handle, pretriggersampleCount, posttriggersampleCount, g_timebase, g_oversample, NULL, 0, CallBackBlock, NULL)) != PICO_OK)
	{
		printf("%s", picoerrortoString(status, __LINE__, __func__, "ps2000aRunBlock").c_str());
		picoerrorLog(g_errorfp, status, __LINE__, __func__, "ps2000aRunBlock");
		return status;
	}

	printf("Waiting for trigger...Press \'Q\' to abort...");

	qinit = _kbhitinit();

	while (!g_ready && !_kbhitpoll(qinit))
	{
		Sleep(0);
	}

	if (g_ready)
	{
		printf("Triggered!\n");
		if ((status = ps2000aGetValues(unit->handle, 0, (uint32_t*)&sampleCount, downsampleratio, ratioMode, 0, NULL)) != PICO_OK)
		{
			printf("%s", picoerrortoString(status, __LINE__, __func__, "ps2000aGetValues").c_str());
			picoerrorLog(g_errorfp, status, __LINE__, __func__, "ps2000aGetValues");
			return status;
		}

		indices = BlockPeaktoPeak(unit, BufferInfo.driverBuffer, sampleCount, timeIntervalNanoseconds, downsampleratio);
		numpeaks = indices[0]; // numpeaks stored in the first array entry

		// might want to make this == 2 since 3-peak events seem to throw a wrench in the data analysis
		// if this change is made we can get rid of the 'T' delimiter for the peak info file
		if (numpeaks > 1) // no reason to record 1-peak events
		{
			g_nummultipeakevents++; // keep track of how many events we've recorded
			if (mode == ANALOGUE)	// if we're using analogue mode
			{
				if (((g_numwavestosaved > 0) || (g_numwavestosaved == -1))) // if we're still saving waveforms
				{
					// give the file a time-dependent name to avoid file name collisions
					wavefilename = "RAW_WAVEFORM_"; // reset the filename from the last block of multi-peak data
					wavefilename += timeInfotoString();
					wavefilename += ".csv";
					fopen_s(&wavefp, wavefilename.c_str(), "w");

					if (wavefp != NULL)
					{
						printf("Writing the raw data to the disk file(%s)\n...", wavefilename.c_str());
						fprintf(wavefp, "Block Data Log:\n\n");
						fprintf(wavefp, "Time (ns), ADC Count, mV\n");
						for (int32_t i = 0; i < sampleCount; i++)
						{
							// Times printed in ns
							fprintf(wavefp,
								"%d, %d, %d\n",
								(int32_t)(i * timeIntervalNanoseconds * downsampleratio),
								BufferInfo.driverBuffer[i],
								adc_to_mv(BufferInfo.driverBuffer[i], unit->channelSettings[PS2000A_CHANNEL_A].range, unit));
						}
						printf("done.\n");
						// update the number of waveforms to be saved
						if (g_numwavestosaved > 0)
						{
							g_numwavestosaved--;
							lasttosave = g_numwavestosaved ? FALSE : TRUE; // If we just saved the last waveform, set this flag to TRUE
						}
					}
					else
					{
						printf("Cannot open the file \n%s\n for writing.\n"
							"Please ensure that you have permission to access and/ or the file isn't currently open.\n", wavefilename.c_str());
					}
				}
				else
				{
					printf("The maximum number of waveforms to be recorded has been reached.\n");
					printf("Peak information for this waveform will still be saved. (%s)\n", peakfilename.c_str());
				}
				if (g_peakfp != NULL)
				{
					// print peak depths both as adc counts and in mV
					for (uint16_t i = 1; i <= numpeaks; i++)
					{
						fprintf(g_peakfp, "%d,%d,",
							BufferInfo.driverBuffer[indices[i]],
							adc_to_mv(BufferInfo.driverBuffer[indices[i]], unit->channelSettings[PS2000A_CHANNEL_A].range, unit));
					}
					fprintf(g_peakfp, "T,"); // some arbitrary deliminating character to separate peak depths and time differences
					// print time differences (in ns) between the first peak and other peaks
					for (uint16_t i = 2; i <= numpeaks; i++)
					{
						fprintf(g_peakfp, "%d,", (indices[i] - indices[1]) * timeIntervalNanoseconds * downsampleratio);
					}
					// if the program wrote a waveform file, its name gets written to the peak file
					fprintf(g_peakfp, (g_numwavestosaved || lasttosave) ? "%s\n" : "No file\n", wavefilename.c_str());
				}
				else
				{
					printf("Cannot open the file \n%s\n for writing.\n"
						"Please ensure that you have permission to access and/ or the file isn't currently open.\n", peakfilename.c_str());
				}
			}
		}
		else
		{
			printf("Not recording 1-peak events.\n");
			printf((g_numwavestosaved == -1) ? "" : "Remaining Number of Waveforms to Record: %d\n", g_numwavestosaved);
		}
	}
	else
	{
		printf("Data collection aborted!\nPress a key to finish terminating the program.\n");
		_getch();
	}

	if ((status = ps2000aStop(unit->handle)) != PICO_OK)
	{
		printf("%s", picoerrortoString(status, __LINE__, __func__, "ps2000aStop").c_str());
		picoerrorLog(g_errorfp, status, __LINE__, __func__, "ps2000aStop");
	}

	if (wavefp != NULL)
	{
		fclose(wavefp); // If we wrote waveform data to a file, close it
	}

	// Free up the buffer if we allocated it
	if (mode == ANALOGUE)
	{
		if (unit->channelSettings[PS2000A_CHANNEL_A].enabled && BufferInfo.driverBuffer != NULL)
		{
			free(BufferInfo.driverBuffer);
		}
	}

	if (indices != NULL)
	{
		free(indices); // no memory leaks in this here code
	}

	printf("Total Number of Multi-Peak Events Recorded: %I64d\n", g_nummultipeakevents);

	// clear the device's internal buffers before the next run just to be thorough, not really necessary
	if ((status = ClearDataBuffers(unit)) != PICO_OK)
	{
		printf("%s", picoerrortoString(status, __LINE__, __func__, "ClearDataBuffers").c_str());
		picoerrorLog(g_errorfp, status, __LINE__, __func__, "ClearDataBuffers");
	}
	return status;
}

/****************************************************************************
* CollectBlockTriggered

*  This function collects a single block of data from the
*  unit, when a trigger event occurs.

* Parameters
* - unit : pointer to the UNIT structure, where the handle is stored
* - feakfp : pointer to the file where peak info (not waveforms) is written
* to
*
* Returns
* - PICO_STATUS : to indicate success, or if an error occurred
****************************************************************************/
PICO_STATUS CollectBlockTriggered(UNIT* unit)
{
	PICO_STATUS status = PICO_OK;

	if (g_firstRun == TRUE) // only need to set the device's settings, triggers once
	{
		g_trigthresh = inputRanges[unit->channelSettings[PS2000A_CHANNEL_A].range] + 1; //set the initial value to something unacceptable so we enter the while loop
		std::cin.clear(); // flush the input buffer
		while (!(g_trigthresh > -inputRanges[unit->channelSettings[PS2000A_CHANNEL_A].range] // make sure input falls in an acceptable range
			&& g_trigthresh < inputRanges[unit->channelSettings[PS2000A_CHANNEL_A].range]) // ^
			|| g_cinflag) // and there were no errors while taking in input
		{
			printf("\n\nPlease enter the scope's trigger threshold (%d mV to %d mV):\nA value of -400mV is recommended.\n",
				-inputRanges[unit->channelSettings[PS2000A_CHANNEL_A].range],
				inputRanges[unit->channelSettings[PS2000A_CHANNEL_A].range]);
			printf("Trigger Threshold (mV): ");

			std::cin >> g_trigthresh;
			g_cinflag = (std::cin.bad() || std::cin.fail()) ? TRUE : FALSE; // check if cin's error flags were set
			cinReset(); // flush the input buffer for future inputs
		}

		printf("Selected Trigger Threshold: %d mV \n", g_trigthresh);

		// convert desired trigger threshold from mV to ADC count
		int16_t	triggerVoltage = mv_to_adc(g_trigthresh, unit->channelSettings[PS2000A_CHANNEL_A].range, unit);

		// some of the author's/ SDK's defined structs used to set up the trigger
		// Only triggering once so hyteresis doesn't matter, but may be better to set to 0 for clarity
		PS2000A_TRIGGER_CHANNEL_PROPERTIES sourceDetails = {
			triggerVoltage,
			0,
			triggerVoltage,
			0,
			PS2000A_CHANNEL_A,
			PS2000A_LEVEL };

		PS2000A_TRIGGER_CONDITIONS conditions = {
			PS2000A_CONDITION_TRUE,				// Channel A
			PS2000A_CONDITION_DONT_CARE,		// Channel B 
			PS2000A_CONDITION_DONT_CARE,		// Channel C
			PS2000A_CONDITION_DONT_CARE,		// Channel D
			PS2000A_CONDITION_DONT_CARE,		// external
			PS2000A_CONDITION_DONT_CARE,		// aux
			PS2000A_CONDITION_DONT_CARE,		// PWQ
			PS2000A_CONDITION_DONT_CARE };		// digital

		// do we want PS2000A_FALLING or PS2000A_FALLING_LOWER?-> PS2000A_FALLING seems to be working
		TRIGGER_DIRECTIONS directions = {
			PS2000A_FALLING,		// Channel A
			PS2000A_NONE,			// Channel B
			PS2000A_NONE,			// Channel C
			PS2000A_NONE,			// Channel D
			PS2000A_NONE,			// ext
			PS2000A_NONE };			// aux

		PWQ pulseWidth;
		memset(&pulseWidth, 0, sizeof(PWQ)); // set all the memory allocated for the struct to 0

		printf("\nCollect block triggered\n");
		printf("Collects when value falls past %d", g_scaleVoltages ?
			adc_to_mv(sourceDetails.thresholdUpper, unit->channelSettings[PS2000A_CHANNEL_A].range, unit)	// If scaleVoltages, print mV value
			: sourceDetails.thresholdLower); 															// else print ADC Count
		printf(g_scaleVoltages ? "mV\n" : "ADC Counts\n");

		printf("\n\nPress \'Q\' once to stop data collection at any point.\n\n");
		printf("Errors returned by calls to Pico Technology's library functions will be displayed in the following format:\n");
		printf("[Line Number in Source File] CallingScope::FunctionThatReturnedError ------ Error (Error Code)\n");
		printf("Press a key to start...\n");
		_getch();
		g_firstRun = FALSE;

		if ((status = SetDefaults(unit)) != PICO_OK)
		{
			printf("%s", picoerrortoString(status, __LINE__, __func__, "SetDefaults").c_str());
			picoerrorLog(g_errorfp, status, __LINE__, __func__, "SetDefaults");
			return status;
		}

		// Trigger setup/ enabled per settings detailed in the above structs
		if ((status = SetTrigger(unit, &sourceDetails, 1, &conditions, 1, &directions, &pulseWidth, 0, 0, 0, 0, 0)) != PICO_OK)
		{
			printf("%s", picoerrortoString(status, __LINE__, __func__, "SetTrigger").c_str());
			picoerrorLog(g_errorfp, status, __LINE__, __func__, "SetTrigger");
			return status;
		}
	}

	// set up the scope for data collection and collect it
	if ((status = BlockDataHandler(unit, 0, ANALOGUE, FALSE)) != PICO_OK)
	{
		printf("%s", picoerrortoString(status, __LINE__, __func__, "BlockDataHandler").c_str());
		picoerrorLog(g_errorfp, status, __LINE__, __func__, "BlockDataHandler");
		return status;
	}

	return status;
}

/****************************************************************************
* get_info
*
* Initialise unit' structure with variant specific defaults

* Parameters
* - unit : pointer to the UNIT structure, where the handle is stored
*
* Returns
* - PICO_STATUS : to indicate success, or if an error occurred
****************************************************************************/
PICO_STATUS get_info(UNIT* unit)
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

	int16_t requiredSize;
	int8_t line[80];
	PICO_STATUS status = PICO_OK;
	int16_t numChannels = DUAL_SCOPE;
	int8_t channelNum = 0;

	unit->signalGenerator = TRUE;
	unit->ETS = FALSE;
	unit->firstRange = PS2000A_20MV; // This is for new PicoScope 220X B, B MSO, 2405A and 2205A MSO models, older devices will have a first range of 50 mV
	unit->lastRange = PS2000A_20V;
	unit->channelCount = DUAL_SCOPE;
	unit->digitalPorts = 0;
	unit->awgBufferSize = PS2000A_MAX_SIG_GEN_BUFFER_SIZE;

	if (unit->handle)
	{
		for (int16_t i = 0; i < 11; i++)
		{
			if ((status = ps2000aGetUnitInfo(unit->handle, (int8_t*)line, sizeof(line), &requiredSize, i)) != PICO_OK)
			{
				printf("%s", picoerrortoString(status, __LINE__, __func__, "ps2000aGetUnitInfo").c_str());
				picoerrorLog(g_errorfp, status, __LINE__, __func__, "ps2000aGetUnitInfo");
				return status;
			}

			if (i == PICO_VARIANT_INFO)
			{
				// Check if device has four channels
				/*
				* have to do a little casting here because Visual Studio
				* doesn't seem to want to compile it the the way
				* the original author wrote it some reason
				*/

				channelNum = line[1];
				numChannels = atoi((const char*)&channelNum);

				if (numChannels == QUAD_SCOPE)
				{
					unit->channelCount = QUAD_SCOPE;
				}

				// Set first range for voltage if device is a 2206/7/8, 2206/7/8A or 2205 MSO
				if (numChannels == DUAL_SCOPE)
				{
					// Visual Studio doesn't like strcmpi function, using _strcmpi instead
					if (strlen((const char*)line) == 4 || (strlen((const char*)line) == 5 && _strcmpi((const char*)&line[4], "A") == 0) || (_strcmpi((const char*)line, "2205MSO")) == 0)
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

	return status;
}

/****************************************************************************
* OpenDevice

* Parameters
* - unit : pointer to the UNIT structure, where the handle will be stored
*
* Returns
* - PICO_STATUS : to indicate success, or if an error occurred
***************************************************************************/
PICO_STATUS OpenDevice(UNIT* unit)
{
	int16_t maxvalue = 0;
	PWQ pulseWidth;
	TRIGGER_DIRECTIONS directions;
	int16_t qinit = -1;
	int16_t rangeselect; // range selected in mV
	PS2000A_RANGE scoperange = PS2000A_2V; // scope range, typically want PS2000A_2V for this application
	PICO_STATUS status = PICO_OK;

	printf("Opening device...");

	if ((status = ps2000aOpenUnit(&(unit->handle), NULL)) != PICO_OK)
	{
		printf("Error opening the device! Ensure it is plugged in.\n");
		printf("If this is your first time attempting to run the program, try restarting your computer.\n");
		printf("%s", picoerrortoString(status, __LINE__, __func__, "ps2000aOpenUnit").c_str());
		picoerrorLog(g_errorfp, status, __LINE__, __func__, "ps2000aOpenUnit");
		return status;
	}

	printf("done.\n");
	printf("Handle: %d\n", unit->handle);

	printf("Device opened successfully\n\n");

	// gather device-specific information
	get_info(unit);

	// flush the data buffers from the start just to be sure
	printf("Flushing the data buffers...");
	if ((status = ClearDataBuffers(unit)) != PICO_OK)
	{
		printf("%s", picoerrortoString(status, __LINE__, __func__, "ClearDataBuffers").c_str());
		picoerrorLog(g_errorfp, status, __LINE__, __func__, "ClearDataBuffers");
	}
	printf("done.\n");

	// max value needed for conversion between adc and mv
	if ((status = ps2000aMaximumValue(unit->handle, &maxvalue)) != PICO_OK)
	{
		printf("%s", picoerrortoString(status, __LINE__, __func__, "ps2000aMaximumValue").c_str());
		picoerrorLog(g_errorfp, status, __LINE__, __func__, "ps2000aMaximumValue");
		return status;
	}
	unit->maxValue = maxvalue;

	rangeselect = unit->firstRange - 1; // make initial value unacceptable so we enter the while loop
	std::cin.clear(); // flush the input buffer
	while (!(rangeselect > unit->firstRange && rangeselect < unit->lastRange) // make sure input falls in an acceptable range
		|| g_cinflag) // and there were no errors while taking in input
	{
		printf("\n\nPlease select the scope's operational voltage range:\n");
		printf("The recommended value is 2000mV.\n");

		for (uint16_t i = unit->firstRange, j = 0; i <= (unit->lastRange - unit->firstRange); i++, j++)
		{
			printf("[%d] %d mV\n", j, inputRanges[i]);
		}

		printf("Range: ");

		std::cin >> rangeselect; // take in index
		// offset needed because the first range supported by our device 
		// isn't the first range in input ranges data type, which lists all
		// supported ranges by all picoscope devices 
		rangeselect += unit->firstRange;
		scoperange = (PS2000A_RANGE)inputRanges[rangeselect];
		g_cinflag = (std::cin.bad() || std::cin.fail()) ? TRUE : FALSE; // check if cin's error flags were set
		cinReset(); // flush the input buffer for future inputs
	}

	printf("Selected Range: %d mV\n", scoperange);

	for (int16_t i = 0; i < unit->channelCount; i++)
	{
		if (i == PS2000A_CHANNEL_A) // only enable Channel A
		{
			unit->channelSettings[i].enabled = TRUE;
			unit->channelSettings[i].DCcoupled = TRUE;
			//unit->channelSettings[i].range = PS2000A_2V; // hard coded default
			unit->channelSettings[i].range = rangeselect; // index, not actual mV value
		}
		else
		{
			unit->channelSettings[i].enabled = FALSE;
			unit->channelSettings[i].DCcoupled = FALSE;
			unit->channelSettings[i].range = rangeselect;
		}
	}

	if (g_firstRun == TRUE)
	{
		// make initial value unacceptable so we enter the while loop (value is 1 below the scope's supported range)
		g_peakthresh = -inputRanges[unit->channelSettings[PS2000A_CHANNEL_A].range] - 1;
		std::cin.clear(); // flush the input buffer
		while (!(g_peakthresh >= -inputRanges[unit->channelSettings[PS2000A_CHANNEL_A].range] // make sure input falls in an acceptable range
			&& g_peakthresh <= 0) // ^
			|| g_cinflag) // and there were no errors while taking in input
		{
			printf("\n\nPlease enter the threshold for the peak detection algorithm(-%d mV to 0 mV):\nA value of -200mV is recommended.\n\n",
				inputRanges[unit->channelSettings[PS2000A_CHANNEL_A].range]);
			printf("Peak Detection Threshold (mV): ");

			std::cin >> g_peakthresh; // take in the input
			g_cinflag = (std::cin.bad() || std::cin.fail()) ? TRUE : FALSE; // check if cin's error flags were set
			cinReset(); // flush the input buffer for future inputs
		}

		printf("Selected Peak Detection Threshold: %d mV \n", g_peakthresh);
	}

	// seems like we don't use these structs but they need to be passed as args still
	memset(&directions, 0, sizeof(TRIGGER_DIRECTIONS)); // set all the memory allocated for the struct to 0
	memset(&pulseWidth, 0, sizeof(PWQ)); // set all the memory allocated for the struct to 0

	if ((status = SetDefaults(unit)) != PICO_OK)
	{
		printf("%s", picoerrortoString(status, __LINE__, __func__, "SetDefaults").c_str());
		picoerrorLog(g_errorfp, status, __LINE__, __func__, "SetDefaults");
		return status;
	}

	// Trigger disabled	(for now)
	// not actually setting trigger here so we won't kill program if it fails to set
	if ((status = SetTrigger(unit, NULL, 0, NULL, 0, &directions, &pulseWidth, 0, 0, 0, 0, 0)) != PICO_OK)
	{
		printf("%s", picoerrortoString(status, __LINE__, __func__, "SetTrigger").c_str());
		picoerrorLog(g_errorfp, status, __LINE__, __func__, "SetTrigger");
	}

	return status;
}

/****************************************************************************
* CloseDevice
*
* Parameters
* - unit : pointer to the UNIT structure, where the handle will be stored
*
* Returns
* - none
****************************************************************************/
void CloseDevice(UNIT* unit)
{
	printf("Closing device...");
	PICO_STATUS status = ps2000aCloseUnit(unit->handle);
	if (status != PICO_OK)
	{
		printf("Failed to properly close the device.\n");
		printf("Handle: %d\n", unit->handle);
		printf("%s", picoerrortoString(status, __LINE__, __func__, "ps2000aCloseUnit").c_str());
		picoerrorLog(g_errorfp, status, __LINE__, __func__, "ps2000aCloseUnit");
		// close the files in the case of a failure on device closure
		if (g_peakfp != NULL)
		{
			fclose(g_peakfp); // close the peak info file
		}
		if (g_errorfp != NULL)
		{
			fclose(g_errorfp); // close the error log file
		}
		int16_t qinit = _kbhitinit();
		printf("Press the \'Q\' key to exit the program.\n");
		while (!_kbhitpoll(qinit));
		exit(99); // exit program
	}
	printf("done.\n");
}

int main()
{
	PICO_STATUS status; // to receive PICO_OK (success) or other various error codes from various function calls
	UNIT unit; // the UNIT structure, where the handle will be stored
	char ch; // program selection choice
	g_qinit = _kbhitinit(); // Initialize state of Q key so that we can quit later on in the program (global)
	int16_t qinit; // Initialize state of Q key so that we can quit later on in the program (for connection checks between runs)
	std::string starttimeinfo;

	// give the error log file a unique (time dependent) name so we don't overwrite anything
	starttimeinfo = timeInfotoString();
	errorfilename += starttimeinfo;
	errorfilename += ".txt";

	fopen_s(&g_errorfp, errorfilename.c_str(), "w");

	if (g_errorfp != NULL)
	{
		printf("Successfully opened the error log disk file (%s)\n", errorfilename.c_str());
		fprintf(g_errorfp, "Pico Error Log:\n\n");
	}
	else
	{
		printf("Cannot open the error log file \n%s\n for writing.\n"
			"Please ensure that you have permission to access and/ or the file isn't currently open.\n", errorfilename.c_str());
		printf("The program will continue, but errors will not be logged.\n");
	}

	// open the device, get its handle for the UNIT struct
	if ((status = OpenDevice(&unit)) != PICO_OK)
	{
		printf("%s", picoerrortoString(status, __LINE__, __func__, "OpenDevice").c_str());
		picoerrorLog(g_errorfp, status, __LINE__, __func__, "OpenDevice");
		if (g_errorfp != NULL)
		{
			fclose(g_errorfp);
		}
		return -1;
	}

	if (_kbhitpoll(g_qinit))
	{
		// CloseDevice exits the program on failure so need for a status check here
		CloseDevice(&unit);
		return 0;
	}

	// display choices
	printf("\n");
	printf("B - Triggered block                          X - Exit\n\n");
	printf("Operation:");

	ch = toupper(_getch()); // get the user's choice

	printf("\n\n");

	switch (ch)
	{
	case 'B': // collect block triggered
	{
		// give the peak info file a unique (time dependent) name so we don't overwrite anything
		// want this name matching with the error to make checking stuff later easier
		peakfilename += starttimeinfo;
		peakfilename += ".csv";

		fopen_s(&g_peakfp, peakfilename.c_str(), "w");

		if (g_peakfp != NULL)
		{
			printf("Successfully opened the peak data disk file (%s)\n", peakfilename.c_str());
		}
		else
		{
			printf("Cannot open the file \n%s\n for writing.\n"
				"Please ensure that you have permission to access and/ or the file isn't currently open.\n", peakfilename.c_str());
			if (g_errorfp != NULL)
			{
				fclose(g_errorfp);
			}
			qinit = _kbhitinit();
			printf("Press the \'Q\' key to exit the program.\n");
			while (!_kbhitpoll(qinit));
			return -1; // no point in continuing if we can't save any data
		}

		// give an initial unacceptable value so we enter the while loop
		g_numwavestosaved = -2;

		// select number of waveforms to save to .csv files
		std::cin.clear(); // flush the input buffer
		while (!(g_numwavestosaved >= -1 && g_numwavestosaved <= (std::numeric_limits<long>::max)()) // make sure input falls in an acceptable range
			|| g_cinflag) // and there were no errors while taking in input
		{
			printf("Please enter the number of multi-peak waveforms you'd like to save. (0-%d)\n", (std::numeric_limits<long>::max)());
			printf("Enter -1 if you wish to save every multi-peak waveform the scope records.\n");
			printf("Number of Waveforms: ");

			std::cin >> g_numwavestosaved; // take in the user input
			g_cinflag = (std::cin.bad() || std::cin.fail()) ? TRUE : FALSE; // check if cin's error flags were set
			cinReset(); // flush the input buffer for future inputs

		}

		printf("Selected number of multi-peak waveforms to save: %I64d\n", g_numwavestosaved);

		while (!_kbhitpoll(g_qinit)) // main data collection loop
		{
			// seems like using cin.clear() might mess with the key toggle states
			// in regards to the Windows API function, resetting the init value 
			// seems to take care of the problem
			if (g_firstRun == FALSE)
			{
				g_qinit = _kbhitinit();
			}

			// make sure the device is still connected
			if ((status = ps2000aPingUnit(unit.handle)) != PICO_OK)
			{
				printf("Issue with USB connection to device!\n");
				printf("%s", picoerrortoString(status, __LINE__, __func__, "ps2000aPingUnit").c_str());
				picoerrorLog(g_errorfp, status, __LINE__, __func__, "ps2000aPingUnit");
				if (g_peakfp != NULL)
				{
					fclose(g_peakfp); // close the peak info file
				}
				if (g_errorfp != NULL)
				{
					fclose(g_errorfp); // close the error log file
				}
				qinit = _kbhitinit();
				printf("Press the \'Q\' key to exit the program.\n");
				while (!_kbhitpoll(qinit));
				return -1;
			}

			// call the data collection routine
			// if it returns an error we can just run again for another try-> don't return the error code, just print 
			if ((status = CollectBlockTriggered(&unit)) != PICO_OK)
			{
				printf("%s", picoerrortoString(status, __LINE__, __func__, "CollectBlockTriggered").c_str());
				picoerrorLog(g_errorfp, status, __LINE__, __func__, "CollectBlockTriggered");
			}
		}

		if (g_peakfp != NULL)
		{
			fclose(g_peakfp); // close the peak info file
		}
		if (g_errorfp != NULL)
		{
			fclose(g_errorfp); // close the error log file
		}
	}
	break;

	case 'X': // exit
	{
		printf("Exiting.\n");
	}
	break;

	default: // invalid input
	{
		printf("Invalid operation.\n");
	}
	break;
	}

	CloseDevice(&unit); // close the device now that we're done

	return 0;
}
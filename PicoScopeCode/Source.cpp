#include <iostream>
#include "ps2000aApi.h"
#include <stdio.h>



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
	uint32_t nSamples = 1000; //different number here?
	int16_t buffer[1000];
	int barWidth = 70;

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

	// start with ps2000aRunStreaming()
			//Grab data either with ps2000aGetStreamingLatestValues() or alternate functions to access stored data
				//manipulation to check for double peaks here
				// use matplotlibcpp for some nice plots?
			//ps2000aCloseUnit()
	//flash LED for various parts of code running?
	std::cout << "Hello, World!" << std::endl;

	return 0;
}
/*
This is the XADC tutorial application main source file.
Details are explained on GitHub: https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP
This version was tested on FreeRTOS running on AMD Xilinx Zynq (Vitis 2023.1 toolchain).

BSD 2-Clause License:

Copyright (c) 2024 Viktor Nikolov

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include "FreeRTOS.h"
#include "task.h"
#include "xgpiops.h"
#include "xsysmon.h"
#include "xaxidma.h"
#include "lwip/sys.h"
#include "button_debounce.h"
#include "FileViaSocket.h"

#include <iostream>
#include <iomanip>
using std::cout;
using std::cerr;
using std::endl;

/* Number of samples transferred in one DMA transfer. Max. value is 33,554,431 */
#define SAMPLE_COUNT 100

#if SAMPLE_COUNT > 0x01FFFFFF
	#error "SAMPLE_COUNT is higher than possible max. of 33,554,431 (=0x01FFFFFF)"
#endif

/* Set XADC averaging.
 * Leave one of the lines below uncommented to set averaging mode of the XADC. */
//#define AVERAGING_MODE XSM_AVG_0_SAMPLES // No averaging
//#define AVERAGING_MODE XSM_AVG_16_SAMPLES  // Averaging over  16 acquisition samples
#define AVERAGING_MODE XSM_AVG_64_SAMPLES  // Averaging over  64 acquisition samples
//#define AVERAGING_MODE XSM_AVG_256_SAMPLES // Averaging over 256 acquisition samples

/* IP address and port of the server running the script file_via_socket.py.
 * The address must be provided in numerical form in a string, e.g., "192.168.44.10".*/
//const std::string    SERVER_ADDR( "###SERVER_ADDR is not set###" );
const std::string    SERVER_ADDR( "192.168.44.10" );
const unsigned short SERVER_PORT{ 65432 }; //The server script file_via_socket.py uses port 65432 by default.

//Size of the stack (as number of 32bit words) for threads we create:
#define STANDARD_THREAD_STACKSIZE 1024

extern sys_thread_t network_init_thread_handle; // Defined in network_thread.cpp
extern void network_init_thread(void *p);              // Defined in network_thread.cpp


/* We need the buffer to be aligned on an address divisible by 4.
 * We are also making it 16 bytes larger than needed, because we need to invalidate Data Cache
 * in a slightly bigger memory range. Otherwise we risk cache issues caused by end of the buffer
 * not aligned with cache line. */
static u16 DataBuffer[ SAMPLE_COUNT + 8 ] __attribute__((aligned(4)));

static XGpioPs GpioInstance;
static XSysMon XADCInstance;
static XAxiDma AxiDmaInstance;

enum class eXADCInput { VAUX1, VPVN };              // Enumeration type for valid XADCInputs
static eXADCInput ActiveXADCInput;                  // Defines, which input is active
static float (*Xadc_RawToVoltageFunc)(u16 RawData); // Pointer to the function for converting raw measurement to volts.
                                                    // We switch it between the function for AUX1 and the function for VP/VN.

static int GPIOInitialize()
{
	XGpioPs_Config *GpioConfig;
	XStatus Status;

	GpioConfig = XGpioPs_LookupConfig(XPAR_PS7_GPIO_0_DEVICE_ID);
	if(GpioConfig == NULL) {
		cerr << "XGpioPs_LookupConfig failed! terminating" << endl;
		return XST_FAILURE;
	}

	Status = XGpioPs_CfgInitialize(&GpioInstance, GpioConfig, GpioConfig->BaseAddr);
	if(Status != XST_SUCCESS) {
		cerr << "XGpioPs_CfgInitialize failed! terminating" << endl;
		return XST_FAILURE;
	}

	/*** Initialize the GPIO pins and drive them low ****/

	/* There are 64 EMIO GPIO pins on Zynq-7000. The first 32 pins are in Bank 2 (EMIO pin numbers 54 through 85),
	 * the rest of the pins are in Bank 3.
	 * In this code we assume that we are using EMIO pin 54 as start/stop signal and pins 55-80
	 * as the 25-bit value, which sets number of data samples we transfer form the XADC via DMA.
	 * EMIO pin 81 is connected to board's button BTN0 and EMIO pin 82 to BTN1 */
	XGpioPs_SetDirection( &GpioInstance, 2 /*Bank 2*/, 0x03FFFFFF );    //Set 26 EMIO pins 54-80 as outputs (pins 81 and 82 will be inputs)
	XGpioPs_Write( &GpioInstance, 2 /*Bank 2*/, SAMPLE_COUNT << 1 );    //Set sample count to pins 55-80 and set start/stop signal to 0
	XGpioPs_SetOutputEnable( &GpioInstance, 2 /*Bank 2*/, 0x03FFFFFF ); //Enable 26 EMIO pins 54-80

	return 0;
} // GPIOInitialize

static float Xadc_RawToVoltageAUX1(u16 RawData)
{
	const float Scale = 3.32; // We use AUX1 as unipolar; it has the scale from 0 V to 3.32 V.
	                          // There is voltage divider of R1 = 2.32 kOhm and R2 = 1 kOhm on the intput.

#if AVERAGING_MODE == XSM_AVG_0_SAMPLES
	// XADC doesn't do averaging, only top 12 bits of RawData are valid
	return Scale * ( float(RawData >> 4) / float(0xFFF) ); // We are not using averaging, only 12 most significant bits are valid, which
	                                                       // contain the XADC reading.
#else
	// XADC does average samples, all 16 bits of RawData are valid
	return Scale * ( float(RawData) / float(0xFFFF) );
#endif
} // Xadc_RawToVoltageAUX1

static float Xadc_RawToVoltageVPVN(u16 RawData)
{
#if AVERAGING_MODE == XSM_AVG_0_SAMPLES
	// XADC doesn't do averaging, only top 12 bits of RawData are valid

	if( (RawData >> 4) == 0x800 ) // This is the special case of the lowest negative value. The measuring range is -500 mV to 499.75 mV.
		return -0.5;

	float sign;

	if( RawData & 0x8000 ) {    // Is sign bit equal to 1? I.e. is RawData negative?
		sign = -1.0;
		RawData = ~RawData + 1; // Get absolute value from negative two's complement integer
	}
	else
		sign = 1.0;

	RawData = RawData >> 4; // We are not using averaging, only 12 most significant bits are valid, which
	                        // contain the XADC reading.
	return sign * float(RawData) * ( 1.0/4096.0 ); // One bit equals to the reading of 244 uV. I.e., 1/4096 == 244e-6
#else
	// XADC does average samples, all 16 bits of RawData are valid

	if( RawData == 0x8000 ) // This is the special case of the lowest negative value. The measuring range is -500 mV to 499.75 mV.
		return -0.5;

	float sign;

	if( RawData & 0x8000 ) {    // Is sign bit equal to 1? I.e. is RawData negative?
		sign = -1.0;
		RawData = ~RawData + 1; // Get absolute value from negative two's complement integer
	}
	else
		sign = 1.0;

	return sign * float(RawData) * ( 1.0/65535.0 ); // One bit equals to the reading of 1/65535 volts
#endif
} // Xadc_RawToVoltageVPVN

// Convert 12-bit two's complement integer stored in u16 to int16_t
static int16_t Convert12BitToSigned16Bit(u16 num)
{
    // Check if the number is negative
    if (num & 0x800) {
        // If negative, sign-extend to 16 bits
        num |= 0xF000;
    }
    return (int16_t)num;
} // Convert12BitToSigned16Bit

// Convert raw value of the XADC Gain Calibration Coefficient to the percentage of gain correction.
static float ConvertRawGainCoefToPercents(u16 num)
{
	float res = (num & 0x3F) * 0.1; // Bottom 6 bits contain number of tenthes of percent
    if( (num & 0x40) == 0 )         // 7th bit is sign bit, value 0 means negative coefficient
    	res *= -1;
    return res;
} // ConvertRawGainCoefToPercents

static int XADCInitialize()
{
	XSysMon_Config *ConfigPtr;
	XStatus Status;

	ConfigPtr = XSysMon_LookupConfig(XPAR_XADC_WIZ_0_DEVICE_ID);
	if(ConfigPtr == NULL) {
		cerr << "XSysMon_LookupConfig failed! terminating" << endl;
		return XST_FAILURE;
	}

	Status = XSysMon_CfgInitialize(&XADCInstance, ConfigPtr, ConfigPtr->BaseAddress);
	if(Status != XST_SUCCESS) {
		cerr << "XSysMon_CfgInitialize failed! terminating" << endl;
		return XST_FAILURE;
	}

	// Print values of calibration coefficients. (Calibration was done automatically during FPGA configuration.)
	u16 ADCOffsetCoeff = XSysMon_GetCalibCoefficient(&XADCInstance, XSM_CALIB_ADC_OFFSET_COEFF); //Read value of the Offset Calibration Coefficient from the XADC register
	cout << "calib coefficient ADC offset: "
	     << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << ADCOffsetCoeff  // Print Offset Coeff. raw value in hex
	     << std::dec << " (" << Convert12BitToSigned16Bit(ADCOffsetCoeff >> 4) << ')' << endl; // Print the Offset Coeff. value

	u16 GainCoeff = XSysMon_GetCalibCoefficient(&XADCInstance, XSM_CALIB_GAIN_ERROR_COEFF); //Read value of the Gain Calibration Coefficient from the XADC register
	cout << "calib coefficient gain error: "
	     << std::hex << std::setw(4) << GainCoeff                               // Print Gain Coeff. raw value in hex
         << std::dec << std::fixed << std::setprecision(1) << std::showpoint
	     << " ("  << ConvertRawGainCoefToPercents(GainCoeff) << " %)" << endl;  // Print Gain Coeff. value

	// Disable all interrupts
	XSysMon_IntrGlobalDisable(&XADCInstance);
	// Disable the Channel Sequencer
	XSysMon_SetSequencerMode(&XADCInstance, XSM_SEQ_MODE_SINGCHAN);
	// Disable all alarms
	XSysMon_SetAlarmEnables(&XADCInstance, 0);

	// Set averaging mode
	XSysMon_SetAvg(&XADCInstance, AVERAGING_MODE); // Select averaging mode by defining value of the macro AVERAGING_MODE

	// Just in case: Disable averaging for the calculation of the calibration coefficients in Configuration Register 0
	u32 RegValue = XSysMon_ReadReg(XADCInstance.Config.BaseAddress, XSM_CFR0_OFFSET);
	RegValue |= XSM_CFR0_CAL_AVG_MASK; //To disable averaging, set bit XSM_CFR0_CAL_AVG_MASK to 1
	XSysMon_WriteReg(XADCInstance.Config.BaseAddress, XSM_CFR0_OFFSET, RegValue);

	/* Enable offset and gain calibration
	 * When internal FPGA voltage references are used, the Gain Calibration Coefficient has constant value 0x007F and should be ignored.
	 * Cora Z7 is an example of Zynq board relying on internal voltage references.
	 * See details explained here: https://support.xilinx.com/s/article/53586
	 */
	u16 CalibrationEnables;
	if( GainCoeff != 0x007F ) // True when external voltage reference is used
		CalibrationEnables = XSM_CFR1_CAL_ADC_GAIN_OFFSET_MASK | XSM_CFR1_CAL_PS_GAIN_OFFSET_MASK; // Use both Offset and Gain Coefficients
	else
		CalibrationEnables = XSM_CFR1_CAL_ADC_OFFSET_MASK | XSM_CFR1_CAL_PS_OFFSET_MASK;           // Use only Offset Coefficient
	XSysMon_SetCalibEnables(&XADCInstance, CalibrationEnables ); //Set the correct use of the calibration coefficients

	return XST_SUCCESS;
} // XADCInitialize

// Activate the XADC input based on value of the variable ActiveXADCInput
static int ActivateXADCInput()
{
	XStatus Status;

	if( ActiveXADCInput == eXADCInput::VAUX1 ) {
		// Set the ADCCLK frequency equal to 1/6 of XADC input clock in order to have correct acquisition time
/////		XSysMon_SetAdcClkDivisor(&XADCInstance, 6);

		// Set single channel mode for VAUX1 analog input
		Status = XSysMon_SetSingleChParams( &XADCInstance,
											XSM_CH_AUX_MIN+1, // == channel bit of VAUX1 == Cora Z7 board pin A0
/////											true,             // IncreaseAcqCycles==false -> default 4 ADCCLKs used for the acquisition; true -> 10 ADCCLKs used
											false,            // IncreaseAcqCycles==false -> default 4 ADCCLKs used for the acquisition; true -> 10 ADCCLKs used
											false,            // IsEventMode==false -> continuous sampling
											false );          // IsDifferentialMode==false -> unipolar mode
		if(Status != XST_SUCCESS) {
			cerr << "XSysMon_SetSingleChParams for VAUX1 failed! terminating" << endl;
			return XST_FAILURE;
		}

		Xadc_RawToVoltageFunc = Xadc_RawToVoltageAUX1;
		cout << "VAUX1 is activated as the input" << endl;
	}
	else if( ActiveXADCInput == eXADCInput::VPVN ) {
		// Set the ADCCLK frequency equal to 1/4 of XADC input clock. We can have shorter acquisition time for VPVN input
/////		XSysMon_SetAdcClkDivisor(&XADCInstance, 4);

		// Set single channel mode for VP/VN dedicated analog inputs
		Status = XSysMon_SetSingleChParams( &XADCInstance,
											XSM_CH_VPVN,      // == channel bit of VP/VN == Cora Z7 board pins V_P and V_N
											false,            // IncreaseAcqCycles==false -> default 4 ADCCLKs used for the acquisition; true -> 10 ADCCLKs used
											false,            // IsEventMode==false -> continuous sampling
											true );           // IsDifferentialMode==true -> bipolar mode
		if(Status != XST_SUCCESS) {
			cerr << "XSysMon_SetSingleChParams for VP/VN failed! terminating" << endl;
			return XST_FAILURE;
		}

		Xadc_RawToVoltageFunc = Xadc_RawToVoltageVPVN;
		cout << "VPVN is activated as the input" << endl;
	}
	else {
		cerr << "Called ActivateXADCInput() for an unknown input!" << endl;
		return XST_FAILURE;
	}
	return XST_SUCCESS;
} // ActivateXADCInput

static int DMAInitialize()
{
	XAxiDma_Config *cfgptr;
	XStatus Status;

    cfgptr = XAxiDma_LookupConfig(XPAR_AXI_DMA_DEVICE_ID);
	if(cfgptr == NULL) {
		cerr << "XAxiDma_LookupConfig  failed! terminating" << endl;
		return XST_FAILURE;
	}

	Status = XAxiDma_CfgInitialize(&AxiDmaInstance, cfgptr);
	if(Status != XST_SUCCESS) {
		cerr << "XAxiDma_CfgInitialize failed! terminating" << endl;
		return XST_FAILURE;
	}

	// Disable interrupts
	XAxiDma_IntrDisable(&AxiDmaInstance, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DEVICE_TO_DMA);
	XAxiDma_IntrDisable(&AxiDmaInstance, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DMA_TO_DEVICE);

	return 0;
} // DMAInitialize

static int ReceiveData()
{
	Xil_DCacheFlushRange( (UINTPTR)DataBuffer, sizeof(DataBuffer) );  // Just in case, flush any data in DataBuffer, held in CPU cache, to RAM

	// Initiate the DMA transfer
	XStatus Status;
	Status = XAxiDma_SimpleTransfer( &AxiDmaInstance, (UINTPTR)DataBuffer, SAMPLE_COUNT * sizeof(u16), XAXIDMA_DEVICE_TO_DMA );
	if(Status != XST_SUCCESS) {
		cerr << "XAxiDma_SimpleTransfer failed! terminating" << endl;
		return XST_FAILURE;
	}

	XGpioPs_WritePin( &GpioInstance, 54, 1 /*high*/ ); // Set start signal to start generation of the AXI Stream of data coming from XADC
	XGpioPs_WritePin( &GpioInstance, 54, 0 /*low*/  ); // Reset the start signal (it needed to be high for just a single PL clock cycle)

	while( XAxiDma_Busy(&AxiDmaInstance, XAXIDMA_DEVICE_TO_DMA) ) // Wait till DMA transfer is done
		vTaskDelay( pdMS_TO_TICKS( 1 ) );

	/* Invalidate the CPU cache for memory block holding the DataBuffer.
	 * DMA transfer wasn't using the CPU cache, it wrote directly to RAM.
	 * We need the CPU to get data from RAM, not cache, when processing the DataBuffer.
	 */
	Xil_DCacheInvalidateRange( (UINTPTR)DataBuffer, sizeof(DataBuffer) );

	return 0;
} // ReceiveData

void XADC_thread(void *p)
{
	cout << "***** XADC THREAD STARTED *****\n";
	cout << "will connect to the network address " << SERVER_ADDR << ':' << SERVER_PORT << endl;
	cout << "samples per DMA transfer: " << SAMPLE_COUNT << endl;

	const std::string AveragingModeDescr[] = { "no", "16 samples", "64 samples", "256 samples" };
	cout << AveragingModeDescr[AVERAGING_MODE] << " averaging is used" << endl;

	// Initialize the subsystems
	if( GPIOInitialize() == XST_FAILURE )
		vTaskDelete(NULL); // We end this task on error
	if( XADCInitialize() == XST_FAILURE )
		vTaskDelete(NULL);
	if( DMAInitialize()  == XST_FAILURE )
		vTaskDelete(NULL);

	cout << "\npress BTN0 to start ADC conversion" << endl
	     << "press BTN1 to switch between VAUX1 and VPVN inputs" << endl;

	// Activate VAUX1 as input
	ActiveXADCInput = eXADCInput::VAUX1;
	if( ActivateXADCInput() == XST_FAILURE )
		vTaskDelete(NULL);

	Debouncer btns( 0 ); // Passing 0 to the constructor because our buttons are pull-down buttons

	while(1) {
		btns.ButtonProcess( XGpioPs_Read( &GpioInstance, 2 /*Bank 2*/ ) >> 26 ); // Give status of the buttons to the debouncer

		if( btns.ButtonPressed(BUTTON_PIN_0) ) { // If Cora Z7 button BTN0 was pressed
			if( ReceiveData() == XST_FAILURE )
				vTaskDelete(NULL); // We end this task

			// Print data sample to the console
			cout << "\n***** XADC DATA[0..7] *****\n";
			cout << std::defaultfloat << std::setprecision(7); // Disabling std::fixed used in previous output
			for( int i = 0; i < 8; i++ )
				cout << Xadc_RawToVoltageFunc( DataBuffer[i]) << endl;

			// Transfer data over the network
			try {
				FileViaSocket f( SERVER_ADDR, SERVER_PORT ); // Declare the object and open the connection

				cout << "sending data..." << std::flush;
				f << std::setprecision(7); // Set decimal precision for the output
				for( int i = 0; i < SAMPLE_COUNT; i++ )
					f << Xadc_RawToVoltageFunc( DataBuffer[i]) << '\n'; /* We are using '\n' on purpose instead of std::endl, because
					                                                     * std::endl has a side effect of flushing the buffer, i.e.,
					                                                     * each single value would be immediately sent in a TCP packet. */
				cout << "   sent" << endl;
			} // Object f ceases to exist, destructor on f is called, buffer is flushed,
			catch( const std::exception& e ) {
				cerr << "Error on opening the socket:\n" << e.what() << endl;
			}
		}

		if( btns.ButtonPressed(BUTTON_PIN_1) ) { // If Cora Z7 button BTN1 was pressed
			// Activate the other channel as input
			ActiveXADCInput = ActiveXADCInput==eXADCInput::VAUX1 ? eXADCInput::VPVN : eXADCInput::VAUX1;
			if( ActivateXADCInput() == XST_FAILURE )
				vTaskDelete(NULL);
		}

		vTaskDelay( pdMS_TO_TICKS( 1 ) );
	} // while(1)
} // XADC_thread

int main()
{
	cout << "\n*************** PROGRAM STARTED ***************" << endl;

	/* Starting the thread, which initializes the network.
	 * It will start the XADC_thread when the network is ready. */
	network_init_thread_handle = sys_thread_new("network_init_thread", network_init_thread, 0,
	                                            STANDARD_THREAD_STACKSIZE,
	                                            DEFAULT_THREAD_PRIO);
	vTaskStartScheduler();

	return 0;
} //main

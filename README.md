# Tutorial: Xilinx Zynq XADC using DMA and network streaming
This tutorial shows how to do a HW design and code a SW application to make use of Xilinx [Zynq XADC](https://www.xilinx.com/products/technology/analog-mixed-signal.html). We will also see how to use the [DMA](https://www.xilinx.com/products/intellectual-property/axi_dma.html) to transfer data from the XADC into Zynq CPU's memory and stream data to a remote server over the network.

In this tutorial, I'm using the Digilent board [Cora Z7-07S](https://digilent.com/shop/cora-z7-zynq-7000-single-core-for-arm-fpga-soc-development/). However, all the principles described here can be used on any other Zynq board. I will highlight aspects specific to Cora Z7 in the text.  
The Cora Z7 is a suitable board for testing the Zynq XADC because it has analog inputs that are usable in a practical way.

The tutorial is based on the Vivado 2023.1 and Vitis 2023.1 toolchain.

## TODOs, to be removed

- [Real Digital Signal Processing - Hackster.io](https://www.hackster.io/adam-taylor/real-digital-signal-processing-0bea44)
- [Signal Processing with XADC and PYNQ - Hackster.io](https://www.hackster.io/adam-taylor/signal-processing-with-xadc-and-pynq-3c716c)
- calibration:

  - [53586 - Zynq and 7-Series XADC Gain Calibration Behaviour with Internal Voltage Reference (xilinx.com)](https://support.xilinx.com/s/article/53586?language=en_US)
  - [The analog input for XADC calibration in 7 series FPGA (xilinx.com)](https://support.xilinx.com/s/question/0D52E00006hpPXlSAM/the-analog-input-for-xadc-calibration-in-7-series-fpga?language=en_US)

Cora Z7 has VREFP and VREFN connected to ADCGND

- The XADC also has an on-chip reference option which is selected by connecting VREFP and VREFN to ADCGND as shown in Figure 6-1. Due to reduced accuracy, the on-chip reference does impact the measurement performance of the XADC as explained previously

[Configuring the Zynq TTC to schedule FreeRTOS tasks : r/FPGA (reddit.com)](https://www.reddit.com/r/FPGA/comments/16bfugb/configuring_the_zynq_ttc_to_schedule_freertos/)

## A short introduction to Zynq XADC

Zynq XADC is a feature of an analog-to-digital converter integrated on selected Xilinx FPGA chips, including Zynq 7000. This ADC has two basic capabilities

1.  The System Monitor (SYSMON) reads the Zynq chip temperature and voltages of various Zynq power rails.
1.  The XADC reads voltages from external inputs, which are called channels.

In this tutorial, we will focus solely on XADC. But don't be confused, Xilinx library functions for controlling XADC are defined in [xsysmon.h](https://github.com/Xilinx/embeddedsw/blob/master/XilinxProcessorIPLib/drivers/sysmon/src/xsysmon.h).

XADC can read one external input (channel) at a time and provides a means for switching between channels. Zynq XADC has one dedicated analog input channel called V<sub>P</sub> /V<sub>N</sub> and 16 so-called auxiliary channels named VAUX[0..15].

For using the XADC you need to instantiate an [XADC Wizard IP](https://www.xilinx.com/products/intellectual-property/xadc-wizard.html) in your HW design.  
If you don't need to modify the XADC configuration during runtime, you can do all the needed setup in the configuration of the XADC Wizzard IP.  
Alternatively, you can configure XADC by calling functions defined in [xsysmon.h](https://github.com/Xilinx/embeddedsw/blob/master/XilinxProcessorIPLib/drivers/sysmon/src/xsysmon.h). This allows you to change the configuration during runtime (e.g., switching between the channels). We will use this method of configuration in this tutorial.

The XADC can run in several operating modes, see the [relevant chapter](https://docs.amd.com/r/en-US/ug480_7Series_XADC/XADC-Operating-Modes) of the [UG480](https://docs.amd.com/r/en-US/ug480_7Series_XADC).  
In this tutorial, we will use the simplest one, the Single Channel Mode.  
We will also configure the XADC for Continuous Sampling. In this timing mode, the XADC does the conversions one after another thus generating a stream of data, which we will process through the DMA.

## Acquisition time

> [!IMPORTANT]
>
> The XADC's rated maximum sampling frequency is 1 Msps. But it doesn't mean that you can run the XADC on 1&nbsp;Msps in all circumstances!
>
> The XADC sampling frequency must be carefully determined to allow sufficient acquisition time given the properties of the circuit you are using. It requires a bit of math, as we explain in this chapter.

The principle of XADC operation is charging an internal capacitor to a voltage equal to the voltage of the analog input being measured. Any electrical resistance between the input voltage and the internal capacitor will of course slow down the charging of the capacitor.  
If you don't give the internal XADC capacitor enough time to charge, the input voltage determined by the XADC will be lower than the actual voltage of the input.

The next picture is a copy of [Figure 2-5](https://docs.amd.com/r/qOeib0vlzXa1isUAfuFzOQ/Jknshmzrw3DvMZgWJO73KQ?section=XREF_26771_X_Ref_Target) from Zynq 7000 XADC User Guide [UG480](https://docs.amd.com/r/en-US/ug480_7Series_XADC), chapter "Analog Input Description" (page 22 of the PDF version of UG480).

<img src="pictures\UG480_fig_2-5.png" title=""  width="650">

### Unipolar input of Cora Z7

Equation 2-2 for acquisition time in unipolar mode:
$$t_{ACQ} = 9 \times ( R_{MUX} + R_{MUX} ) \times C_{SAMPLE}$$

In the case of Cora Z7, we need to take into account the unipolar input circuitry on the board as depicted in Figure 13.2.1 from the Cora Z7 [Reference Manual](https://digilent.com/reference/programmable-logic/cora-z7/reference-manual#shield_analog_io):

<img src="pictures\cora-analog-single-ended.png" title=""  width="550">

In our case, $R_{MUX}$ equals to 10 kΩ because we are using the auxiliary input VAUX1 (which is connected to pin A0 on the Cora Z7 board).  
In addition to $R_{MUX}$, we must include resistors in the signal path on the Cora Z7 board: 2.32&nbsp;kΩ, 140&nbsp;Ω, and 845&nbsp;Ω.  
$C_{SAMPLE}$ is specified by Xilinx as 3 pF.

We now calculate the needed acquisition time for VAUX1 as follows:
$$t_{ACQ} = 9 \times ( 10000 + 10000 + 2320 + 140 + 845 ) \times 3 \times 10^{-12} = 629\mskip3muns$$

> [!IMPORTANT]
>
> **TODO:**  
> Beware of the impedance of the circuit you connect to the A0 pin.

**TODO:**  
We will set the XADC to use 10 ADCCLK clocks for the acquisition. For 10 clocks to have a duration of 629 ns, we would need to use a frequency of 15.898&nbsp;MHz. 
We need to find an XADC input frequency DCLK, which, divided by an integer, results in a frequency close to 15.898&nbsp;MHz.

Using a Clocking Wizard, we are able to generate an output frequency of 95.363 MHz (with the Wizard clocked by 50&nbsp;MHz from the Zynq FCLK_CLK0).  
95.363 MHz divided by 6 gives us a DCLK of 15.894 MHz, which is very close to the value we desire.

With ADCCLK of 15.894 MHz, we will achieve a sampling rate of 497 ksps (a single conversion cycle will take 32 ADCLKs).

### Bipolar input  of Cora Z7

Equation 2-1 from UG480 for acquisition time in unipolar mode:
$$t_{ACQ} = 9 \times R_{MUX} \times C_{SAMPLE}$$

On Cora Z7, we need to take into account the bipolar input circuitry for dedicated V_P/V_N input on the board, as depicted in Figure 13.2.3 from the Cora Z7 [Reference Manual](https://digilent.com/reference/programmable-logic/cora-z7/reference-manual#shield_analog_io):

<img src="pictures\cora-analog-dedicated.png"  width="400">

The $R_{MUX}$ for a dedicated analog input is 100 Ω.  
In addition to $R_{MUX}$, we must include the 140 Ω resistor in the signal path on the Cora&nbsp;Z7 board  
$C_{SAMPLE}$ is 3 pF.

We now calculate the needed acquisition time for V_P/V_N as follows:
$$t_{ACQ} = 9 \times ( 100 + 140) \times 3 \times 10^{-12} = 6.5\mskip3muns$$

**TODO:**  
This would allow us to use a sampling rate of 1 Msps because, with the ADCCLK frequency of 26&nbsp;MHz and 4 ADCCLKs allowed for the acquisition, we get 150&nbsp;ns acquisition time, which is more than enough.

However, in our design, we are limited to using an ADCCLK frequency of 23.84&nbsp;MHz (95.363&nbsp;MHz divided by 4) because we must use an XADC clock of 95.363&nbsp;MHz to achieve the 629&nbsp;ns acquisition time for the unipolar input as described in the previous chapter.  
The resulting sampling rate will be, therefore, 917 ksps (a single conversion cycle will take 26 ADCLKs).

## Calibration

TODO

From [UG480](https://docs.amd.com/r/en-US/ug480_7Series_XADC/Status-Registers), I understood that the calibration coefficients are calculated by initiating a conversion on channel 8. 

I'm using a Digilent [Cora Z7](https://digilent.com/reference/programmable-logic/cora-z7/reference-manual) board, which doesn't provide external voltage references. VREFP and VREFN are connected to ADCGND on this board. Therefore, the internal FPGA voltage references are utilized for calibration.

From [this post](https://support.xilinx.com/s/article/53586?language=en_US), I know that with internal references, the Gain Calibration Coefficient will always be 0x7F.

When the default mode is enabled, both ADCs are calibrated. The XADC also operates in default mode after initial power-up and during FPGA configuration. See the [UG480](https://docs.amd.com/r/en-US/ug480_7Series_XADC/Sequencer-Modes), chapter Default Mode, page 48.

So unless you want to recalibrate the XADC during runtime, you never need to care about calibration by initiating a conversion on channel 8. It's done during FPGA configuration, which happens also when you re-run the PS code. 

## DMA

TODO

## Measurements

TODO

Connected precise 2.5 V voltage reference to A0 (2.50026 V).

Without averaging, the mean value over 10,000 samples was 2.492 V. 

Beware of the precision of the resistors in the Cora Z7 voltage dividers.

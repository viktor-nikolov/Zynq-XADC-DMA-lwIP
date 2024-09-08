# Tutorial: Xilinx Zynq XADC using DMA and network streaming
This tutorial shows how to do a HW design and code a SW application to make use of AMD Xilinx Zynq-7000 [XADC](https://www.xilinx.com/products/technology/analog-mixed-signal.html). We will also see how to use the [DMA](https://www.xilinx.com/products/intellectual-property/axi_dma.html) to transfer data from the XADC into Zynq CPU's memory and stream data to a remote server over the network.

In this tutorial, I'm using the Digilent board [Cora Z7-07S](https://digilent.com/shop/cora-z7-zynq-7000-single-core-for-arm-fpga-soc-development/). However, all the principles described here can be used on any other Zynq-7000 board. I will highlight aspects specific to Cora Z7 in the text.  
The Cora Z7 is a suitable board for testing the Zynq XADC because it has analog inputs that are usable in a practical way.

The tutorial is based on the Vivado 2023.1 and Vitis 2023.1 toolchain.

## TODOs, to be removed

- Oversampling: https://www.silabs.com/documents/public/application-notes/an118.pdf
- calibration:
  - [53586 - Zynq and 7-Series XADC Gain Calibration Behaviour with Internal Voltage Reference (xilinx.com)](https://support.xilinx.com/s/article/53586?language=en_US)
  - [The analog input for XADC calibration in 7 series FPGA (xilinx.com)](https://support.xilinx.com/s/question/0D52E00006hpPXlSAM/the-analog-input-for-xadc-calibration-in-7-series-fpga?language=en_US)

Cora Z7 has VREFP and VREFN connected to ADCGND

- The XADC also has an on-chip reference option which is selected by connecting VREFP and VREFN to ADCGND as shown in Figure 6-1. Due to reduced accuracy, the on-chip reference does impact the measurement performance of the XADC as explained previously



We will set the XADC to use 10 ADCCLK clocks for the acquisition. For 10 clocks to have a duration of 629 ns, we would need to use a frequency of 15.898&nbsp;MHz. 
We need to find an XADC input frequency DCLK, which, divided by an integer, results in a frequency close to 15.898&nbsp;MHz.

Using a Clocking Wizard, we are able to generate an output frequency of 95.363 MHz (with the Wizard clocked by 50&nbsp;MHz from the Zynq FCLK_CLK0).  
95.363 MHz divided by 6 gives us a DCLK of 15.894 MHz, which is very close to the value we desire.

With ADCCLK of 15.894 MHz, we will achieve a sampling rate of 497 ksps (a single conversion cycle will take 32 ADCCLKs).

## A short introduction to Zynq-7000 XADC

### What is XADC

The XADC is a feature of an analog-to-digital converter integrated on selected Xilinx FPGA chips, including Zynq-7000. This ADC has two basic capabilities

1.  The System Monitor (SYSMON) reads the Zynq chip temperature and voltages of various Zynq power rails.
1.  The XADC reads voltages from external inputs, which are called channels.

In this tutorial, we will focus solely on XADC. But please don't get confused, Xilinx library functions for controlling XADC are defined in [xsysmon.h](https://github.com/Xilinx/embeddedsw/blob/master/XilinxProcessorIPLib/drivers/sysmon/src/xsysmon.h).

XADC can read one external input (channel) at a time and provides a means for switching between channels.  
The Zynq-7000 XADC has one dedicated analog input channel called V<sub>P</sub> /V<sub>N</sub> and 16 so-called auxiliary channels named VAUX[0..15].
Each channel has two input signals because it is a differential input channel. A positive differential input is denoted V<sub>P</sub> or VAUXP, and a negative one is denoted V<sub>N</sub> or VAUXN.

> [!WARNING]
>
> All input voltages must be positive with respect to the analog ground (GNDADC) of the Xilinx chip.  
> I.e., V<sub>N</sub> can't go below GNDADC even though it's called "negative input".

A channel may operate in unipolar or bipolar mode.

#### Unipolar mode channel

- The differential analog inputs (V<sub>P</sub> and V<sub>N</sub>) have an input range of 0 V to 1.0 V.
- The voltage on V<sub>P</sub> (measured with respect to V<sub>N</sub>) must always be positive. I.e., the XADC output is a value between 0 V and 1.0 V
- V<sub>N</sub> is typically connected to a local ground or common mode signal.

#### Bipolar mode channel

- This mode can accommodate input signals driven from a true differential source.
- The differential analog input (V<sub>P</sub> &minus; V<sub>N</sub>) can have a maximum input range of ±0.5V. I.e., the XADC output has a value between &minus;0.5 V and 0.5 V.
- However, both V<sub>P</sub> and V<sub>N</sub> must be always within a range from 0 V to 1.0 V with respect to GNDADC.

See the chapter [Analog Inputs](https://docs.amd.com/r/en-US/ug480_7Series_XADC/Analog-Inputs) of [UG480](https://docs.amd.com/r/en-US/ug480_7Series_XADC) for more details on unipolar and bipolar inputs.

### Configuration

To use the XADC, you need to instantiate an [XADC Wizard IP](https://www.xilinx.com/products/intellectual-property/xadc-wizard.html) in your HW design.  
If you don't need to modify the XADC configuration during runtime, you can do all the needed setup in the XADC Wizzard IP configuration.  
Alternatively, you can configure XADC by calling functions defined in [xsysmon.h](https://github.com/Xilinx/embeddedsw/blob/master/XilinxProcessorIPLib/drivers/sysmon/src/xsysmon.h) (this applies to programs running in PS and XADC Wizard configured to have AXI4Lite interface). Functions from [xsysmon.h](https://github.com/Xilinx/embeddedsw/blob/master/XilinxProcessorIPLib/drivers/sysmon/src/xsysmon.h) allow you to change the configuration during runtime (e.g., switching between the channels). We will use this method of configuration in this tutorial.  
If you need to control the XADC from FPGA logic, the use of the [dynamic reconfiguration port](https://docs.amd.com/r/en-US/ug480_7Series_XADC/Dynamic-Reconfiguration-Port-DRP-Timing) (DRP) interface is recommended. DRP is outside of the scope of this tutorial.

The Zynq-7000 XADC can run in several operating modes, see the [relevant chapter](https://docs.amd.com/r/en-US/ug480_7Series_XADC/XADC-Operating-Modes) of the [UG480](https://docs.amd.com/r/en-US/ug480_7Series_XADC).  
In this tutorial, we will use the simplest one, the Single Channel Mode.  
We will also configure the XADC for Continuous Sampling. In this timing mode, the XADC performs the conversions one after another, generating a stream of data that we will process through the DMA.

### Bit resolution

Zynq-7000 XADC is a 12-bit ADC. However, the XADC [status registers](https://docs.amd.com/r/en-US/ug480_7Series_XADC/Status-Registers) storing the conversion result are 16-bit, and the function [XSysMon_GetAdcData](https://github.com/Xilinx/embeddedsw/blob/5688620af40994a0012ef5db3c873e1de3f20e9f/XilinxProcessorIPLib/drivers/sysmon/src/xsysmon.c#L331) also returns a 16-bit value.
In general, the 12 most significant bits of the register are the converted XADC sample. Do ignore the 4 least significant bits.

It is possible to configure the XADC to do an averaging of consecutive 16, 64 or 256 samples (see function [XSysMon_SetAvg](https://github.com/Xilinx/embeddedsw/blob/5688620af40994a0012ef5db3c873e1de3f20e9f/XilinxProcessorIPLib/drivers/sysmon/src/xsysmon.c#L488)). I.e., to do the oversampling. The 4 least significant bits are then used to represent the averaged value with enhanced precision, i.e., the whole 16 bits of a status register can be used.  
Obviously, letting the XADC do the averaging makes sense for slowly changing input signals where noise is expected to be removed by the averaging.

### Clocking, sampling rate, and bandwidth

The XADC is driven by the input clock DCLK.  
When the XADC Wizard is configured to have an AIX4Lite interface (which is what we will do), the DCLK is driven by the s_axi_aclk clock of the AXI interface.

The ADC circuitry within the XADC is driven by the clock ADCCLK, which is derived from DCLK by a configurable ratio divider. The minimum possible divider ratio is 2, and the maximum ratio is 255.  
The divider ratio can be configured dynamically by the function [XSysMon_SetAdcClkDivisor()](https://github.com/Xilinx/embeddedsw/blob/5688620af40994a0012ef5db3c873e1de3f20e9f/XilinxProcessorIPLib/drivers/sysmon/src/xsysmon.c#L1089).

In the default XADC setup of Continuous Sampling mode, 26 ADCCLK cycles are required to acquire an analog signal and perform a conversion.  
The 26 ADCCLK cycle period can be extended to 32 cycles by configuration. See the boolean parameter IncreaseAcqCycles of the [XSysMon_SetSingleChParams()](https://github.com/Xilinx/embeddedsw/blob/5688620af40994a0012ef5db3c873e1de3f20e9f/XilinxProcessorIPLib/drivers/sysmon/src/xsysmon.c#L586). The parameter controls the duration of the so-called settling period (this is what [UG480](https://docs.amd.com/r/en-US/ug480_7Series_XADC/Continuous-Sampling) calls it), which is 4 ADCCLK cycles by default and can be extended to 10 cycles.

> [!NOTE]
>
> Don't be confused by the different vocabulary used in Xillinx products regarding this 4 or 10 ADCCLK period within the XADC's acquisition and conversion cycle.  
> [UG480](https://docs.amd.com/r/en-US/ug480_7Series_XADC/Continuous-Sampling) calls it a "settling period," but it is called "Acquisition Time" in the UI of XADC Wizard IP in Vivado and comments in the [xsysmon.c](https://github.com/Xilinx/embeddedsw/blob/master/XilinxProcessorIPLib/drivers/sysmon/src/xsysmon.c).  
> However, [Figure 5-1](https://docs.amd.com/r/qOeib0vlzXa1isUAfuFzOQ/4MmXaAzpjJTjs~BpjCs4Rw?section=XREF_95899_X_Ref_Target) in the [UG480](https://docs.amd.com/r/en-US/ug480_7Series_XADC/Continuous-Sampling) clearly shows that the acquisition time is longer than 4 or 10 ADCCLK clocks.
>
> The "settling period" is probably the best term for the reasons I explain later in this text.

The XADC maximum sampling rate is 1&nbsp;Msps.  
This is achieved by having 104 MHz DCLK and the divider ratio set to 4. This results in the highest possible ADCCLK frequency of 26 MHz. Using 26 ADCCLK cycles for single conversion then gives 1 Msps.

> [!IMPORTANT]
>
> XADC can run at 1 Msps, but that doesn't mean it's wise to run it at 1&nbsp;Msps in all circumstances. It very much depends on the circuitry before the XADC and the characteristics of the input signal.
> I will explore this topic in the following chapters.

> [!IMPORTANT]
>
> The guaranteed analog bandwidth of auxiliary channels is only 250 kHz.
> The Zynq-7000 SoC [Data Sheet DS187](https://docs.amd.com/v/u/en-US/ds187-XC7Z010-XC7Z020-Data-Sheet) states on page 68 that the Auxiliary Channel Full Resolution Bandwidth is 250 kHz.

You can sample an auxiliary channel at 1 Msps. However, signals with a frequency higher than 250 kHz are not guaranteed to be precisely recorded. This seems to be an analog bandwidth limitation of Zynq-7000 circuits related to auxiliary channels.

The data sheet doesn't mention the bandwidth of the dedicated analog input channel V<sub>P</sub> /V<sub>N</sub>. We can assume it is at least 500 kHz (i.e., the [Nyquist frequency](https://en.wikipedia.org/wiki/Nyquist_frequency) for 1 Msps ADC).

## Acquisition and settling time—the theory

In this chapter, I will summarize what Zynq-7000 XADC User Guide [UG480](https://docs.amd.com/r/en-US/ug480_7Series_XADC) and the Application Note [XAPP795](https://docs.amd.com/v/u/en-US/xapp795-driving-xadc) tell us about acquisition and settling time.

### Acquisition time

The principle of XADC operation is charging an internal capacitor to a voltage equal to the voltage of the analog input being measured. Any electrical resistance between the input voltage and the internal capacitor will, of course, slow down the charging of the capacitor.  
If you don't give the internal XADC capacitor enough time to charge, the input voltage determined by the XADC will be lower than the actual input voltage.

The next picture is a copy of [Figure 2-5](https://docs.amd.com/r/qOeib0vlzXa1isUAfuFzOQ/Jknshmzrw3DvMZgWJO73KQ?section=XREF_26771_X_Ref_Target) from Zynq-7000 XADC User Guide [UG480](https://docs.amd.com/r/en-US/ug480_7Series_XADC), chapter "Analog Input Description" (page 22 of the PDF version of the UG480).

<img src="pictures\UG480_fig_2-5.png" title=""  width="650">

We see in the picture that in the unipolar mode, the current to the capacitor goes through two internal resistances R<sub>MUX</sub>. In bipolar mode, two capacitors are used, and the current into them goes through a single internal resistance R<sub>MUX</sub>.  
R<sub>MUX</sub> is the resistance of the analog multiplexer circuit inside the Zynq XADC. Please note that the value of R<sub>MUX</sub> for a dedicated analog input is different from the R<sub>MUX</sub> of the auxiliary inputs.  

Xilinx is giving us [Equation 2-2](https://docs.amd.com/r/qOeib0vlzXa1isUAfuFzOQ/Jknshmzrw3DvMZgWJO73KQ?section=XREF_62490_Equation2_2) in the [UG480](https://docs.amd.com/r/en-US/ug480_7Series_XADC/Analog-Inputs) for calculating minimum acquisition time in unipolar mode:
```math
t_{ACQ} = 9 \times ( R_{MUX} + R_{MUX} ) \times C_{SAMPLE}
```
Factor 9 is the so-called time constant. It is derived from $`TC=\ln(2^{N+m})`$ , where $`N=12`$ for a 12-bit system and $m=1$ additional resolution bit.

R<sub>MUX</sub> for an auxiliary input is 10 kΩ.  
C<sub>SAMPLE</sub> is specified by Xilinx as 3 pF.

Therefore, we calculate the minimum acquisition time t<sub>ACQ</sub> for an unipolar auxiliary input as follows:
```math
t_{ACQ} = 9 \times ( 10000 + 10000 ) \times 3 \times 10^{-12} = 540 \mskip3mu ns
```
&nbsp;

For minimum acquisition time in bipolar mode, Xilinx is giving the [Equation 2-1](https://docs.amd.com/r/qOeib0vlzXa1isUAfuFzOQ/Jknshmzrw3DvMZgWJO73KQ?section=XREF_35025_Equation2_1) in the [UG480](https://docs.amd.com/r/en-US/ug480_7Series_XADC/Analog-Inputs):

```math
t_{ACQ} = 9 \times R_{MUX} \times C_{SAMPLE}
```
For a dedicated analog input, R<sub>MUX</sub> equals 100 Ω. This gives us the following value of the minimum acquisition time of a bipolar dedicated input:
```math
t_{ACQ} = 9 \times 100 \times 3 \times 10^{-12} = 2.7 \mskip3mu ns
```
> [!IMPORTANT]
>
> The calculation of the acquisition times we did above is valid only for an ideal case when the only resistance present in the circuit is the resistance of the Zynq XADC's internal analog multiplexer.
>
> In most cases, this is not true because you need an [anti-aliasing filter](https://en.wikipedia.org/wiki/Anti-aliasing_filter), i.e., a low pass filter, which will eliminate frequencies higher than the [Nyquist frequency](https://en.wikipedia.org/wiki/Nyquist_frequency) in the input signal.

But how does the calculated acquisition time translate to the possible sampling rate?  
The hint for that can be found in the Driving the Xilinx Analog-to-Digital Converter Application Note [XAPP795](https://docs.amd.com/v/u/en-US/xapp795-driving-xadc). This note explains on page 4 that XADC is able to acquire the next sample (i.e., charge the internal capacitor) during the conversion of the current sample. At least 75% of the overall sample time is available for the acquisition.

When the XADC runs at a maximum sample rate of 1 Msps, the duration of a single sample is 1 μs, and 75% of that is 750 ns.  
[Equation 2-1](https://docs.amd.com/r/qOeib0vlzXa1isUAfuFzOQ/Jknshmzrw3DvMZgWJO73KQ?section=XREF_35025_Equation2_1) and [Equation 2-2](https://docs.amd.com/r/qOeib0vlzXa1isUAfuFzOQ/Jknshmzrw3DvMZgWJO73KQ?section=XREF_62490_Equation2_2) gave us an acquisition time of unipolar auxiliary input of 540 ns and bipolar dedicated input of 2.7 ns. This is well below the 750 ns. So there seems to be no problem until you add an [anti-aliasing filter](https://en.wikipedia.org/wiki/Anti-aliasing_filter) (AAF) on the input. The next chapter explains how AAF changes the acquisition time requirements.


### Settling time of unipolar input AAF of Cora Z7

In this chapter, I will discuss the unipolar analog input circuitry of the Digilent [Cora Z7](https://digilent.com/shop/cora-z7-zynq-7000-single-core-for-arm-fpga-soc-development/) development board. Nevertheless, the very same principles apply to any Zynq-7000 board with a passive [anti-aliasing filter](https://en.wikipedia.org/wiki/Anti-aliasing_filter) (AAF) on analog inputs.

The pins labeled A0-A5 on the Cora Z7 board can be used as digital I/O pins or analog input pins for the auxiliary channels. How the given pin is used (digital vs. analog) is controlled by a constraint specified in the HW design.  
The following table lists the assignment of Cora Z7 pins to the XADC auxiliary channels.

| Cora Z7 pin | Associated XADC channel |
| ----------- | ----------------------- |
| A0          | VAUX[1]                 |
| A1          | VAUX[9]                 |
| A2          | VAUX[6]                 |
| A3          | VAUX[15]                |
| A4          | VAUX[5]                 |
| A5          | VAUX[13]                |

The following picture is a copy of Figure 13.2.1 from the Cora Z7 [Reference Manual](https://digilent.com/reference/programmable-logic/cora-z7/reference-manual#shield_analog_io). It depicts the circuit used for pins A0-A5.

<img src="pictures\cora-analog-single-ended.png" title=""  width="550">

We see that the analog input circuit on Cora Z7 consists of a voltage divider and a low-pass anti-aliasing filter (AAF).   
The voltage divider allows voltage up to 3.3 V to be connected to pins A0-A5. The voltage is reduced to the 1.0 V limit of the XADC.

- To be precise, an input voltage of 3.3 V  is reduced to 0.994 V. The exact value may vary depending on how much the resistors on your particular board deviate within the tolerances. 

The analog inputs A0-A5 act as single-ended inputs because negative signals VAUXN[] are tied to the board's ground.  
The low-pass filter formed by the circuit has a cut-off frequency of 134 kHz (I simulated the circuit's frequency response in [LTspice](https://www.analog.com/en/resources/design-tools-and-calculators/ltspice-simulator.html)).

This circuit on Cora Z7 is basically the same as the one discussed in the Application Guidelines chapter [External Analog Inputs](https://docs.amd.com/r/en-US/ug480_7Series_XADC/External-Analog-Inputs) in UG480.  
The AAF contains a 1 nF capacitor, which is orders of magnitude larger capacitance than the 3 pF sampling capacitor inside the XADC. Therefore, we can ignore the XADC sampling capacitor when determining the acquisition time.

We need to determine the AAF circuit's settling time, which is the acquisition time needed for the XADC to acquire the input signal with the desired precision.  
When the input analog signal changes to a new value, the settling time is the time it takes the circuit to settle to this new value on its output. Meaning, settle close enough for acquiring the new value with 12-bit precision.

We can use a slightly modified [Equation 6-1](https://docs.amd.com/r/qOeib0vlzXa1isUAfuFzOQ/8erAzNpWEDQ8zWWH_EdtFg?section=XREF_11532_Equation2_5) from [UG480](https://docs.amd.com/r/en-US/ug480_7Series_XADC/External-Analog-Inputs) to adapt it to the resistances of Cora Z7 analog input:

```math
t_{settling} = \ln(2^{12+1}) \times ( {{2320 \times 1000} \over {2300 + 1000 }} + 140 + 845) \times 1 \times 10^{-9} = 15.1725 \mskip3mu \mu s
```
The term $`\ln(2^{12+1})`$ is the number of time constants needed for 12-bit resolution.

The term $`{2320 \times 1000} \over {2300 + 1000 }`$ is the output impedance of the voltage divider.

The terms 140 and 845 are the resistors on the analog inputs.

The factor $`1 \times 10^{-9}`$ is capacitance of AAF's capacitor.

Further details on how the equation was constructed can be found in the Application Note [XAPP795](https://docs.amd.com/v/u/en-US/xapp795-driving-xadc).

In theory, the settling time of 15.1725 μs allows for a 65.909 kHz sampling rate. In the later chapter, we will explore how the circuit behaves in practice.

> [!IMPORTANT]
>
> Please note that any additional resistance of circuitry you connect to the Cora Z7's pins A0-A5 can further increase the settling time needed.  
> In order to achieve a reliable measurement, the voltage source connected to pins A0-A5 must act as having low internal resistance.

### Settling time of bipolar input of Cora Z7

Equation 2-1 from UG480  [Equation 2-1](https://docs.amd.com/r/qOeib0vlzXa1isUAfuFzOQ/Jknshmzrw3DvMZgWJO73KQ?section=XREF_35025_Equation2_1) in the [UG480](https://docs.amd.com/r/en-US/ug480_7Series_XADC/Analog-Inputs) for acquisition time in unipolar mode:
```math
t_{ACQ} = 9 \times R_{MUX} \times C_{SAMPLE}
```
On Cora Z7, we need to take into account the bipolar input circuitry for dedicated V<sub>P</sub> /V<sub>N</sub> input on the board, as depicted in Figure 13.2.3 from the Cora Z7 [Reference Manual](https://digilent.com/reference/programmable-logic/cora-z7/reference-manual#shield_analog_io):

<img src="pictures\cora-analog-dedicated.png"  width="400">

The R<sub>MUX</sub>for a dedicated analog input is 100 Ω.  
In addition to R<sub>MUX</sub>, we must include the 140 Ω resistor in the signal path on the Cora&nbsp;Z7 board  
C<sub>SAMPLE</sub> is 3 pF.

We now calculate the needed acquisition time for V<sub>P</sub> /V<sub>N</sub> as follows:
```math
t_{ACQ} = 9 \times ( 100 + 140) \times 3 \times 10^{-12} = 6.5\mskip3muns
```
**TODO:**  
This would allow us to use a sampling rate of 1 Msps because, with the ADCCLK frequency of 26&nbsp;MHz and 4 ADCCLKs allowed for the acquisition, we get 150&nbsp;ns acquisition time, which is more than enough.

However, in our design, we are limited to using an ADCCLK frequency of 23.84&nbsp;MHz (95.363&nbsp;MHz divided by 4) because we must use an XADC clock of 95.363&nbsp;MHz to achieve the 629&nbsp;ns acquisition time for the unipolar input as described in the previous chapter.  
Therefore, the resulting sampling rate will be 917 ksps (a single conversion cycle will take 26 ADCCLKs).

## Calibration

TODO

From [UG480](https://docs.amd.com/r/en-US/ug480_7Series_XADC/Status-Registers), I understood that the calibration coefficients are calculated by initiating a conversion on channel 8. 

I'm using a Digilent [Cora Z7](https://digilent.com/reference/programmable-logic/cora-z7/reference-manual) board, which doesn't provide external voltage references. VREFP and VREFN are connected to ADCGND on this board. Therefore, the internal FPGA voltage references are utilized for calibration.

From [this post](https://support.xilinx.com/s/article/53586?language=en_US), I know that the Gain Calibration Coefficient will always be 0x7F with internal references.

When the default mode is enabled, both ADCs are calibrated. The XADC also operates in default mode after initial power-up and during FPGA configuration. See the [UG480](https://docs.amd.com/r/en-US/ug480_7Series_XADC/Sequencer-Modes), chapter Default Mode, page 48.

So unless you want to recalibrate the XADC during runtime, you never need to care about calibration by initiating a conversion on channel 8. It's done during FPGA configuration, which happens also when you re-run the PS code. 

## DMA

TODO

## Measurements

TODO

Connected precise 2.5 V voltage reference to A0 (2.50026 V).

Without averaging, the mean value over 10,000 samples was 2.492 V. 

Beware of the precision of the resistors in the Cora Z7 voltage dividers.

<img src="pictures\test.png"  >

bla bla bla

<img src="pictures\test2.png">

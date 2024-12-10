[![License](https://img.shields.io/badge/License-BSD_2--Clause-orange.svg)](https://opensource.org/licenses/BSD-2-Clause)

# Tutorial: Xilinx Zynq XADC using DMA and network streaming

This tutorial shows how to do an HW design and code a SW application to make use of AMD Xilinx Zynq-7000 [XADC](https://www.xilinx.com/products/technology/analog-mixed-signal.html). We will also see how to use the [DMA](https://www.xilinx.com/products/intellectual-property/axi_dma.html) to transfer data from the XADC into Zynq CPU's memory and stream data to a remote PC over the network.

In this tutorial, I'm using the Digilent board [Cora Z7-07S](https://digilent.com/reference/programmable-logic/cora-z7/start). However, all the principles described here can be used on any other Zynq-7000 board. I will highlight aspects specific to Cora Z7 in the text.  
The Cora Z7 is a suitable board for testing the Zynq XADC because it has analog inputs that are usable in a practical way.

I'm using Vivado 2024.1.1 and Vitis Classic 2024.1.1 in this tutorial. Nevertheless, the same steps also work in Vivado 2023.1, Vitis 2023.1, and Vitis Classic 2023.2.  
I also provide instructions on how to build the demo application in Vitis 2024.1.1 (a.k.a. Vitis Unified 2024).

## A short introduction to Zynq-7000 XADC

Before we dive into drawing a block diagram in Vivado and writing code in Vitis, we need to understand the basics of the XDAC and be aware of important aspects and limitations. Let me cover this in the first chapters of this tutorial.

### What is XADC

The XADC is a feature of an analog-to-digital converter integrated on selected Xilinx FPGA chips, including Zynq-7000. This ADC has two basic capabilities:

1.  The System Monitor (SYSMON) reads the Zynq chip temperature and voltages of various Zynq power rails.
1.  The XADC reads voltages from external inputs, which are called channels.

In this tutorial, we will focus solely on XADC. But please don't get confused. Xilinx library functions for controlling XADC are defined in [xsysmon.h](https://github.com/Xilinx/embeddedsw/blob/master/XilinxProcessorIPLib/drivers/sysmon/src/xsysmon.h).

XADC can read one external input (channel) at a time and provides a means for switching between channels.  
The Zynq-7000 XADC has one dedicated analog input channel called V<sub>P</sub>/V<sub>N</sub> and 16 so-called auxiliary channels named VAUX[0..15].
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
- However, both V<sub>P</sub> and V<sub>N</sub> must always be within a range from 0 V to 1.0 V with respect to GNDADC.

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

Zynq-7000 XADC is a 12-bit ADC. However, the XADC [status registers](https://docs.amd.com/r/en-US/ug480_7Series_XADC/Status-Registers) storing the conversion result are 16-bit, and the function [XSysMon_GetAdcData()](https://github.com/Xilinx/embeddedsw/blob/5688620af40994a0012ef5db3c873e1de3f20e9f/XilinxProcessorIPLib/drivers/sysmon/src/xsysmon.c#L331) also returns a 16-bit value.  
In general, the 12 most significant bits of the register are the converted XADC sample. Do ignore the 4 least significant bits.

It is possible to configure the XADC to do an averaging of consecutive 16, 64, or 256 samples (see function [XSysMon_SetAvg()](https://github.com/Xilinx/embeddedsw/blob/5688620af40994a0012ef5db3c873e1de3f20e9f/XilinxProcessorIPLib/drivers/sysmon/src/xsysmon.c#L488)). I.e., to do the oversampling. The 4 least significant bits are then used to represent the averaged value with enhanced precision, i.e., the whole 16 bits of a status register can be used.  
Obviously, letting the XADC do the averaging makes sense for slowly changing input signals where noise is expected to be removed by the averaging. I show a practical example of the effect of averaging in the [Measurement precision—a practical example](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP?#measurement-precisiona-practical-example) chapter of this tutorial.

### Clocking, sampling rate, and bandwidth

The XADC is driven by the input clock DCLK.  
When the XADC Wizard is configured to have an AIX4Lite interface (which is what we will do), the DCLK is driven by the s_axi_aclk clock of the AXI interface.

The ADC circuitry within the XADC is driven by the clock ADCCLK, which is derived from DCLK by a configurable ratio divider. The minimum possible divider ratio is 2, and the maximum ratio is 255.  
The divider ratio can be configured dynamically by calling the function [XSysMon_SetAdcClkDivisor()](https://github.com/Xilinx/embeddedsw/blob/5688620af40994a0012ef5db3c873e1de3f20e9f/XilinxProcessorIPLib/drivers/sysmon/src/xsysmon.c#L1089).

In the default XADC setup of Continuous Sampling mode, 26 ADCCLK cycles are required to acquire an analog signal and perform a conversion.  
The 26 ADCCLK cycle period can be extended to 32 cycles by configuration. See the boolean parameter IncreaseAcqCycles of the [XSysMon_SetSingleChParams()](https://github.com/Xilinx/embeddedsw/blob/5688620af40994a0012ef5db3c873e1de3f20e9f/XilinxProcessorIPLib/drivers/sysmon/src/xsysmon.c#L586). The parameter controls the duration of the so-called settling period (this is what [UG480](https://docs.amd.com/r/en-US/ug480_7Series_XADC/Continuous-Sampling) calls it), which is 4 ADCCLK cycles by default and can be extended to 10 cycles.

> [!NOTE]
>
> Don't be confused by the different vocabulary used in Xillinx documents regarding this 4 or 10 ADCCLK period within the XADC's acquisition and conversion cycle.  
> [UG480](https://docs.amd.com/r/en-US/ug480_7Series_XADC/Continuous-Sampling) calls it a "settling period," but it is called "Acquisition Time" in the UI of XADC Wizard IP in Vivado and comments in the [xsysmon.c](https://github.com/Xilinx/embeddedsw/blob/master/XilinxProcessorIPLib/drivers/sysmon/src/xsysmon.c).  
> However, [Figure 5-1](https://docs.amd.com/r/qOeib0vlzXa1isUAfuFzOQ/4MmXaAzpjJTjs~BpjCs4Rw?section=XREF_95899_X_Ref_Target) in the [UG480](https://docs.amd.com/r/en-US/ug480_7Series_XADC/Continuous-Sampling) clearly shows that the acquisition time is longer than 4 or 10 ADCCLK clocks.
>
> The "settling period" is probably the best term for the reasons I explain later in this article.

The XADC maximum sampling rate is 1&nbsp;Msps.  
This is achieved by having 104 MHz DCLK and the divider ratio set to 4. This results in the highest possible ADCCLK frequency of 26 MHz. Using 26 ADCCLK cycles for single conversion then gives 1 Msps.

To achieve other (i.e., lower) sampling rates, you need to set a suitable DCLK clock frequency in the HW design and a suitable ADCCLK clock divider ratio, so the quotient of frequency and the ratio is 26 times the desired sampling rate.  
E.g., to have a sampling rate of 100 ksps, you can set the DCLK to 101.4&nbsp;MHz and the divider ratio to 39.&nbsp;&nbsp;&nbsp;$`\frac{101400}{39} = 2600`$&nbsp;&nbsp;&nbsp;&nbsp;$`\frac{2600}{26} = 100 \mskip3mu ksps`$

> [!IMPORTANT]
>
> XADC can run at 1 Msps, but that doesn't mean it's wise to run it at 1&nbsp;Msps in all circumstances. It very much depends on the circuitry before the XADC and the characteristics of the input signal.
> I will explore this topic in the following chapters.

> [!IMPORTANT]
>
> The guaranteed analog bandwidth of auxiliary channels is only 250 kHz.
> The Zynq-7000 SoC [Data Sheet DS187](https://docs.amd.com/v/u/en-US/ds187-XC7Z010-XC7Z020-Data-Sheet) states on page 68 that the Auxiliary Channel Full Resolution Bandwidth is 250 kHz.

You can sample an auxiliary channel at 1 Msps. However, signals with a frequency higher than 250 kHz are not guaranteed to be precisely recorded. This seems to be an analog bandwidth limitation of Zynq-7000 circuits related to auxiliary channels.

The data sheet doesn't mention the bandwidth of the dedicated analog input channel V<sub>P</sub>/V<sub>N</sub>. We can assume it is at least 500 kHz (i.e., the [Nyquist frequency](https://en.wikipedia.org/wiki/Nyquist_frequency) for a 1 Msps ADC).

## Acquisition and settling time—the theory

In this chapter, I will summarize what Zynq-7000 XADC User Guide [UG480](https://docs.amd.com/r/en-US/ug480_7Series_XADC) and the Application Note [XAPP795](https://docs.amd.com/v/u/en-US/xapp795-driving-xadc) tell us about the acquisition and settling time.

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
> In the vast majority of cases, this is not true because you need an [anti-aliasing filter](https://en.wikipedia.org/wiki/Anti-aliasing_filter), i.e., a low pass filter, which will eliminate frequencies higher than the [Nyquist frequency](https://en.wikipedia.org/wiki/Nyquist_frequency) in the input signal.

But how does the calculated acquisition time translate to the possible sampling rate?  
The hint for that can be found in the Driving the Xilinx Analog-to-Digital Converter Application Note [XAPP795](https://docs.amd.com/v/u/en-US/xapp795-driving-xadc). This note explains on page 4 that XADC is able to acquire the next sample (i.e., charge the internal capacitor) during the conversion of the current sample. At least 75% of the overall sample time is available for the acquisition.

When the XADC runs at a maximum sample rate of 1 Msps, the duration of a single sample is 1 μs, and 75% of that is 750 ns.  
[Equation 2-1](https://docs.amd.com/r/qOeib0vlzXa1isUAfuFzOQ/Jknshmzrw3DvMZgWJO73KQ?section=XREF_35025_Equation2_1) and [Equation 2-2](https://docs.amd.com/r/qOeib0vlzXa1isUAfuFzOQ/Jknshmzrw3DvMZgWJO73KQ?section=XREF_62490_Equation2_2) gave us an acquisition time of unipolar auxiliary input of 540 ns and bipolar dedicated input of 2.7 ns. This is well below the 750 ns. So there seems to be no problem, right? This is valid only until you add an [anti-aliasing filter](https://en.wikipedia.org/wiki/Anti-aliasing_filter) (AAF) to the input. The next chapter explains how AAF changes the acquisition time requirements.


### Settling time of auxiliary unipolar channel AAF of Cora Z7

In this chapter, I will discuss the unipolar analog input circuitry of the Digilent [Cora Z7](https://digilent.com/reference/programmable-logic/cora-z7/start) development board. Nevertheless, the very same principles apply to any Zynq-7000 board with a passive [anti-aliasing filter](https://en.wikipedia.org/wiki/Anti-aliasing_filter) (AAF) on analog inputs.

The pins labeled A0-A5 on the Cora Z7 board can be used as digital I/O pins or analog input pins for the auxiliary single-ended channels. How the given pin is used (digital vs. analog) is controlled by a constraint specified in the HW design.  
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

We see that the analog input circuit on Cora Z7 consists of a voltage divider and a low-pass [anti-aliasing filter](https://en.wikipedia.org/wiki/Anti-aliasing_filter) (AAF).   
The voltage divider allows voltage up to 3.3 V to be connected to pins A0-A5. The voltage is reduced to the 1.0 V limit of the XADC.

- To be precise, an input voltage of 3.3 V  is reduced to 0.994 V. The exact value may vary depending on how much the resistors on your particular board deviate within the tolerances. 

The analog inputs A0-A5 act as single-ended inputs because negative signals VAUXN[] are tied to the board's ground.  
The low-pass filter formed by the circuit has a cut-off frequency of 94.6 kHz.

This circuit on Cora Z7 is basically the same as the one discussed in the Application Guidelines chapter [External Analog Inputs](https://docs.amd.com/r/en-US/ug480_7Series_XADC/External-Analog-Inputs) in UG480.  
The AAF contains a 1 nF capacitor, which is orders of magnitude larger capacitance than the 3 pF sampling capacitor inside the XADC. Therefore, we can ignore the XADC sampling capacitor when determining the acquisition time.

We need to determine the AAF circuit's settling time, which is the acquisition time needed for the XADC to acquire the input signal with the desired precision.  
When the input analog signal changes to a new value, the settling time is the time it takes the circuit to settle to this new value on its output. Meaning, settle close enough for acquiring the new signal value with 12-bit precision.

We can use a slightly modified [Equation 6-1](https://docs.amd.com/r/qOeib0vlzXa1isUAfuFzOQ/8erAzNpWEDQ8zWWH_EdtFg?section=XREF_11532_Equation2_5) from [UG480](https://docs.amd.com/r/en-US/ug480_7Series_XADC/External-Analog-Inputs) to adapt it to the resistances of Cora Z7 auxiliary analog input:

```math
t_{settling} = \ln(2^{12+1}) \times ( {{2320 \times 1000} \over {2300 + 1000}} + 140 + 845) \times 1 \times 10^{-9} = 15.1725 \mskip3mu \mu s
```
The term $`\ln(2^{12+1})`$ is the number of time constants needed for 12-bit resolution.

The term $`{2320 \times 1000} \over {2300 + 1000 }`$ is the output impedance of the voltage divider.

The terms 140 and 845 are the resistors on the analog inputs.

The factor $`1 \times 10^{-9}`$ is the capacitance of the AAF's capacitor.

Further details on how the equation was constructed can be found in the Application Note [XAPP795](https://docs.amd.com/v/u/en-US/xapp795-driving-xadc).

One may be tempted to think that a settling time of 15.1725 μs allows for a sampling rate with a 15.1725 μs period or 65.909 kHz. However, it is not that simple. In a [later chapter](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/tree/main?#the-behavior-of-unipolar-auxiliary-channel-aaf-of-cora-z7), we will explore how the circuit behaves in practice and to which use cases the 65.909 kHz sampling rate may apply well.

One "controversy" is already apparent: According to the [Nyquist theorem](https://www.techtarget.com/whatis/definition/Nyquist-Theorem), the 65.9 kHz sampling rate allows correct sampling of an input signal of frequency up to 33 kHz. However, the cut-off frequency of the AAF on the Cora Z7 unipolar input is 94.6 kHz. So, by running the XADC at 65.9 ksps, we risk that an imprecise digitalization of the so-called [alias](https://en.wikipedia.org/wiki/Aliasing) will happen if frequencies above 33 kHz are present in the input signal. We would need an AAF with a cutoff frequency of 33 kHz. However, such an AAF would have an even longer settling time.  
There is no universal solution for this issue. How you compromise between sampling rate and settling time depends on the characteristics of the input signal and on what you want to achieve by digitizing it.

> [!IMPORTANT]
>
> Please note that any additional resistance of circuitry you connect to the Cora Z7's pins A0-A5 can further increase the settling time needed.  
> To achieve a reliable measurement, the voltage source connected to pins A0-A5 should act as having low internal resistance.

### Settling time of dedicated channel V<sub>P</sub>/V<sub>N</sub> AAF of Cora Z7

The following picture is a copy of Figure 13.2.3 from the Cora Z7 [Reference Manual](https://digilent.com/reference/programmable-logic/cora-z7/reference-manual#shield_analog_io). It depicts the circuit used for the dedicated analog input channel V<sub>P</sub>/V<sub>N</sub> (the pins are labeled V_P and V_N on the Cora Z7 board):

<img src="pictures\cora-analog-dedicated.png"  width="400">

The Cora Z7 V<sub>P</sub>/V<sub>N</sub> channel can be used in both bipolar and unipolar modes.

- Note: The Cora Z7 also provides pins labeled A6-A11, which can be used as differential auxiliary channels. See the Cora Z7 [Reference Manual](https://digilent.com/reference/programmable-logic/cora-z7/reference-manual#shield_analog_io) for details.

> [!CAUTION]
>
> The dedicated analog input channel on Cora Z7 is less protected than auxiliary channels A0-A5. There is no voltage divider. Therefore, both V<sub>P</sub> and V<sub>N</sub> must always be within a range from 0 V to 1.0 V with respect to the board's GND. Also, the differential V<sub>P</sub> &minus; V<sub>N</sub> must be within the range of ±0.5V.

The capacitor and the resistors form a low-pass filter with a cutoff frequency of 568.7 kHz (which is relatively close to the 500 kHz [Nyquist frequency](https://en.wikipedia.org/wiki/Nyquist_frequency) of the XADC running at 1 Msps).

We can calculate the settling time of this circuit as follows:

```math
t_{settling} = \ln(2^{12+1}) \times ( 140 + 140 ) \times 1 \times 10^{-9} = 2.52 \mskip3mu \mu s
```

In a [later chapter](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/tree/main?#the-behavior-of-dedicated-channel-vpvn-aaf-of-cora-z7), I will show a practical example of how the circuit behaves.

## Acquisition and settling time—the practice

### The behavior of unipolar auxiliary channel AAF of Cora Z7

Let's see what the low-pass [AAF](https://en.wikipedia.org/wiki/Anti-aliasing_filter) does to a signal.  
I simulated a square wave signal passing through the Cora Z7 unipolar input AAF in [LTspice](https://www.analog.com/en/resources/design-tools-and-calculators/ltspice-simulator.html). One "step" of the signal has a duration of 15.1725 μs, i.e., it is as long as the circuit's settling time we calculated in the [previous chapter](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP?#settling-time-of-auxiliary-unipolar-channel-aaf-of-cora-z7). The result of the simulation is in the following figure.

<img src="pictures\Cora_Z7_stair_signal_simulation.png">

A square wave signal is actually a high-frequency signal (see an explanation for example [here](https://www.allaboutcircuits.com/textbook/alternating-current/chpt-7/square-wave-signals/)). The AAF on Cora Z7 unipolar input attenuates frequencies above 94.6 kHz. This manifests as the "rounding of edges" of the square wave signal.

We see that the simulation is consistent with the settling time we calculated in the previous chapter. It takes exactly 15.17 μs for the output signal to reach a new level after the input changes.  
Please note that the settling time does not depend on the magnitude of the input signal change. In this simulation, the change in each step is 0.5 V. However, if I changed the steps to only 0.05 V, it would still take the output signal 15.17 μs to settle on the new level. The shape of the chart would be the same.

So, the simulation matches the theory. But does it really work in practice?  
It does. :smiley: I reproduced the scenario on a physical Cora Z7 board and measured the signal by the XADC configured to the sample rate of 1 Msps using the [software app](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/tree/main/sources/XADC_tutorial_app) shared in this repository. See the following figure.

<img src="pictures\Cora_Z7_stair_signal_reading.png" alt="Zynq-7000 XADC square wave signal digitization">

As you can see, the real-life measurement on the physical HW matches the simulation pretty well.

What is happening here? The XADC auxiliary input, which has an acquisition time of 540 ns, precisely digitized a signal <ins>after</ins> AAF, which has a settling time of 15.17 μs. The XADC is, of course, unable to "see" the actual input signal <ins>before</ins> the AAF.

Let's show in the following figure what would happen if I mindlessly interpreted the circuit's settling time of 15.17 μs as a period of sampling rate (i.e., 65.9 kHz sampling frequency).

<img src="pictures\Cora_Z7_stair_signal_reading_simulation.png">

We see that at 65.9 ksps, the digitized signal looks nothing like the input signal. This is not a helpful result.  
At 1 Msps, we could apply some digital processing, e.g., identify local maxima and minima of the signal and thus get some understanding of the characteristics of the square wave signal on the input. That is not a possibility at 65.9 ksps.

We saw that a square wave signal is a challenge for the XADC. What about something more reasonable, for example, an 8 kHz sine signal? The following figure shows the effect of Cora Z7 auxiliary channel AAF on such a signal.

<img src="pictures\Cora_Z7_sine_signal_simulation.png">

8 kHz is very well below the 94.6 kHz cutoff frequency of the AAF, so we see only a very minor impact of the filter on the output signal. There is very slight attenuation and a small phase shift (i.e., delay of the output signal as compared to the input).

The next figure shows an example of what the 65.9 ksps digitization of the 8 kHz signal may look like (i.e., digitization with the sample period equal to the circuit's settling time).

<img src="pictures\Cora_Z7_sine_signal_reading_simulation.png">

At 65.9 ksps, the shape of the signal is generally well-recorded. You may miss the exact local maxima and minima, though. If you needed to calculate the [RMS](https://en.wikipedia.org/wiki/Root_mean_square) of the input signal, it would be better to use a higher sample rate, ideally 1 Msps, to get a more precise approximation of the signal curve and, thus, a more accurate RMS. 

#### The bottom line

The settling time, which you can calculate using formulas in the Application Note [XAPP795](https://docs.amd.com/v/u/en-US/xapp795-driving-xadc) and the User Guide [UG480](https://docs.amd.com/r/en-US/ug480_7Series_XADC/External-Analog-Inputs), tells you how long it takes for the output signal to settle to a new value (for the 12-bit digitization precision) when the input signal undergoes a <ins>step change</ins> to a new value. Nothing else!

Knowing the settling time is crucial in cases when the input voltage undergoes sudden changes, such as when a [multiplexer](https://en.wikipedia.org/wiki/Multiplexer) is used. When the input signal is switched to a new source, the XADC needs to acquire the signal for at least the settling time to produce the correct sample value.

When you measure slowly changing signals (e.g., a voltage from a temperature sensor) and are not very concerned about noise in the input signal, it's probably enough to sample the input with the period equal to the settling time.

I think that in all other cases, the proper digitization setup depends on the circumstances.  You need to understand your objective and your input signal. And you definitely have to do a lot of testing.  
There will be cases when it's beneficial to sample a low-frequency signal with a high XADC sample rate to use some kind of averaging or other digital signal processing algorithm, e.g., to reduce the noise component of the input signal.

### The behavior of dedicated channel V<sub>P</sub>/V<sub>N</sub> AAF of Cora Z7

For completeness, let's look shortly also at a practical example using the dedicated analog input channel V<sub>P</sub>/V<sub>N</sub>.

The V<sub>P</sub>/V<sub>N</sub> channel's [AAF](https://en.wikipedia.org/wiki/Anti-aliasing_filter) cutoff frequency is pretty high at 568.7 kHz. Let's see in the following figure what happens when we feed the inputs with two 50 kHz sine waves of opposing phases and measure the output differentially.

<img src="pictures\Cora_Z7_diff_signal_simulation.png">

There are no surprises. The 50 kHz signal is negligibly attenuated by the AAF, and there is a very slight phase shift in the output.

I reproduced the scenario on a physical Cora Z7 board and measured the signal by the XADC configured to the sample rate of 1 Msps using the [software app](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/tree/main/sources/XADC_tutorial_app) shared in this repository. The resulting measurement shown in the following figure is exactly as expected. The XADC captures a 50 kHz differential signal with good precision.

<img src="pictures\Cora_Z7_diff_signal_reading.png" alt="Zynq-7000 XADC differential signal digitization">

## Calibration and precision

### XADC autocalibration

The Zynq-7000 XADC is equipped with autocalibration capability, which automatically sets Offset and Gain Calibration Coefficients in the XADC registers. XADC may or may not apply these coefficients to the measurements it performs depending on the configuration, which is set by [XSysMon_SetCalibEnables()](https://github.com/Xilinx/embeddedsw/blob/5688620af40994a0012ef5db3c873e1de3f20e9f/XilinxProcessorIPLib/drivers/sysmon/src/xsysmon.c#L828).

The XADC performs the calibration automatically at startup. This is because after powering up and during FPGA configuration (i.e., when the bitstream is written into the FPGA or when the PS code is rerun), the XADC starts its operation in so-called default mode. In the default mode, the XADC calibration is done automatically. See details in the [UG480](https://docs.amd.com/r/en-US/ug480_7Series_XADC/Sequencer-Modes), chapter Default Mode.  
Even if you change the XADC mode afterward, the calibration done during the default mode will remain valid.

If you need to repeat the calibration later during the runtime (e.g., to make a correction for the temperature changes of the board), you can do that by initiating a conversion on channel 8. This is a special channel that is not connected to any analog input.

The XADC calibrates using a 1.25 V voltage reference external to the Zynq-7000 chip (if present in the board's circuit) or an internal voltage reference within the Zynq-7000 chip.  
Using an external voltage reference allows the board designer to achieve higher precision of XADC measurements.

> [!IMPORTANT]
>
> There is a "catch" regarding gain calibration:  
> It shouldn't be used when the XADC calibration is done by internal voltage reference of the Zynq-7000 chip (see details explained [here](https://adaptivesupport.amd.com/s/article/53586?language=en_US)).
>
> This is the case for the Digilent [Cora Z7](https://digilent.com/reference/programmable-logic/cora-z7/start) board, which I use in this tutorial.  
> Zynq-7000 reference input pins VREFP and VREFN are connected to ADCGND on this board. Therefore, the internal voltage reference is utilized for calibration.
>
> You need to correctly handle this in the PS code, as I explain in the next paragraphs.

When you know exactly what Zynq-7000 board your code will run on, you can hard-code the configuration, understanding whether the external or internal reference is used. See the schematics of your board to check what is connected to Zynq-7000 [reference input pins VREFP and VREFN](https://docs.amd.com/r/en-US/ug480_7Series_XADC/Reference-Inputs-VREFP-and-VREFN). The internal voltage reference is used if they are connected to the ADCGND (i.e., the ground of XADC circuitry).

Use the following call (after you initialize the XADC instance) when the internal voltage reference is used to enable only the Offset Calibration Coefficient for XADC and Zynq power supply measurements:

`XSysMon_SetCalibEnables(&XADCInstance, XSM_CFR1_CAL_ADC_OFFSET_MASK | XSM_CFR1_CAL_PS_OFFSET_MASK);`

When the  Zynq-7000 board you are using provides an external voltage reference, then use the following code to enable both Gain and Offset Calibration Coefficients for XADC and Zynq power supply measurements:

`XSysMon_SetCalibEnables(&XADCInstance, XSM_CFR1_CAL_ADC_GAIN_OFFSET_MASK | XSM_CFR1_CAL_PS_GAIN_OFFSET_MASK);`

When the Zynq-7000 internal voltage reference is used for the calibration, the calibration of XADC gain is actually not done (only the XADC offset is calibrated). As explained [here](https://adaptivesupport.amd.com/s/article/53586?language=en_US), the value of the Gain Calibration Coefficient is set to 0x007F in such a case. 0x007F represents the maximum Gain Coefficient of 6.3%. Obviously, letting XADC apply this maximal Gain Coefficient would reduce the precision of digitalization. That is why we need to pay attention to what kind of voltage reference our Zynq-7000 board uses.

We can write an XADC calibration setup code, which is portable between different Zynq-7000 boards, by checking the value of the Gain Calibration Coefficient. 

```c
//Read value of the Gain Calibration Coefficient from the XADC register
u16 GainCoeff = XSysMon_GetCalibCoefficient( &XADCInstance, XSM_CALIB_GAIN_ERROR_COEFF );

u16 CalibrationEnables;
if( GainCoeff != 0x007F ) // True when external voltage reference is used
    // Use both Offset and Gain Coefficients
    CalibrationEnables = XSM_CFR1_CAL_ADC_GAIN_OFFSET_MASK | XSM_CFR1_CAL_PS_GAIN_OFFSET_MASK;
else
    // Use only Offset Coefficient
    CalibrationEnables = XSM_CFR1_CAL_ADC_OFFSET_MASK | XSM_CFR1_CAL_PS_OFFSET_MASK;

//Set the correct use of the calibration coefficients
XSysMon_SetCalibEnables( &XADCInstance, CalibrationEnables );
```

### Measurement precision—a practical example

How precise can the XADC measurement be? Judging by my specimen of the [Cora Z7](https://digilent.com/reference/programmable-logic/cora-z7/start), it can be pretty precise when you use averaging.

I connected a voltage reference to the unipolar auxiliary channel VAUX[1] of my Cora Z7. My recently calibrated 6½ digits multimeter measured the reference as 2.495 V.  
I let the XADC digitize 6400 samples at 1 Msps using the [software app](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/tree/main/sources/XADC_tutorial_app) shared in this repository. The mean value of the 6400 samples was 2.498 V.

Only a 3 mV difference between a Cora Z7 and an expensive digital multimeter is an excellent result! Mind that the Cora Z7 board doesn't provide an external voltage reference for XADC calibration and that two resistors forming a voltage divider are placed before the VAUX[1] input, i.e., tolerances of the resistors are at play here.  
Doing the averaging is no "cheating." Every high-precision voltmeter does the same.

The following figure shows the first 800 samples of my XADC measurement.

<img src="pictures\Cora_Z7_2.495V_reading.png" alt="Zynq-7000 XADC constant signal digitization">

We see that the noise makes most of the samples oscillate in an interval of 8 bits (8 values) of the 12-bit measurements. One bit represents a 0.81 mV change in the measured voltage.

I mentioned earlier that the XADC can be configured to do averaging of samples. I set 64-sample averaging by calling `XSysMon_SetAvg(&XADCInstance, XSM_AVG_64_SAMPLES);` and captured 100 samples shown in the next figure.

<img src="pictures\Cora_Z7_2.495V_64avg_reading.png" alt="Zynq-7000 XADC constant signal digitization with averaging">

The signal looks much cleaner now. The basic sample rate of the XADC is still 1 Msps, but it averages 64 samples before it produces one sample as the output. Therefore, the apparent sample rate is 15.6 ksps ( $`1000/64\dot{=}15.6`$ ). The 100 data points shown in the figure result from 6400 acquisitions done by the XADC.  
The output of XADC's averaging is a 16-bit value, so we see much finer differences between the data points compared to raw 12-bit samples in the previous figure.

Of course, you can achieve 64-sample averaging (or any other type of averaging) by post-processing raw 12-bit samples with the PS code or PL logic. Nevertheless, 16, 64, or 256-sample averaging, which the XADC is able to do internally, can save you the coding effort.

## DMA (Direct Memory Access)

I think the most practical way to transfer large amounts of samples from the XADC for processing in the PS is by means of DMA (Direct Memory Access). This is achieved by including the [AXI Direct Memory Access IP](https://www.xilinx.com/products/intellectual-property/axi_dma.html) in the HW design.

<img src="pictures\bd_axi_dma_ip.png" width="300">

The "magic" of the AXI DMA is that it gets data from the slave [AXI-Stream](https://docs.amd.com/r/en-US/ug1399-vitis-hls/How-AXI4-Stream-Works) interface S_AXIX_S2MM and sends them via the master AXI interface M_AXI_S2MM to a memory address. If the M_AXI_S2MM is properly connected (as I will show later in this tutorial), the data are loaded directly into the RAM without Zynq-7000 ARM core being involved. (The S_AXI_LITE interface is used to control the AXI DMA by functions from [xaxidma.h](https://xilinx.github.io/embeddedsw.github.io/axidma/doc/html/api/xaxidma_8h.html).)

In essence, you call something like `XAxiDma_SimpleTransfer( &AxiDmaInstance, (UINTPTR)DataBuffer, DATA_SIZE, XAXIDMA_DEVICE_TO_DMA );` in the PS code and wait till the data appears in the `DataBuffer` (I will explain all the details in a [later chapter](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP?#using-the-dma)).

The maximum amount of data that AXI DMA can move in a single transfer is 64 MB (exactly 0x3FFFFFF bytes). This is because the AXI DMA register to store buffer length can be, at most, 26 bits wide.  
In our case, we will be transferring 16-bit values, i.e., we can transfer at most 33,554,431 samples in one go. That should be more than enough. We could record up to 33.6 seconds of the input signal with the XADC running at 1 Msps.

The [XADC Wizard IP](https://www.xilinx.com/products/intellectual-property/xadc-wizard.html) can be configured to have an output AXI-Stream interface. When you configure the XADC for continuous sampling, you will get the actual stream of data coming out from the XADC Wizard AXI-Stream interface at a rate equal to the sampling rate of the XADC. However, this data stream is not ready to be connected directly to the AXI DMA.  
The thing is that the AXI-Stream interface on the XADC Wizard doesn't contain an AXI-Stream signal TLAST. This signal is asserted to indicate the end of the data stream. The AXI DMA must receive the TLAST signal to know when to stop the DMA transfer.

Therefore, we need an intermediate PL module to handle the AXI-Stream data between the XADC Wizard and the DMA IP. I wrote a module [stream_tlaster.v](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/blob/main/sources/HDL/stream_tlaster.v) for use in this tutorial.

<img src="pictures\bd_tlaster.png" width="200">

This Verilog module controls when the data from the slave AXI-Stream interface (connected to the XADC Wizard) starts to be sent to the master AXI-Stream interface (connected to the AXI DMA).  This happens when the input signal `start` is asserted.
The module also controls how many data transfers are made (the input signal `count` defines this) and asserts the TLAST signal of the m_axis interface on the last transfer.

We will control the input signals `start` and `count` from the PS (will connect them to the GPIO). First, we set the `count`, then call `XAxiDma_SimpleTransfer()`, and lastly, assert the `start` signal so the data starts to flow into the AXI DMA and thus into the RAM. This will ensure our complete control of how many data samples are transferred from the XADC into the RAM and when.

## Hardware design in Vivado

Note: I'm using the board Digilent [Cora Z7-07S](https://digilent.com/reference/programmable-logic/cora-z7/start) in this tutorial. However, most of the steps are valid also for any other Zynq-7000 board.  
I'm using Vivado 2024.1. Nevertheless, the same steps work also in Vivado 2023.1.

Make sure you have Digilent board files installed. [This article](https://digilent.com/reference/programmable-logic/guides/install-board-files) provides instructions on how to install them.  

- In short: Download the most recent [Master Branch ZIP Archive](https://github.com/Digilent/vivado-boards/archive/master.zip), open it, and extract the content of folder \vivado-boards-master\new\board_files into c:\Xilinx\Vivado\2024.1\data\boards\board_files\\. You may need to create the folder board_files at the destination.

### Constraints

Create a new RTL Project in Vivado 2024.1. Select your version of Cora Z7 from the list of boards.

Let's first set the constraints.  
Download the [Cora-Z7-07S-Master.xdc](https://github.com/Digilent/digilent-xdc/blob/master/Cora-Z7-07S-Master.xdc) from the Digilent [XDC repository](https://github.com/Digilent/digilent-xdc/tree/master) and import it in Vivado as a constraints file (this file will work also for Cora Z7-10).

We will use the two buttons, the dedicated analog input v_p/v_n (labeled V_P/V_N on the board) and the auxiliary input vaux1_p/vaux1_n (labeled A0 on the board; the vaux1_n is connected to ground, it doesn't have a pin on the board).  
Uncomment the following lines in the constraints file:

```
## Buttons
set_property -dict { PACKAGE_PIN D20   IOSTANDARD LVCMOS33 } [get_ports { btn[0] }]; #IO_L4N_T0_35 Sch=btn[0]
set_property -dict { PACKAGE_PIN D19   IOSTANDARD LVCMOS33 } [get_ports { btn[1] }]; #IO_L4P_T0_35 Sch=btn[1]

## Dedicated Analog Inputs
set_property -dict { PACKAGE_PIN K9    IOSTANDARD LVCMOS33 } [get_ports { Vp_Vn_0_v_p }]; #VP_0 Sch=xadc_v_p
set_property -dict { PACKAGE_PIN L10   IOSTANDARD LVCMOS33 } [get_ports { Vp_Vn_0_v_n }]; #VN_0 Sch=xadc_v_n

## ChipKit Outer Analog Header - as Single-Ended Analog Inputs
## NOTE: These ports can be used as single-ended analog inputs with voltages from 0-3.3V (ChipKit analog pins A0-A5) or as digital I/O.
## WARNING: Do not use both sets of constraints at the same time!
set_property -dict { PACKAGE_PIN E17   IOSTANDARD LVCMOS33 } [get_ports { Vaux1_0_v_p  }]; #IO_L3P_T0_DQS_AD1P_35 Sch=ck_an_p[0]
set_property -dict { PACKAGE_PIN D18   IOSTANDARD LVCMOS33 } [get_ports { Vaux1_0_v_n  }]; #IO_L3N_T0_DQS_AD1N_35 Sch=ck_an_n[0]
```
> [!TIP]
>
> Specifying constraints for the dedicated analog input channel V<sub>P</sub>/V<sub>N</sub> is optional. Vivado will do synthesis and implementation even without the constraints specified.
>
> For auxiliary channels, the constraints specification can be omitted only if you don't use other Zynq pins from Bank 35.  
> In our case, we need buttons and they are connected to pins in Bank 35 on Cora Z7. Without the constraints for VAUX[1], the implementation would fail because of incompatible IO standards error.

### Zynq Processing System

Create the block design.  
Add the ZYNQ7 Processing System to the diagram. Vivado offers to run the block automation. Run it. DDR and FIXED_IO signals will be connected to the Zynq PS.

We need to configure the Zynq PS to meet our needs.  
Enable Slave AXI High-Performance interface 0. This is the interface to which AXI DMA will be connected.

<img src="pictures\bd_axi_hp0.png" width="500">

We will need 28 EMIO GPIO pins. Let's enable them in the Zynq PS configuration.

<img src="pictures\bd_gpio.png" width="400">

**Note:** After you close the Zynq PS configuration, Vivado will probably display a critical warning about negative DQS skew values. This warning can be ignored. I guess this is some glitch in the Cora Z7 board file. It has no negative effect.

There are 64 EMIO GPIO pins on Zynq-7000. The first 32 pins are in Bank 2 (EMIO pin numbers 54 through 85). Let me explain how we will use the first 28 GPIO pins from Bank 2 in our design.

| EMIO pin number | Usage                                                        |
| --------------- | :----------------------------------------------------------- |
| 54              | output from Zynq PS  <br />`start` input signal to the [stream_tlaster.v](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/blob/main/sources/HDL/stream_tlaster.v)  <br />see the explanation in the [DMA chapter](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP?#dma-direct-memory-access) |
| 55-80           | output from Zynq PS  <br />25-bit value of the `count` input signal to the [stream_tlaster.v](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/blob/main/sources/HDL/stream_tlaster.v)  <br />see the explanation in the [DMA chapter](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP?#dma-direct-memory-access) |
| 81              | input to Zynq PS  <br />connected to the board's button BTN0 |
| 82              | input to Zynq PS  <br />connected to the board's button BTN1 |

Let's connect the buttons.  
Create input port btn[1:0].

<img src="pictures\bd_btn.png" width="300">

Since the buttons are the two most significant bits in the vector of GPIO signals, we need to concatenate them with a 26-bit zero value before we can connect them to the Zynq PS.  
Add a 26-bit Constant with zero value to the diagram.

<img src="pictures\bd_const.png" width="300">

Add Concat to the diagram. Connect the constant to Concat's In0 and btn[1:0] to In1. Then, connect the dout of the Concat to GPIO_I of the Zynq PS. We now have the following diagram. (Don't be alarmed that Concat doesn't show the correct widths of the signals. Vivado will update this later.)

<img src="pictures\bd_1.png" width="450">

### XADC Wizard

Now, we add a Clocking Wizard. It will generate the XADC input clock DCLK, i.e., the clock for AXI interfaces connected to the XADC, because, in our setup, the DCLK will be driven by the AXI clock.

Add a Clocking Wizzard to the diagram and connect clk_in1 to the FCLK_CLK0 output clock of the Zynq PS. 

Change the source of the input clock from "Single ended clock capable pin" to "No buffer".  
The "clock capable pin" setting applies to clock signals from an external pin, which the FCLK_CLK0 is not. Therefore, an input buffer (IBUF) on Clocking Wizzard is unnecessary. Leaving the source set as a "clock capable pin" may result in a critical warning during implementation on some boards.

<img src="pictures\bd_clwiz1.png" width="750">

Set the frequency of clk_out1 to 104 MHz. This frequency will allow us to run the XADC at 1 Msps. We will use a clock divider equal to 4, which gives ADCCLK of 26 MHz, which translates to the sampling rate of 1 Msps. See detailed explanation in the chapter [Clocking, sampling rate, and bandwidth](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP?#clocking-sampling-rate-and-bandwidth).  
If you later want to experiment with slower XADC sampling rates, you can set the Clocing Wizard's output frequency and the clock divider differently so you achieve the desired sampling rate.

Set the Reset Type of the Clocking Wizzard to Active Low. This is because the FCLK_RESET0_N reset signal of the Zynq PS is active low.

<img src="pictures\bd_clwiz2.png" width="700">

Connect the reset signal of the Clocking Wizard with the FCLK_RESET0_N of the Zynq PS.

It's time for the XADC Wizard now. Add it to the diagram and open its configuration.  
We will control the XADC from PS using functions from [xsysmon.h](https://github.com/Xilinx/embeddedsw/blob/master/XilinxProcessorIPLib/drivers/sysmon/src/xsysmon.h). Therefore, we will only do a bare minimum configuration of the XADC Wizard in Vivado. 

On the Basic tab, enable Enable AXI4Stream. We need the XADC Wizard to provide an AXI-Stream interface because this is how we get data into the RAM by means of AXI DMA.

In the Startup Channel Selection, select Single Channel (if not selected already by default). Then go to the Single Channel tab and select "VAUXP1 VAUXN1" (i.e., the auxiliary channel 1).  
Notice that the XADC Wizard now exposes both V<sub>P</sub>/V<sub>N</sub> and VAUX[1] channels as input signals (V<sub>P</sub>/V<sub>N</sub> is always available as the input signal in all XADC Wizard configurations.)

That's all the XADC Wizard configuration we need to do.

> [!TIP]
>
> If you need to switch Single Channel mode between multiple auxiliary channels, follow this "hack" to expose them as input signals on the XADC Wizard:  
> Select Channel Sequencer in the Startup Channel Selection. Channel Sequencer tab will appear, where you can select as many auxiliary channels as you wish. It doesn't matter that you configured the XADC Wizard IP for Channel Sequencer mode. You will switch it to the Single Channel mode during runtime by calling [XSysMon_SetSingleChParams()](https://github.com/Xilinx/embeddedsw/blob/5688620af40994a0012ef5db3c873e1de3f20e9f/XilinxProcessorIPLib/drivers/sysmon/src/xsysmon.c#L586).

We need to connect the analog channels.  
Right-click on the Vp_Vn input signal of the XADC Wizard and select Make External. Repeat the same for input signal Vaux1. This will create two analog differential input ports in the diagram. The ports in the constraint file are properly named to match the ports we just created.

Vivado now offers Run Connection Automation. Run it.  
Set all clock sources to the Clocking Wizard (/clk_wiz_0/clk_out1). As explained earlier, we want to drive the XADC clock from the Clocking Wizard to have the flexibility of choosing the frequency.

<img src="pictures\bd_conn_aut_axi_lite.png" width="520">

<img src="pictures\bd_conn_aut_axis.png" width="520">

The Connection Automation added AXI Interconnect to connect the Zynq PS with the XADC Wizard. A processor System Reset was added to generate a reset signal for the AXI bus. Everything is clocked by the Clocking Wizard.   
We now have the following diagram (I shuffled and rotated the IPs for better clarity).

<img src="pictures\bd_2.png">

### DMA

As explained in the [DMA chapter](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP?#dma-direct-memory-access), we need to have the module [stream_tlaster.v](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/blob/main/sources/HDL/stream_tlaster.v) between the XADC Wizard and AXI DMA, and we need to connect its input signals to Zynq PS GPIO.

Add the [stream_tlaster.v](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/blob/main/sources/HDL/stream_tlaster.v) as a design source to the project. Add the module to the block diagram.  
Connect the module's s_axis interface to the M_AXIS interface of the XADC Wizard. Connect the module's input clock (clk) to the output clock of the Clocking Wizard.

In order to connect the module's input signals start and count, we need to slice the 28-bit PS GPIO signal accordingly.  
Add Slice twice to the diagram and connect both to the GPIO_O[27:0] of the Zynq PS.

Configure xlslice_0 to extract the least significant bit and connect its output to the start signal of the stream_tlaster module.

<img src="pictures\bd_slice0.png" width="450">

Configure xlslice_1 to extract bits [25:1] and connect its output to the count signal of the stream_tlaster module.

<img src="pictures\bd_slice1.png" width="450">

Our diagram now looks as follows.

<img src="pictures\bd_3.png">

Now, add the AXI Direct Memory Access to the diagram and open its configuration.  
We do not need to use AXI DMA [Scatter/Gather Mode](https://docs.amd.com/r/en-US/pg021_axi_dma/Scatter/Gather-Mode), so we disable the Enable Scatter Gather Engine option.  
We set the Width of the Buffer Length Register to the value 26 (i.e., the maximum). This will allow us to transfer up to 33.5 million XADC data samples in one transfer (64 MB of data).  
We are only writing to RAM, so we disable the option Enable Read Channel.  
We enable the Allow Unaligned Transfer option on the Write Channel and increase the Max Burst Size to 128.

<img src="pictures\bd_dma.png" width="730">

Connect S_AXIS_S2MM of AXI DMA with the m_axis of the stream_tlaster module.  
Run Connection Automation, which Vivado offers now. Make sure to set all clock sources to the Clocking Wizard (/clk_wiz_0/clk_out1).

The automation connected the AXI DMA AXI-Lite interface to the Zynq PS via the existing AXI Interconnect. It created a new AXI Interconnect and used it to connect the DMA AXI master interface to the S_AXI_HP0 of Zynq PS. Also, the resets and clocks were connected accordingly.

We now have the final diagram (the image in full resolution is available [here](https://raw.githubusercontent.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/refs/heads/main/pictures/bd_final.png)).

<img src="pictures\bd_final.png" alt="XADC tutorial HW design diagram">

### Generate output

To make sure that nothing was missed, click the Validate Design button in the toolbar of the diagram window (or press F6).  
You will probably see a critical warning about negative DQS skew values. This warning can be ignored. I guess this is some glitch in the Cora Z7 board file. It has no negative effect.

HDL Wrapper for the diagram needs to be created: Go to Sources|Design Sources, right-click on the block diagram's name, select "Create HDL Wrapper," and select "Let Vivado manage wrapper."

Now, we create the design outputs: Click "Generate Bitstream" in the Flow Navigator on the left. Synthesis and Implementation will be run automatically before bitstream generation. There should be no errors.

Last but not least, we need to export the hardware specification to an XSA file. Go to File|Export|Export Hardware, and select "Include Bitstream".

## Software

### Building the application

I'm using Vitis Classic 2024.1 in this chapter. Nevertheless, the same steps work also in Vitis 2023.1 and Vitis Classic 2023.2.

If you want to use Vitis 2024.1.1 (a.k.a. Vitis Unified 2024), go to this [readme file](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/blob/main/sources/XADC_tutorial_app_Vitis_Unified/README.md) and then continue with the chapter [How to use the application](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP?#how-to-use-the-application).

Start Vitis Classic 2024.1.  
Create a new platform project using the HW export XSA file we just generated.  
Make sure to select freertos_10_xilinx as the operating system.

We will be using the lwIP library to stream XADC data over the network. Therefore, we must enable the lwIP in the Board Support Package settings (BSP).

<img src="pictures\vt_bsp_lwip.png" width="400">

The following settings are then needed in the lwIP BSP configuration:

- Set api_mode to "SOCKET API" because this api_mode is required for a FreeRTOS or stand-alone application.
- Set dhcp_options/lwip_dhcp to true because my demo application is using DHCP to obtain an IP address.

<img src="pictures\vt_bsp_lwip_conf.png" width="400">

Create a new application project.  
Make sure to select the "**Empty Application (C++)**" in the last step of the application project creating wizard.

Copy all source files from the [XADC_tutorial_app](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/tree/main/sources/XADC_tutorial_app) folder of my repository into the src folder of the application project in Vitis.  
Let me briefly explain what source files we have:

| Source file                                                  | Description                                                  |
| ------------------------------------------------------------ | ------------------------------------------------------------ |
| [FileViaSocket.h](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/blob/main/sources/XADC_tutorial_app/FileViaSocket.h)  <br />[FileViaSocket.cpp](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/blob/main/sources/XADC_tutorial_app/FileViaSocket.cpp) | Definition of the C++ [ostream](https://en.cppreference.com/w/cpp/io/basic_ostream) class, which the demo application uses to send data over the network. I copied the files from another [repository](https://github.com/viktor-nikolov/lwIP-file-via-socket) of mine. |
| [button_debounce.h](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/blob/main/sources/XADC_tutorial_app/button_debounce.h)  <br />[button_debounce.cpp](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/blob/main/sources/XADC_tutorial_app/button_debounce.cpp) | A C++ class that the demo application uses for debouncing buttons (i.e., to ensure that the app gets a filtered signal from the buttons for smooth control).  <br />Copyright © 2014 [Trent Cleghorn](https://github.com/tcleg). I copied the files from his [repository](https://github.com/tcleg/Button_Debouncer). |
| [main.cpp](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/blob/main/sources/XADC_tutorial_app/main.cpp) | The main source file of the demo application.                |
| [network_thread.cpp](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/blob/main/sources/XADC_tutorial_app/network_thread.cpp) | The definition of a FreeRTOS thread, which initiates the network and handles network operation.  <br />I derived it from a [sample project](https://github.com/Xilinx/embeddedsw/blob/master/lib/sw_apps/freertos_lwip_tcp_perf_client/src/main.c) provided by AMD Xilinx.<br/>Copyright © 2024 Viktor Nikolov<br/>Copyright © 2018-2022 Xilinx, Inc.<br/>Copyright © 2022-2023 Advanced Micro Devices, Inc. |

The project should be built without errors. You may see two or three warnings coming from the platform source files (not files in the app's src folder). These can be ignored.

### How to use the application

The HW design and the demo application from this tutorial allow 1 Msps digitalization on the differential dedicated analog input channel V<sub>P</sub>/V<sub>N</sub>  (labeled V_P/V_N on the Cora Z7 board) and the unipolar auxiliary channel VAUX[1] (labeled A0 on the board).

To see interesting results, you need to use a signal generator. Connect a suitable differential signal to pins V_P/V_N (e.g., a signal I used in [this chapter](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/tree/main#the-behavior-of-dedicated-channel-vpvn-aaf-of-cora-z7)) and a unipolar signal to pin A0.

> [!CAUTION]
>
> **The voltage on the V_P and V_N pins must always be within a range from 0 V to 1.0 V with respect to the board's GND. Also, the differential V<sub>P</sub> &minus; V<sub>N</sub> must be within the range of ±0.5V.**
>
> **The voltage on the pin A0 must always be positive and not greater than 3.3 V.**

The application sends data samples over the network to a server, which is the Python script [file_via_socket.py](https://github.com/viktor-nikolov/lwIP-file-via-socket/blob/main/file_via_socket.py).  
You must specify the IP address of the server in the constant in [main.cpp](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/blob/main/sources/XADC_tutorial_app/main.cpp). It is this line at the beginning of the main.cpp:

```c++
const std::string SERVER_ADDR( "192.168.44.10" ); // Specify your actual server IP address
```

The server writes the XADC samples (a list of voltage values) to a text file. Each set of samples is written to a new file.  
The standard name of the file the server creates looks like this: via_socket_*240324_203824.6369*.txt  
Part of the name in italics is the date and time stamp.

Depending on your Python installation, run the server script with the command  
`python3 file_via_socket.py [params]` or  
`python file_via_socket.py [params]`.

In typical use, you will want to specify the output folder for the files. For example:
```
>python file_via_socket.py --path c:\Temp\XADC_data
Waiting for connection on 0.0.0.0:65432
(Press Ctrl+C to terminate)
```

The default port the script listens on for connections is 65432, and the default bind IP address is 0.0.0.0 (i.e., the script listens on all the configured network interfaces and their IP addresses).  
Typically, you have just one IP address assigned to the Ethernet port of your PC. Use the command `ipconfig` (on Windows) or `ip a` (on Linux) to get this address and enter it as the `SERVER_ADDR` constant at the beginning of the [main.cpp](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/blob/main/sources/XADC_tutorial_app/main.cpp).   
**Make sure that the firewall on your PC allows incoming connections to Python on the given IP address and port.**

I tested the script on Windows 11 and Ubuntu 22.04.  
To get the full list of available parameters, run `python file_via_socket.py --help`.

Let me summarize. To successfully use the demo application, you need to perform these steps:

1. Connect a suitable signal from a signal generator to Cora Z7 pins V_P and V_N or to pin A0 (or to both).
2. Connect the network cable to the Cora Z7 board.
3. Start the `python file_via_socket.py` on your PC as the server to receive digitized data samples.
4. Start a serial terminal application (e.g., [PuTTY](https://www.putty.org/)) and connect it to the USB serial port of the Cora Z7 board in your OS.
5. Specify the IP address of your server in the constant `SERVER_ADDR` at the beginning of the [main.cpp](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/blob/main/sources/XADC_tutorial_app/main.cpp).  
   Use the command `ipconfig` (on Windows) or `ip a` (on Linux) if you are unsure what the IP address of your PC is.
6. Build and run the application in Vitis.

After the application starts, you shall see the output in the serial terminal similar to this:

```
*************** PROGRAM STARTED ***************

------lwIP Socket Mode TCP Startup------
Start PHY autonegotiation 
Waiting for PHY to complete autonegotiation.
autonegotiation complete 
link speed for phy address 1: 1000
DHCP request success
Board IP:       192.168.44.39
Netmask :       255.255.255.0
Gateway :       192.168.44.1

***** XADC THREAD STARTED *****
will connect to the network address 192.168.44.10:65432
samples per DMA transfer: 1000
no averaging is used
calib coefficient ADC offset: FF9A (-7 bits)
calib coefficient gain error: 007F (6.3%)

press BTN0 to start ADC conversion
press BTN1 to switch between VAUX[1] and VP/VN inputs
VAUX[1] is activated as the input
```

Information under the header `--lwIP Socket Mode TCP Startup--` comes from the lwIP network initialization and DHCP IP address assignment.  
After the network initializes the thread controlling the XADC takes over and displays some basic information.

We see that 1000 XADC samples will be provided in each measurement (i.e., in each DMA transfer from the XADC). You can control this number of samples by modifying the macro `SAMPLE_COUNT` at the beginning of the [main.cpp](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/blob/main/sources/XADC_tutorial_app/main.cpp).

```c++
/* Number of samples transferred in one DMA transfer. Max. value is 33,554,431 */
#define SAMPLE_COUNT 1000
```

- Note: Yes, I successfully tested the digitization of 33,554,431 samples. :smiley: It takes 33.5 seconds to record the XADC data and then about 1 minute 45 seconds to convert raw values to voltage and transfer the data to the PC (tested on Cora Z7, compiled with the highest optimization level -O3).

We also see in the console output that XADC will not use any averaging. This is controlled by defining the value of the macro `AVERAGING_MODE` at the beginning of the [main.cpp](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/blob/main/sources/XADC_tutorial_app/main.cpp). You can select one of the four options: 

```c++
/* Set XADC averaging.
 * Leave one of the lines below uncommented to set averaging mode of the XADC. */
#define AVERAGING_MODE XSM_AVG_0_SAMPLES // No averaging
//#define AVERAGING_MODE XSM_AVG_16_SAMPLES  // Averaging over  16 acquisition samples
//#define AVERAGING_MODE XSM_AVG_64_SAMPLES  // Averaging over  64 acquisition samples
//#define AVERAGING_MODE XSM_AVG_256_SAMPLES // Averaging over 256 acquisition samples
```

The next information in the console output tells us that the value of XADC's Offset Calibration Coefficient ix 0xFF9A, which translates to -7 bits of correction. You may observe that this value changes slightly with each application run.

The value of XADC's Gain Calibration Coefficient is shown as 0x007F, which translates to a 6.3% correction (the maximum possible value). This is expected on the Cora Z7 board for reasons I explained in detail in the chapter [XADC autocalibration](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP?#xadc-autocalibration).

Lastly, the console output tells us what the buttons do and that the auxiliary channel VAUX[1] is activated as input for the XADC measurement.  
When we press the board's button labeled BTN0, the application will store 1000 XADC samples in memory (using DMA), display values of the first 8 samples on the terminal, and send all 1000 samples to the server, where a file of 1000 lines will be created. 

```
***** XADC DATA[0..7] *****
1.878496
1.651487
1.448801
1.277734
1.156933
1.086398
1.074237
1.120449
sending data...   sent
```

### Controlling the XADC from the PS

Let me explain the aspects of controlling the XADC from the PS in more detail. I will use code snippets from the [main.cpp](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/blob/main/sources/XADC_tutorial_app/main.cpp).

The initialization of the XADC is very similar to the other Xillinx subsystems:

```c++
#include "xsysmon.h"

XSysMon XADCInstance;      // The XADC instance
XSysMon_Config *ConfigPtr; // Pointer to the XADC configuration
XStatus Status;

ConfigPtr = XSysMon_LookupConfig( XPAR_XADC_WIZ_0_DEVICE_ID ); // The macro comes from xparameters.h
if( ConfigPtr == NULL ) { /* raise an error*/ }

Status = XSysMon_CfgInitialize( &XADCInstance, ConfigPtr, ConfigPtr->BaseAddress );
if( Status != XST_SUCCESS ) { /* raise an error*/ }
```

After the initialization, I disable XADC features, which we do not need in this demo application:

```c++
// Disable all interrupts
XSysMon_IntrGlobalDisable( &XADCInstance );
// Disable the Channel Sequencer (we will use the Single Channel mode)
XSysMon_SetSequencerMode( &XADCInstance, XSM_SEQ_MODE_SINGCHAN );
// Disable all alarms
XSysMon_SetAlarmEnables( &XADCInstance, 0 );

/* Disable averaging for the calculation of the calibration coefficients */
// Read Configuration Register 0
u32 RegValue = XSysMon_ReadReg( XADCInstance.Config.BaseAddress, XSM_CFR0_OFFSET );
// To disable calibration coef. averaging, set bit XSM_CFR0_CAL_AVG_MASK to 1
RegValue |= XSM_CFR0_CAL_AVG_MASK;
// Write Configuration Register 0
XSysMon_WriteReg( XADCInstance.Config.BaseAddress, XSM_CFR0_OFFSET, RegValue );
```

The XADC Averaging is set using the following call. 

```c++
XSysMon_SetAvg( &XADCInstance, AVERAGING_MODE );
```
The macro `AVERAGING_MODE` is set to one of the values `XSM_AVG_0_SAMPLES` (no averaging), `XSM_AVG_16_SAMPLES`, `XSM_AVG_64_SAMPLES` or `XSM_AVG_256_SAMPLES` at the beginning of the [main.cpp](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/blob/main/sources/XADC_tutorial_app/main.cpp).

Because the board Cora Z7 doesn't provide an external voltage reference to the XADC, we must make sure to enable only usage of the Offset Calibration Coefficient by the following call (see details explained in the chapter [XADC Autocalibration](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP?#xadc-autocalibration)).

```c++
XSysMon_SetCalibEnables(&XADCInstance, XSM_CFR1_CAL_ADC_OFFSET_MASK | XSM_CFR1_CAL_PS_OFFSET_MASK);
```

Before activating an analog input, we must set the ADCCLK divider ratio (see details explained in the chapter [Clocking](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP?#clocking-sampling-rate-and-bandwidth)).

```c++
// Set the ADCCLK frequency equal to 1/4 of the XADC input clock 
XSysMon_SetAdcClkDivisor( &XADCInstance, 4 );
```

The following call activates the auxiliary input VAUX[1] in the single channel unipolar continuous sampling mode.

```c++
XSysMon_SetSingleChParams(
  &XADCInstance,
  XSM_CH_AUX_MIN+1, // == channel index of VAUX[1]
  false,            /* IncreaseAcqCycles==false -> default 4 ADCCLKs used for the settling;
                       true -> 10 ADCCLKs used */
  false,            // IsEventMode==false -> continuous sampling
  false );          // IsDifferentialMode==false -> unipolar mode
```

The second parameter of [XSysMon_SetSingleChParams()](https://github.com/Xilinx/embeddedsw/blob/5688620af40994a0012ef5db3c873e1de3f20e9f/XilinxProcessorIPLib/drivers/sysmon/src/xsysmon.c#L586) is the channel index. You can use macros `XSM_CH_*`, which are defined in the [xsysmon.h](https://github.com/Xilinx/embeddedsw/blob/master/XilinxProcessorIPLib/drivers/sysmon/src/xsysmon.h). Macro `XSM_CH_AUX_MIN` is the index of VAUX[0]. By adding 1 to it, we get the index of VAUX[1]. The macro `XSM_CH_VPVN` gives the index of the dedicated analog input channel V<sub>P</sub>/V<sub>N</sub>.

Next is the boolean parameter `IncreaseAcqCycles`.  
Value false means that the default duration of 4 ADCCLK clock cycles is used for the settling period, so the acquisition takes 26 ADCCLK cycles in total. We use a 104 MHz XADC input clock in the HW design. We set the divider ratio to 4 by calling [XSysMon_SetAdcClkDivisor()](https://github.com/Xilinx/embeddedsw/blob/5688620af40994a0012ef5db3c873e1de3f20e9f/XilinxProcessorIPLib/drivers/sysmon/src/xsysmon.c#L1089). This results in 26 MHz ADCCLK and thus 1 Msps sampling rate.  
If the parameter `IncreaseAcqCycles` was true, 10 ADCCLK cycles would be used for the settling period, thus extending the acquisition to 32 ADCCLK cycles. That would result in an 812.5 ksps sampling rate. 

The HW design in this tutorial and the demo app are set to run the XADC at the maximum possible sampling rate of 1 Msps.  
To achieve other (i.e., lower) sampling rates, you need to set a suitable XADC Wizard input clock frequency in the HW design and a suitable ADCCLK clock divider ratio so the quotient of frequency and the ratio is 26 times the desired sampling rate.  
E.g., to have a sampling rate of 100 ksps, you can set the Clocking Wizard, which feeds the XADC Wizard input clock, to 101.4&nbsp;MHz and set the divider ratio to 39 (by calling [XSysMon_SetAdcClkDivisor()](https://github.com/Xilinx/embeddedsw/blob/5688620af40994a0012ef5db3c873e1de3f20e9f/XilinxProcessorIPLib/drivers/sysmon/src/xsysmon.c#L1089)).&nbsp;&nbsp;&nbsp;$`\frac{101400}{39} = 2600`$&nbsp;&nbsp;&nbsp;&nbsp;$`\frac{2600}{26} = 100 \mskip3mu ksps`$

The next boolean parameter `IsEventMode` specifies [event sampling mode](https://docs.amd.com/r/en-US/ug480_7Series_XADC/Event-Driven-Sampling) (value true) or [continuous sampling mode](https://docs.amd.com/r/en-US/ug480_7Series_XADC/Continuous-Sampling) (value false).

The last boolean parameter, `IsDifferentialMode`, specifies bipolar mode (value true) or unipolar mode (value false). See the explanation of the two modes in [this chapter](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP?#what-is-xadc).

### Using the DMA

In our design, the XADC starts sending the desired data samples over the master AXI-Stream of the XADC Wizard after we call  [XSysMon_SetSingleChParams()](https://github.com/Xilinx/embeddedsw/blob/5688620af40994a0012ef5db3c873e1de3f20e9f/XilinxProcessorIPLib/drivers/sysmon/src/xsysmon.c#L586) in the PS.  
Let me explain in this chapter how we control the AXI DMA to get data from PL into the RAM of the Zynq ARM core.

The initialization of the DMA is like that of other Xillinx subsystems:

```c++
#include "xaxidma.h"

XAxiDma AxiDmaInstance; // The AXI DMA instance
XAxiDma_Config *cfgptr; // Pointer to the AXI DMA configuration
XStatus Status;

cfgptr = XAxiDma_LookupConfig( XPAR_AXI_DMA_0_DEVICE_ID ); // The macro comes from xparameters.h
if( cfgptr == NULL ) { /* raise an error*/ }

Status = XAxiDma_CfgInitialize( &AxiDmaInstance, cfgptr );
if( Status != XST_SUCCESS ) { /* raise an error*/ }
```

We don't use AXI DMA interrupts in this demo application, so we disable them:

```c++
XAxiDma_IntrDisable( &AxiDmaInstance, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DEVICE_TO_DMA );
XAxiDma_IntrDisable( &AxiDmaInstance, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DMA_TO_DEVICE );
```

We need to have a space in memory for the AXI DMA to load data into. The easiest way is to declare the data buffer as a global variable. This way, we don't need to worry about the FreeRTOS thread's stack size. 

```c++
u16 DataBuffer[ SAMPLE_COUNT + 8 ] __attribute__((aligned(4)));
```

Important considerations go into declaring an array for receiving data from AXI DMA. Let me explain:

1. **Data type:** In our case, the AXI-Stream data width is 16 bits. This is because the XADC Wizzard exposes a 16-bit wide master AXI-Stream interface, and it goes as the 16-bit stream all the way into the AXI DMA. So, we use the data type `u16`.
2. **Array length:** The number of samples we transfer in one DMA transfer is given by the macro `SAMPLE_COUNT`. However, there is a catch, which is why I recommend that you declare the `DataBuffer` slightly larger than needed.  
   The AXI DMA loads data directly into the RAM. There is a data cache between the RAM and the CPU in play. To achieve proper results so the CPU "sees" the correct data, we flush the data cache into RAM by calling [Xil_DCacheFlushRange()](https://docs.amd.com/r/en-US/oslib_rm/Xil_DCacheFlushRange?tocId=ih5Bwba_1KuHZ_3v1wDPjw) before the DMA transfer. We then invalidate the data cache by calling [Xil_DCacheInvalidateRange()](https://docs.amd.com/r/en-US/oslib_rm/Xil_DCacheInvalidateRange?tocId=hQlJBPx~LFoO1Pndt20_5g) after the DMA transfer finishes so the `DataBuffer` memory region is served to the CPU from RAM when read for the first time.  
   I faced strange errors in my testing when I used a bigger `DataBuffer`. The problem went away when I declared the `DataBuffer` slightly larger than needed. I think this is a bug or a limitation of the [Xil_DCacheInvalidateRange()](https://docs.amd.com/r/en-US/oslib_rm/Xil_DCacheInvalidateRange?tocId=hQlJBPx~LFoO1Pndt20_5g).  
   My theory is that the issue is connected to the fact that the data cache is organized into so-called lines, which are 32 bytes long on the ARM Cortex-A9 CPU used in Zynq-7000. Cache invalidation is done by the 32-byte cache lines, not by the exact length of `DataBuffer`. I guess the problem I observed was caused by [Xil_DCacheInvalidateRange()](https://docs.amd.com/r/en-US/oslib_rm/Xil_DCacheInvalidateRange?tocId=hQlJBPx~LFoO1Pndt20_5g) missing a cache line.
3. **Memory alignment:** Even though the AXI DMA can be configured to allow data transfer to an address that is not aligned to a word boundary, it's a good practice to declare the `DataBuffer` as aligned to a 32-bit word. This is what the GCC attribute definition `__attribute__((aligned(4)))` does.

We tell the AXI DMA to start loading data into RAM by this call:

```c++
// Just in case, flush any data in DataBuffer, held in the CPU cache, to the RAM
Xil_DCacheFlushRange( (UINTPTR)DataBuffer, sizeof(DataBuffer) );

// Tell AXI DMA to start the data transfer
XAxiDma_SimpleTransfer( &AxiDmaInstance, (UINTPTR)DataBuffer,
                        SAMPLE_COUNT * sizeof(u16), XAXIDMA_DEVICE_TO_DMA );
```

This call initiates the AXI DMA, but no data will start flowing yet. As I explained in the chapter [DMA,](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/tree/main?#dma-direct-memory-access) we have the module  [stream_tlaster.v](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/blob/main/sources/HDL/stream_tlaster.v) in our HW design to serve as a "valve" on the AXI-Strem between the XADC Wizard and AXI DMA.  
We need to tell the module how many data samples we want to go through and then start the data flow. We do that by means of GPIO signals, which we connected to the stream_tlaster module in the HW design.

```c++
// Set sample count to EMIO GPIO pins 55-80, which are connected to the
// stream_tlaster module.
// (The least significant EMIO GPIO pin 54 is the start/stop signal.)
XGpioPs_Write( &GpioInstance, 2 /*Bank 2*/, SAMPLE_COUNT << 1 );    

// Set start signal of the stream_tlaster module to start generation of the 
// AXI-Stream of data coming from the XADC.
XGpioPs_WritePin( &GpioInstance, 54, 1 /*high*/ );
// Reset the start signal (it needed to be high for just a single PL clock cycle)
XGpioPs_WritePin( &GpioInstance, 54, 0 /*low*/  );
```

After that, we wait in a while loop for the AXI DMA to finish the data transfer.  
After the AXI DMA transfer finishes, we must invalidate the memory region of the `DataBuffer` in the data cache.

```c++
// Wait till the DMA transfer is done
while( XAxiDma_Busy( &AxiDmaInstance, XAXIDMA_DEVICE_TO_DMA ) )
    vTaskDelay( pdMS_TO_TICKS( 1 ) ); // Wait 1 ms

// Invalidate the CPU cache for the memory region holding the DataBuffer.
Xil_DCacheInvalidateRange( (UINTPTR)DataBuffer, sizeof(DataBuffer) );
```

> [!TIP]
>
> Please note that in my code, I don't need to check how much data the AXI DMA actually transferred. This is because we have the [stream_tlaster.v](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/blob/main/sources/HDL/stream_tlaster.v) module in the HW design, which gives us the ultimate control over the AXI-Stream data flow. We control in the HW when the stream starts and how much data comes in it.  
> This may not be the case in other HW designs. Post DMA transfer, the information about how many bytes were actually transferred (i.e., how many bytes came before AXI-Stream signal TLAST was asserted) is available in the AXI DMA's [S2MM_LENGTH](https://docs.amd.com/r/en-US/pg021_axi_dma/S2MM_LENGTH-S2MM-DMA-Buffer-Length-Register-Offset-58h) register. You can read this value by the following call:  
>
> ```
> u32 BytesTransferred = XAxiDma_ReadReg( AxiDmaInstance.RegBase, XAXIDMA_RX_OFFSET+XAXIDMA_BUFFLEN_OFFSET );
> ```

### Converting raw XADC data samples to the voltage

To convert the raw XADC data sample to the voltage, we must consider whether the XADC uses averaging. If a voltage divider is present on the XADC channel input, we must, of course, also consider the scaling factor.

The [main.cpp](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/blob/main/sources/XADC_tutorial_app/main.cpp) of the demo application contains conversion functions for unipolar channel VAUX[1] and bipolar channel V<sub>P</sub>/V<sub>N</sub>.  
I'm using conditional compilation based on the value of the macro `AVERAGING_MODE`. When the XADC is set to use averaging, all 16 bits of the raw sample are used. Without averaging, only 12 bits are valid; the 4 least significant bits must be ignored.  
Code snippets shown in this chapter are the versions when XADC averaging is not used.

Converting a raw sample to voltage is pretty straightforward for a channel in the unipolar mode:

```c++
// Conversion function of XADC raw sample to voltage for the unipolar channel VAUX[1]
float Xadc_RawToVoltageAUX1(u16 RawData)
{
    // We use VAUX[1] as unipolar; it has the scale from 0 V to 3.32 V.
    // There is voltage divider of R1 = 2.32 kOhm and R2 = 1 kOhm on the input.
    const float Scale = 3.32; 

    // When XADC doesn't do averaging, only the 12 most significant bits of RawData are valid
    return Scale * ( float(RawData >> 4) / float(0xFFF) );
}
```

Converting a raw sample from a channel in the bipolar mode requires a bit of binary arithmetics.  
The measuring range is not symmetrical around zero. It goes from -500.00 mV to 499.75 mV in 244 μV steps. See  [Figure 2-3](https://docs.amd.com/r/qOeib0vlzXa1isUAfuFzOQ/yDu6LkRwmz5q985frASotQ?section=XREF_78910_X_Ref_Target) in the  [UG480](https://docs.amd.com/r/en-US/ug480_7Series_XADC/ADC-Transfer-Functions).

```c++
// Conversion function of XADC raw sample to voltage for the bipolar channel VP/VN
float Xadc_RawToVoltageVPVN(u16 RawData)
{
    // When XADC doesn't do averaging, only the 12 most significant bits of RawData are valid
    if( (RawData >> 4) == 0x800 ) // This is the special case of the lowest negative value
        return -0.5;              // The measuring range in bipolar mode is -500 mV to 499.75 mV.

    float sign;

    if( RawData & 0x8000 ) {    // Is sign bit equal to 1? I.e. is RawData negative?
        sign = -1.0;
        RawData = ~RawData + 1; // Get absolute value from negative two's complement integer
    }
    else
        sign = 1.0;

    RawData = RawData >> 4; // We are not using averaging, only the 12 most significant bits of RawData
                            // are valid
    // One bit equals to the reading of 244 uV. I.e., 1/4096 == 244e-6
    return sign * float(RawData) * ( 1.0/4096.0 );
}
```

## Project files

The repository's folder [project_files](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/tree/main/project_files) provides the following files:

- [XADC_tutorial_hw_2024.1.1.xpr.zip](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/blob/main/project_files/XADC_tutorial_hw_2024.1.1.xpr.zip)
  - Contains the HW design project export from Vivado 2024.1.1.
  - This is the HW design for the Digilent [Cora Z7-07S](https://digilent.com/reference/programmable-logic/cora-z7/start) board, which we created in the chapter [Hardware design in Vivado](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP?#hardware-design-in-vivado).
  - To use the export, simply extract the .zip into a folder and open XADC_tutorial_hw.xpr in Vivado 2024.1.1.
  - The archive contains exported HW, the file system_wrapper.xsa, which can be directly used in Vitis 2024.1.1 for creating the platform.
  - Note: To have Vivado version 2024.1.1, you must install "Vivado™ Edition **Update 1** - 2024.1 Product Update" on top of "Vivado™ Edition - 2024.1 Full Product Installation". See the Xilinx [download page](https://www.xilinx.com/support/download.html).
- [XADC_tutorial_hw_2023.1.xpr.zip](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/blob/main/project_files/XADC_tutorial_hw_2023.1.xpr.zip)
  - Contains the HW design project export from Vivado 2023.1.
  - This is the HW design for the Digilent [Cora Z7-07S](https://digilent.com/reference/programmable-logic/cora-z7/start) board, created using the same steps as described in the chapter [Hardware design in Vivado](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP?#hardware-design-in-vivado).
  - To use the export, simply extract the .zip into a folder and open XADC_tutorial_hw2023.1.xpr in Vivado.
  - The archive contains exported HW, the file system_wrapper.xsa, which can be directly used in Vitis 2023.1 for creating the platform.
- [vitis_export_archive.ide_Classic_2024.1.1.zip](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/blob/main/project_files/vitis_export_archive.ide_Classic_2024.1.1.zip)
  - Contains the SW project export from Vitis Classic 2024.1.1.
  - This is the XADC demo application for the Digilent [Cora Z7-07S](https://digilent.com/reference/programmable-logic/cora-z7/start) board, which I described in the chapter [Software](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/tree/main?#software).
  - To use the export, create an empty folder on your PC and open it as a workspace in Vitis Classic 2024.1.1.  
    Then select File|Import|"Vitis project exported zip file". Select the archive file, and select all projects in the archive.
  - Note: To have Vitis version 2024.1.1, you must install "Vivado™ Edition **Update 1** - 2024.1 Product Update" on top of "Vivado™ Edition - 2024.1 Full Product Installation". See the Xilinx [download page](https://www.xilinx.com/support/download.html).

<img src="pictures\vt_import.png" width="450">

The following project exports are for building the demo application in Vitis 2024.1.1 (a.k.a. Vitis Unified 2024):

- [XADC_tutorial_timer_hw_2024.1.1.xpr.zip](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/blob/main/project_files/XADC_tutorial_timer_hw_2024.1.1.xpr.zip)
  - Contains the HW design project export from Vivado 2024.1.1.
  - This is the HW design for the Digilent [Cora Z7-07S](https://digilent.com/reference/programmable-logic/cora-z7/start) board, which we created in the chapter [Hardware design in Vivado](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP?#hardware-design-in-vivado). The only addition is the enablement of Timer 0 in the Zynq PS configuration.  
    Vitis Unified requires a HW timer to be present in the HW design. Otherwise, a FreeRTOS application can't be built in Vitis Unified.
- [vitis_archive_Unified_2024.1.1.zip](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/blob/main/project_files/vitis_archive_Unified_2024.1.1.zip)
  - Contains the SW project export from Vitis 2024.1.1.
  - This is the XADC demo application for the Digilent [Cora Z7-07S](https://digilent.com/reference/programmable-logic/cora-z7/start) board, which I described in the chapter [Software](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/tree/main?#software).
  - To use the export, create an empty folder on your PC and open it as a workspace in Vitis 2024.1.1.  
    Then select File|Import. Select the archive file, and select all projects in the archive.

<img src="pictures\unif_import.png" width="380">

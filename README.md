# Tutorial: Xilinx Zynq XADC using DMA and network streaming
This tutorial shows how to do an HW design and code a SW application to make use of AMD Xilinx Zynq-7000 [XADC](https://www.xilinx.com/products/technology/analog-mixed-signal.html). We will also see how to use the [DMA](https://www.xilinx.com/products/intellectual-property/axi_dma.html) to transfer data from the XADC into Zynq CPU's memory and stream data to a remote PC over the network.

In this tutorial, I'm using the Digilent board [Cora Z7-07S](https://digilent.com/reference/programmable-logic/cora-z7/start). However, all the principles described here can be used on any other Zynq-7000 board. I will highlight aspects specific to Cora Z7 in the text.  
The Cora Z7 is a suitable board for testing the Zynq XADC because it has analog inputs that are usable in a practical way.

The tutorial is based on the Vivado 2023.1 and Vitis 2023.1 toolchain. **TODO Vivado 2024.1.1, Vitis Classic 2024.1.1, Vitis 2024.1.1; i.e., after installing Vivado Edition Update 1**

Before we dive into drawing a block diagram in Vivado and writing code in Vitis, we need to understand the basics of the XDAC and be aware of important aspects and limitations. Let me cover this in the first chapters of this tutorial.

## TODOs, to be removed

- Oversampling: https://www.silabs.com/documents/public/application-notes/an118.pdf
- calibration:
  - [53586 - Zynq and 7-Series XADC Gain Calibration Behaviour with Internal Voltage Reference (xilinx.com)](https://support.xilinx.com/s/article/53586?language=en_US)
  - [The analog input for XADC calibration in 7 series FPGA (xilinx.com)](https://support.xilinx.com/s/question/0D52E00006hpPXlSAM/the-analog-input-for-xadc-calibration-in-7-series-fpga?language=en_US)

Cora Z7 has VREFP and VREFN connected to ADCGND

## A short introduction to Zynq-7000 XADC

### What is XADC

The XADC is a feature of an analog-to-digital converter integrated on selected Xilinx FPGA chips, including Zynq-7000. This ADC has two basic capabilities

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

It is possible to configure the XADC to do an averaging of consecutive 16, 64, or 256 samples (see function [XSysMon_SetAvg](https://github.com/Xilinx/embeddedsw/blob/5688620af40994a0012ef5db3c873e1de3f20e9f/XilinxProcessorIPLib/drivers/sysmon/src/xsysmon.c#L488)). I.e., to do the oversampling. The 4 least significant bits are then used to represent the averaged value with enhanced precision, i.e., the whole 16 bits of a status register can be used.  
Obviously, letting the XADC do the averaging makes sense for slowly changing input signals where noise is expected to be removed by the averaging. I show a practical example of the effect of averaging in the [Measurement precision—a practical example](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP?#measurement-precisiona-practical-example) chapter of this tutorial.

### Clocking, sampling rate, and bandwidth

The XADC is driven by the input clock DCLK.  
When the XADC Wizard is configured to have an AIX4Lite interface (which is what we will do), the DCLK is driven by the s_axi_aclk clock of the AXI interface.

The ADC circuitry within the XADC is driven by the clock ADCCLK, which is derived from DCLK by a configurable ratio divider. The minimum possible divider ratio is 2, and the maximum ratio is 255.  
The divider ratio can be configured dynamically by the function [XSysMon_SetAdcClkDivisor()](https://github.com/Xilinx/embeddedsw/blob/5688620af40994a0012ef5db3c873e1de3f20e9f/XilinxProcessorIPLib/drivers/sysmon/src/xsysmon.c#L1089).

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

> [!IMPORTANT]
>
> XADC can run at 1 Msps, but that doesn't mean it's wise to run it at 1&nbsp;Msps in all circumstances. It very much depends on the circuitry before the XADC and the characteristics of the input signal.
> I will explore this topic in the following chapters.

> [!IMPORTANT]
>
> The guaranteed analog bandwidth of auxiliary channels is only 250 kHz.
> The Zynq-7000 SoC [Data Sheet DS187](https://docs.amd.com/v/u/en-US/ds187-XC7Z010-XC7Z020-Data-Sheet) states on page 68 that the Auxiliary Channel Full Resolution Bandwidth is 250 kHz.

You can sample an auxiliary channel at 1 Msps. However, signals with a frequency higher than 250 kHz are not guaranteed to be precisely recorded. This seems to be an analog bandwidth limitation of Zynq-7000 circuits related to auxiliary channels.

The data sheet doesn't mention the bandwidth of the dedicated analog input channel V<sub>P</sub>/V<sub>N</sub>. We can assume it is at least 500 kHz (i.e., the [Nyquist frequency](https://en.wikipedia.org/wiki/Nyquist_frequency) for 1 Msps ADC).

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
> In most cases, this is not true because you need an [anti-aliasing filter](https://en.wikipedia.org/wiki/Anti-aliasing_filter), i.e., a low pass filter, which will eliminate frequencies higher than the [Nyquist frequency](https://en.wikipedia.org/wiki/Nyquist_frequency) in the input signal.

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

We see that the analog input circuit on Cora Z7 consists of a voltage divider and a low-pass anti-aliasing filter (AAF).   
The voltage divider allows voltage up to 3.3 V to be connected to pins A0-A5. The voltage is reduced to the 1.0 V limit of the XADC.

- To be precise, an input voltage of 3.3 V  is reduced to 0.994 V. The exact value may vary depending on how much the resistors on your particular board deviate within the tolerances. 

The analog inputs A0-A5 act as single-ended inputs because negative signals VAUXN[] are tied to the board's ground.  
The low-pass filter formed by the circuit has a cut-off frequency of 94.6 kHz.

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

The factor $`1 \times 10^{-9}`$ is the capacitance of the AAF's capacitor.

Further details on how the equation was constructed can be found in the Application Note [XAPP795](https://docs.amd.com/v/u/en-US/xapp795-driving-xadc).

In theory, the settling time of 15.1725 μs allows for a 65.909 kHz sampling rate. In the later chapter, we will explore how the circuit behaves in practice and to which use cases the 65.909 kHz sampling rate applies well.

One "controversy" is already apparent: According to the [Nyquist theorem](https://www.techtarget.com/whatis/definition/Nyquist-Theorem), the 65.9 kHz sampling rate allows correct sampling of an input signal of frequency up to 33 kHz. However, the cut-off frequency of the AAF on the Cora Z7 unipolar input is 94.6 kHz. So, by running the XADC at 65.9 ksps, we risk that an imprecise digitalization of the so-called [alias](https://en.wikipedia.org/wiki/Aliasing) will happen if frequencies above 33 kHz are present in the input signal. We would need an AAF with a cutoff frequency of 33 kHz. However, such an AAF would have an even longer settling time.  
There is no universal solution for this issue. How you compromise between sampling rate and settling time depends on the characteristics of the input signal and on what you want to achieve by digitizing it.

> [!IMPORTANT]
>
> Please note that any additional resistance of circuitry you connect to the Cora Z7's pins A0-A5 can further increase the settling time needed.  
> To achieve a reliable measurement, the voltage source connected to pins A0-A5 must act as having low internal resistance.

### Settling time of dedicated channel V<sub>P</sub>/V<sub>N</sub> AAF of Cora Z7

The following picture is a copy of Figure 13.2.3 from the Cora Z7 [Reference Manual](https://digilent.com/reference/programmable-logic/cora-z7/reference-manual#shield_analog_io). It depicts the circuit used for the dedicated analog input channel V<sub>P</sub>/V<sub>N</sub> (the pins are labeled V_P and V_N on the Cora Z7 board):

<img src="pictures\cora-analog-dedicated.png"  width="400">

The Cora Z7 V<sub>P</sub>/V<sub>N</sub> channel can be used in both bipolar and unipolar modes.

- Note: The Cora Z7 also provides pins labeled A6-A11, which can be used as differential auxiliary channels. See the Cora Z7 [Reference Manual](https://digilent.com/reference/programmable-logic/cora-z7/reference-manual#shield_analog_io) for details.

> [!CAUTION]
>
> The dedicated analog input channel on Cora Z7 is less protected than auxiliary channels A0-A5. There is no voltage divider. Therefore, both V<sub>P</sub> and V<sub>N</sub> must always be within a range from 0 V to 1.0 V with respect to the board's GND. Also, the differential V<sub>P</sub> &minus; V<sub>N</sub> must be within the range of ±0.5V.

The capacitor and the resistors form a low-pass filter with a cutoff frequency of 568.7 kHz (which is close to the 500 kHz [Nyquist frequency](https://en.wikipedia.org/wiki/Nyquist_frequency) of the XADC running at 1 Msps).

We can calculate the settling time of this circuit as follows:

```math
t_{settling} = \ln(2^{12+1}) \times ( 140 + 140 ) \times 1 \times 10^{-9} = 2.52 \mskip3mu \mu s
```

In theory, the settling time of 2.52 μs allows for a 396.3 kHz sampling rate. In the next chapter, we will explore how the circuit behaves in practice and to which use cases the 396.3 kHz sampling rate applies well.

## Acquisition and settling time—the practice

### The behavior of unipolar auxiliary channel AAF of Cora Z7

Let's see what the low-pass AAF does to a signal.  
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

I mentioned before that the settling time of 15.17 μs allows for a 65.9 kHz theoretical sampling rate. This is what [XAPP795](https://docs.amd.com/v/u/en-US/xapp795-driving-xadc) says. Let's see in the following figure what would happen if I mindlessly followed XAPP795's advice and used 65.9 ksps instead of 1 Msps.

<img src="pictures\Cora_Z7_stair_signal_reading_simulation.png">

We see that at 65.9 ksps, the digitized signal looks nothing like the input signal. This is not a helpful result.  
At 1 Msps, we could apply some digital processing, e.g., identify local maxima and minima of the signal and thus get some understanding of the characteristics of the square wave signal on the input. That is not a possibility at 65.9 ksps.

We saw that a square wave signal is a challenge for the XADC. What about something more reasonable, for example, an 8 kHz sine signal? The following figure shows the effect of Cora Z7 auxiliary channel AAF on such a signal.

<img src="pictures\Cora_Z7_sine_signal_simulation.png">

8 kHz is very well below the 94.6 kHz cutoff frequency of the AAF, so we see only a very minor impact of the filter on the output signal. There is very slight attenuation and a small phase shift (i.e., delay of the output signal as compared to the input).

The next figure shows an example of what the 65.9 ksps digitization of the 8 kHz signal may look like.

<img src="pictures\Cora_Z7_sine_signal_reading_simulation.png">

At 65.9 ksps, the shape of the signal is generally well-recorded. You may miss the exact local maxima and minima, though. If you needed to calculate the [RMS](https://en.wikipedia.org/wiki/Root_mean_square) of the input signal, it would be better to use a higher sample rate, ideally 1 Msps, to get a more precise approximation of the signal curve and, thus, a more accurate RMS. 

#### The bottom line

The settling time, which you can calculate using formulas in the Application Note [XAPP795](https://docs.amd.com/v/u/en-US/xapp795-driving-xadc) and the User Guide [UG480](https://docs.amd.com/r/en-US/ug480_7Series_XADC/External-Analog-Inputs), tells you how long it takes for the output signal to settle to a new value (for the 12-bit digitization precision) when the input signal undergoes a <ins>step change</ins> to a new value. Nothing else!

Knowing the settling time is crucial in cases when the input voltage undergoes sudden changes, such as when a [multiplexer](https://en.wikipedia.org/wiki/Multiplexer) is used. When the input signal is switched to a new source, you must wait at least the settling time before taking a sample by the XADC.

When you measure slowly changing signals (e.g., a voltage from a temperature sensor) and are not very concerned about noise in the input signal, it's probably enough to sample the input with the period equal to the settling time.

I think that in all other cases, the proper digitization setup depends on the circumstances.  You need to understand your objective and your input signal. And you definitely have to do a lot of testing.  
There will be cases when it's beneficial to sample a low-frequency signal with a high XADC sample rate to use some kind of averaging or other digital signal processing algorithm, e.g., to reduce the noise component of the input signal.

### The behavior of dedicated channel V<sub>P</sub>/V<sub>N</sub> AAF of Cora Z7

For completeness, let's look shortly also at a practical example using the dedicated analog input channel V<sub>P</sub>/V<sub>N</sub>.

The V<sub>P</sub>/V<sub>N</sub> channel's AAF cutoff frequency is pretty high at 568.7 kHz. Let's see in the following figure what happens when we feed the inputs with two 50 kHz sine waves of opposing phases and measure the output differentially.

<img src="pictures\Cora_Z7_diff_signal_simulation.png">

There are no surprises. The 50 kHz signal is negligibly attenuated by the AAF, and there is a very slight phase shift in the output.

I reproduced the scenario on a physical Cora Z7 board and measured the signal by the XADC configured to the sample rate of 1 Msps using the [software app](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/tree/main/sources/XADC_tutorial_app) shared in this repository. The resulting measurement shown in the following figure is exactly as expected. A 50 kHz differential is captured by the XADC with good precision.

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

Use the following call (after you initialize the XADC instance) when the internal voltage reference is used to enable only the Offset Calibration Coefficient used for XADC and Zynq power supply measurements:

`XSysMon_SetCalibEnables(&XADCInstance, XSM_CFR1_CAL_ADC_OFFSET_MASK | XSM_CFR1_CAL_PS_OFFSET_MASK);`

When the  Zynq-7000 board you are using provides an external voltage reference, then use the following code to enable both Gain and Offset Calibration Coefficients used for XADC and Zynq power supply measurements:

`XSysMon_SetCalibEnables(&XADCInstance, XSM_CFR1_CAL_ADC_GAIN_OFFSET_MASK | XSM_CFR1_CAL_PS_GAIN_OFFSET_MASK);`

When the Zynq-7000 internal voltage reference is used for the calibration, the calibration of XADC gain is actually not done (only the XADC offset is calibrated). As explained [here](https://adaptivesupport.amd.com/s/article/53586?language=en_US), the value of the Gain Calibration Coefficient is set to 0x007F in such a case. 0x007F represents the maximum Gain Coefficient of 6.3%. Obviously, letting XADC apply this maximal Gain Coefficient would significantly reduce the precision of digitalization. That is why we need to pay attention to what kind of voltage reference our Zynq-7000 board uses.

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

I connected a voltage reference to the unipolar auxiliary channel VAUX[1] of my Cora Z7. My freshly calibrated 6½ digits multimeter measured the reference as 2.495 V.  
I let the XADC digitize 6400 samples at 1 Msps using the [software app](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/tree/main/sources/XADC_tutorial_app) shared in this repository. The mean value of the 6400 samples was 2.498 V.

Only a 3 mV difference between a Cora Z7 and an expensive digital multimeter is an excellent result! Mind that the Cora Z7 board doesn't provide an external voltage reference for XADC calibration and that two resistors forming a voltage divider are placed before the VAUX[1] input, i.e., tolerances of the resistors are at play here.  
Doing the averaging is no "cheating." Every high-precision voltmeter does the same.

The following figure shows the first 800 samples of my XADC measurement.

<img src="pictures\Cora_Z7_2.495V_reading.png" alt="Zynq-7000 XADC constant signal digitization">

We see that the noise makes most of the samples oscillate in an interval of 8 bits (8 values) of the 12-bit measurements. One bit represents a 0.81 mV change in the measured voltage.

I mentioned earlier that the XADC can be configured to do averaging of samples. It set 64-sample averaging by calling `XSysMon_SetAvg(&XADCInstance, XSM_AVG_64_SAMPLES);` and captured 100 samples shown in the next figure. 

Zynq-7000 XADC constant signal digitization with averaging

<img src="pictures\Cora_Z7_2.495V_64avg_reading.png" alt="Zynq-7000 XADC constant signal digitization with averaging">

The signal looks much cleaner now. The basic sample rate of the XADC is still 1 Msps, but it averages 64 samples before it produces one sample as the output. Therefore, the apparent sample rate is 15.6 ksps ( $`1000/64\dot{=}15.6`$ ). The 100 data points shown in the figure are the result of 6400 samples done by the XADC.  
The output of XADC's averaging is a 16-bit value, so we see much finer differences between the data points compared to raw 12-bit samples in the previous figure.

Of course, you can achieve 64-sample averaging (or any other type of averaging) by post-processing raw 12-bit samples with the PS code or PL logic. Nevertheless, 16, 64, or 256-sample averaging, which the XADC is able to do internally, can save you the coding effort.

## DMA (Direct Memory Access)

I think the most practical way to transfer large amounts of samples from the XADC for processing in the PS is by means of DMA (Direct Memory Access). This is achieved by including the [AXI Direct Memory Access IP](https://www.xilinx.com/products/intellectual-property/axi_dma.html) in the HW design.

<img src="pictures\bd_axi_dma_ip.png" width="300">

The "magic" of the AXI DMA is that it gets data from the [AXI-Stream](https://docs.amd.com/r/en-US/ug1399-vitis-hls/How-AXI4-Stream-Works) input interface S_AXIX_S2MM and sends them via output AXI interface M_AXI_S2MM to a memory address. If the M_AXI_S2MM is properly connected (as I will show later in this tutorial), the data are loaded directly into the RAM without Zynq-7000 ARM cores being involved. (The S_AXI_LITE interface is used to control the AXI DMA by functions from [xaxidma.h](https://xilinx.github.io/embeddedsw.github.io/axidma/doc/html/api/xaxidma_8h.html).)  
In essence, you call something like `XAxiDma_SimpleTransfer( &AxiDmaInstance, (UINTPTR)DataBuffer, DATA_SIZE, XAXIDMA_DEVICE_TO_DMA );` in the PS code and wait till the data appears in the `DataBuffer` (I will explain all the details in a later chapter).

The maximum amount of data that AXI DMA can move in a single transfer is 64 MB (exactly 0x3FFFFFF bytes). This is because the AXI DMA register to store buffer length can be, at most, 26 bits wide.  
In our case, we will be transferring 16-bit values, i.e., we can transfer at most 33,554,431 samples in one go. That should be more than enough. We could record up to 33.6 seconds of the input signal with the XADC running at 1 Msps.

The [XADC Wizard IP](https://www.xilinx.com/products/intellectual-property/xadc-wizard.html) can be configured to have an output AXI-Stream interface. When you configure the XADC for continuous sampling, you will get the actual stream of data coming out from the XADC Wizard AXI-Stream interface. However, this data stream is not ready to be connected directly to the AXI DMA.  
The thing is that the AXI-Stream interface on the XADC Wizard doesn't contain an AXI-Stream signal TLAST. This signal is asserted to indicate the end of the data stream. The AXI DMA must receive the TLAST signal to know when to stop the DMA transfer.

Therefore, we need an intermediate PL module to handle the AXI-Stream data between the XADC Wizard and the DMA IP. I wrote a module [stream_tlaster.v](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/blob/main/sources/HDL/stream_tlaster.v) for use in this tutorial.

<img src="pictures\bd_tlaster.png" width="200">

This Verilog module controls when the data from the slave AXI-Stream interface (connected to the XADC Wizard) starts to be sent to the master AXI-Stream interface (connected to the AXI DMA).  This happens when the input signal `start` is asserted.
The module also controls how many data transfers are made (the input signal `count` defines this) and asserts the TLAST signal of the m_axis interface on the last transfer.

We will control the input signals `start` and `count` from the PS (will connect them to the GPIO). First, we set the `count`, then call `XAxiDma_SimpleTransfer()`, and lastly, assert the `start` signal so the data starts to flow into the AXI DMA and thus into the RAM. This will ensure our complete control of how many data samples are transferred from the XADC into the RAM.

## Hardware design in Vivado

Note: I'm using the board Digilent [Cora Z7-07S](https://digilent.com/reference/programmable-logic/cora-z7/start) in this tutorial. However, most of the steps are valid also for any other Zynq-7000 board.  
I'm using Vivado 2024.1. Nevertheless, the same steps work also in Vivado 2023.1.

Make sure you have Digilent board files installed. [This article](https://digilent.com/reference/programmable-logic/guides/install-board-files) provides instructions on how to install them.  

- In short: Download the most recent [Master Branch ZIP Archive](https://github.com/Digilent/vivado-boards/archive/master.zip), open it, and extract the content of folder \vivado-boards-master\new\board_files into c:\Xilinx\Vivado\2024.1\data\boards\board_files\\. You may need to create the folder board_files at the destination.

### Constraints

Create a new RTL Project in Vivado 2024.1. Select your version of Cora Z7 from the list of boards.

Let's first set the constraints.  
Download [Cora-Z7-07S-Master.xdc](https://github.com/Digilent/digilent-xdc/blob/master/Cora-Z7-07S-Master.xdc) from the [Digilent GitHub repository](https://github.com/Digilent/digilent-xdc/) and add it as a constraints source file to the project.

We will use the two buttons, the dedicated analog input v_p/v_n (labeled V_P/V_N on the board) and the auxiliary input vaux1_p/vaux1_n (labeled A0 on the board; the vaux1_n is connected to ground, it doesn't have a pin on the board).  
Uncomment these items in the constraints file.

```
## Buttons
set_property -dict { PACKAGE_PIN D20   IOSTANDARD LVCMOS33 } [get_ports { btn[0] }]; #IO_L4N_T0_35 Sch=btn[0]
set_property -dict { PACKAGE_PIN D19   IOSTANDARD LVCMOS33 } [get_ports { btn[1] }]; #IO_L4P_T0_35 Sch=btn[1]

## Dedicated Analog Inputs
set_property -dict { PACKAGE_PIN K9    IOSTANDARD LVCMOS33 } [get_ports { v_p }]; #VP_0 Sch=xadc_v_p
set_property -dict { PACKAGE_PIN L10   IOSTANDARD LVCMOS33 } [get_ports { v_n }]; #VN_0 Sch=xadc_v_n

## ChipKit Outer Analog Header - as Single-Ended Analog Inputs
## NOTE: These ports can be used as single-ended analog inputs with voltages from 0-3.3V (ChipKit analog pins A0-A5) or as digital I/O.
## WARNING: Do not use both sets of constraints at the same time!
set_property -dict { PACKAGE_PIN E17   IOSTANDARD LVCMOS33 } [get_ports { vaux1_p }]; #IO_L3P_T0_DQS_AD1P_35 Sch=ck_an_p[0]
set_property -dict { PACKAGE_PIN D18   IOSTANDARD LVCMOS33 } [get_ports { vaux1_n }]; #IO_L3N_T0_DQS_AD1N_35 Sch=ck_an_n[0]
```

### Zynq Processing System

Create the block design.  
Add the ZYNQ7 Processing System to the diagram. Vivado offers to run the block automation. Run it. DDR and FIXED_IO signals will be connected to the Zynq PS.

We need to configure the Zynq PS for our needs.  
Enable Slave AXI High-Performance interface 0. This is the interface AXI DMA will be connected to.

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
Set the frequency of clk_out1 to 104 MHz. This frequency will allow us to run the XADC at 1 Msps. We will use a clock divider equal to 4, which gives ADCCLK of 26 MHz, which translates to the sampling rate of 1 Msps. See detailed explanation in the chapter [Clocking, sampling rate, and bandwidth](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP?#clocking-sampling-rate-and-bandwidth).  
If you later want to experiment with slower XADC sampling rates, you can set the Clocing Wizard's output frequency and the clock divider differently so you achieve the desired sampling rate.

Set the Reset Type of the Clocking Wizzard to Active Low (the setting is on the Output Clocks tab)  
Connect the reset signal of the Clocking Wizard with the FCLK_RESET0_N of the Zynq PS.

It's time for the XADC Wizard now. Add it to the diagram and open its configuration.  
We will control the XADC from PS using functions from [xsysmon.h](https://github.com/Xilinx/embeddedsw/blob/master/XilinxProcessorIPLib/drivers/sysmon/src/xsysmon.h). Therefore, we will only do a bare minimum configuration of the XADC Wizard. 

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
Set all clocking sources to the Clocking Wizard (/clk_wiz_0/clk_out1). As explained earlier, we want to drive the XADC clock from the Clocking Wizard to have the flexibility of choosing the frequency.

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
We are only writing to RAM so we disable the option Enable Read Channel.  
We enable the Allow Unaligned Transfer option on the Write Channel and increase the Max Burst Size to 128.

<img src="pictures\bd_dma.png" width="650">














bkla

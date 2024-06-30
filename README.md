# Tutorial: Xilinx Zynq XADC using DMA and network streaming
This tutorial shows how to do a HW design and code a SW application to make use of AMD Xilinx Zynq 7000 [XADC](https://www.xilinx.com/products/technology/analog-mixed-signal.html). We will also see how to use the [DMA](https://www.xilinx.com/products/intellectual-property/axi_dma.html) to transfer data from the XADC into Zynq CPU's memory and stream data to a remote server over the network.

In this tutorial, I'm using the Digilent board [Cora Z7-07S](https://digilent.com/shop/cora-z7-zynq-7000-single-core-for-arm-fpga-soc-development/). However, all the principles described here can be used on any other Zynq 7000 board. I will highlight aspects specific to Cora Z7 in the text.  
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

## A short introduction to Zynq 7000 XADC

The XADC is a feature of an analog-to-digital converter integrated on selected Xilinx FPGA chips, including Zynq 7000. This ADC has two basic capabilities

1.  The System Monitor (SYSMON) reads the Zynq chip temperature and voltages of various Zynq power rails.
1.  The XADC reads voltages from external inputs, which are called channels.

In this tutorial, we will focus solely on XADC. But don't be confused, Xilinx library functions for controlling XADC are defined in [xsysmon.h](https://github.com/Xilinx/embeddedsw/blob/master/XilinxProcessorIPLib/drivers/sysmon/src/xsysmon.h).

XADC can read one external input (channel) at a time and provides a means for switching between channels.  
The Zynq 7000 XADC has one dedicated analog input channel called V<sub>P</sub> /V<sub>N</sub> and 16 so-called auxiliary channels named VAUX[0..15].
Each channel has two input signals because they are differential input channels. A positive differential input is denoted V<sub>P</sub> or VAUXP, and a negative one is denoted V<sub>N</sub> or VAUXN.

> [!WARNING]
>
> All input voltages must be positive with respect to the analog ground (GNDADC) of the Xilinx chip.  
> I.e., V<sub>N</sub> can't go below GNDADC even though it's called "negative input".

A channel may operate in unipolar or bipolar mode.

#### Unipolar mode

- The differential analog inputs (V<sub>P</sub> and V<sub>N</sub>) have an input range of 0 V to 1.0 V.
- The voltage on V<sub>P</sub> (measured with respect to V<sub>N</sub>) must always be positive. I.e., the XADC output is a value between 0 V and 1.0 V
- V<sub>N</sub> is typically connected to a local ground or common mode signal.

#### Bipolar mode

- This mode can accommodate input signals driven from a true differential source.
- The differential analog input (V<sub>P</sub> &minus; V<sub>N</sub>) can have a maximum input range of ±0.5V. I.e., the XADC output has a value between &minus;0.5 V and 0.5 V.
- However, both V<sub>P</sub> and V<sub>N</sub> must be always within a range from 0 V to 1.0 V with respect to GNDADC.

See the chapter [Analog Inputs](https://docs.amd.com/r/en-US/ug480_7Series_XADC/Analog-Inputs) of [UG480](https://docs.amd.com/r/en-US/ug480_7Series_XADC) for more details on unipolar and bipolar inputs.

**TODO: number of bits in output reg**

For using the XADC you need to instantiate an [XADC Wizard IP](https://www.xilinx.com/products/intellectual-property/xadc-wizard.html) in your HW design.  
If you don't need to modify the XADC configuration during runtime, you can do all the needed setup in the XADC Wizzard IP configuration.  
Alternatively, you can configure XADC by calling functions defined in [xsysmon.h](https://github.com/Xilinx/embeddedsw/blob/master/XilinxProcessorIPLib/drivers/sysmon/src/xsysmon.h). This allows you to change the configuration during runtime (e.g., switching between the channels). We will use this method of configuration in this tutorial.

The Zynq 7000 XADC can run in several operating modes, see the [relevant chapter](https://docs.amd.com/r/en-US/ug480_7Series_XADC/XADC-Operating-Modes) of the [UG480](https://docs.amd.com/r/en-US/ug480_7Series_XADC).  
In this tutorial, we will use the simplest one, the Single Channel Mode.  
We will also configure the XADC for Continuous Sampling. In this timing mode, the XADC performs the conversions one after another, generating a stream of data that we will process through the DMA.

## Acquisition time

> [!IMPORTANT]
>
> The XADC's rated maximum sampling frequency is 1 Msps. But it doesn't mean that you can run the XADC on 1&nbsp;Msps in all circumstances!
>
> The XADC sampling frequency must be carefully determined to allow sufficient acquisition time, given the properties of the circuit you are using. It requires a bit of math, as we explain in this chapter.

The principle of XADC operation is charging an internal capacitor to a voltage equal to the voltage of the analog input being measured. Any electrical resistance between the input voltage and the internal capacitor will, of course, slow down the charging of the capacitor.  
If you don't give the internal XADC capacitor enough time to charge, the input voltage determined by the XADC will be lower than the actual voltage of the input.

The next picture is a copy of [Figure 2-5](https://docs.amd.com/r/qOeib0vlzXa1isUAfuFzOQ/Jknshmzrw3DvMZgWJO73KQ?section=XREF_26771_X_Ref_Target) from Zynq 7000 XADC User Guide [UG480](https://docs.amd.com/r/en-US/ug480_7Series_XADC), chapter "Analog Input Description" (page 22 of the PDF version of UG480).

<img src="pictures\UG480_fig_2-5.png" title=""  width="650">

We see in the picture that in the unipolar mode the current to the capacitor goes through two internal resistances R<sub>MUX</sub>. In bipolar mode, two capacitors are used, and the current into them goes through a single internal resistance R<sub>MUX</sub>.  
R<sub>MUX</sub> is the resistance of the analog multiplexer circuit inside the Zynq XADC. Please note that the value of R<sub>MUX</sub> for a dedicated analog input is different from the R<sub>MUX</sub> of the auxiliary inputs.  

Xilinx is giving us [Equation 2-2](https://docs.amd.com/r/qOeib0vlzXa1isUAfuFzOQ/Jknshmzrw3DvMZgWJO73KQ?section=XREF_62490_Equation2_2) in the [UG480](https://docs.amd.com/r/en-US/ug480_7Series_XADC/Analog-Inputs) for calculating minimum acquisition time in unipolar mode:
```math
t_{ACQ} = 9 \times ( R_{MUX} + R_{MUX} ) \times C_{SAMPLE}
```
R<sub>MUX</sub> for an auxiliary input is 10 kΩ.  
C<sub>SAMPLE</sub> is specified by Xilinx as 3 pF.  
Therefore, we calculate the minimum acquisition time t<sub>ACQ</sub> for an unipolar auxiliary input as follows:

```math
t_{ACQ} = 9 \times ( 10000 + 10000 ) \times 3 \times 10^{-12} = 540 \mskip3mu ns
```
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
> The calculation of the acquisition times we did above is valid only for an ideal case when the only resistance present in the circuit is the resistance of the internal analog multiplexer of the Zynq XADC.
>
> In the next chapters, we will see a real-life example of calculating acquisition times for the development board  [Cora Z7-07S](https://digilent.com/shop/cora-z7-zynq-7000-single-core-for-arm-fpga-soc-development/), which has additional resistances in the circuitry outside the Zynq XADC. 

To understand how the calculated acquisition time translates to XADC configuration, we need to see how the XADC works in terms of input clock and timing.

**TODO**  
**How the input clock DCLK translates to ADCCLK.**

In the default XADC setup of Continuous Sampling mode, 26 ADCCLK cycles are required to acquire an analog signal and perform a conversion.

The first 4 ADCCLK cycles are the so-called settling period, and they are followed by 22 ADCCLK cycles of the so-called conversion phase, during which the XADC does the conversion and generates the output value.  
XADC can be configured to extend the settling period to 10 ADCCLK cycles (thus resulting in a total of 32 ADCCLK cycles for acquisition and conversion).

Charging of the internal capacitor starts at the beginning of the conversion phase. I.e., the XADC does the conversion in parallel with sampling input voltage for the next conversion.  
This is possible because the XADC has a separate track-and-hold amplifier (T/H). Thus, when the XADC starts to convert an input voltage, the T/H is free to start charging to the next voltage to be converted.

The minimum acquisition time must fit within ADCCLK cycles of the settling and conversion phase. I.e., within 26 or 32 ADCCLK cycles.  
Therefore, we must ensure that the ADCCLK frequency is set so that 26 or 32 ADCCLK cycles are at least acquisition time t<sub>ACQ</sub> long.

This is not a problem for acquisition times of unipolar auxiliary input (540 ns) and bipolar dedicated input (2.7 ns), which we calculated using [Equation 2-1](https://docs.amd.com/r/qOeib0vlzXa1isUAfuFzOQ/Jknshmzrw3DvMZgWJO73KQ?section=XREF_35025_Equation2_1) or [Equation 2-2](https://docs.amd.com/r/qOeib0vlzXa1isUAfuFzOQ/Jknshmzrw3DvMZgWJO73KQ?section=XREF_62490_Equation2_2).

The maximum possible ADCLK frequency is 26 MHz. 26 clocks of this frequency take 1 &mu;s, which is more than the needed acquisition time. In this case, we could run the XADC at the maximum sampling rate of 1&nbsp;Msps.  
Please note that this may not be the case in circuits with higher resistances in the path of analog inputs.  
**TODO example of higher resistance:** [link](https://docs.amd.com/r/en-US/ug480_7Series_XADC/External-Analog-Inputs)


### Unipolar input acquisition time of Cora Z7

Let's analyze the acquisition time of unipolar analog input circuitry of the Digilent [Cora Z7](https://digilent.com/shop/cora-z7-zynq-7000-single-core-for-arm-fpga-soc-development/) development board.

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
The analog inputs A0-A5 act as single-ended inputs because negative signals VAUXN[] are tied to the board's ground.  
The low-pass filter formed by the circuit has a cut-off frequency of 134 kHz.

This circuit on Cora Z7 is basically the same as the one discussed in the Application Guidelines chapter [External Analog Inputs](https://docs.amd.com/r/en-US/ug480_7Series_XADC/External-Analog-Inputs) of UG480.  
The AAF contains a 1 nF capacitor, which is orders of magnitude larger capacitance than the 3 pF sampling capacitor inside the XADC. Therefore, we can ignore the XADC sampling capacitor when determining the acquisition time.

We need to determine the so-called settling time of the AAF circuit, which is the needed acquisition time for the XADC. 

We can use a slightly modified [Equation 6-1](https://docs.amd.com/r/qOeib0vlzXa1isUAfuFzOQ/8erAzNpWEDQ8zWWH_EdtFg?section=XREF_11532_Equation2_5) from [UG480](https://docs.amd.com/r/en-US/ug480_7Series_XADC/External-Analog-Inputs) to adapt it to the impedances of Cora Z7 analog input:

```math
t_{settling} = \ln(2^{12+1}) \times ( {{2320 \times 1000} \over {2300 + 1000 }} + 140 + 845) \times 1 \times 10^{-9} = 15.17 \mskip3mu \mu s
```
The term $\`\mu$\`
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

### Bipolar input acquisition time of Cora Z7

Equation 2-1 from UG480 for acquisition time in unipolar mode:
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
Therefore, the resulting sampling rate will be 917 ksps (a single conversion cycle will take 26 ADCLKs).

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

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
$C_{SAMPLE}$ C<sub>SAMPLE</sub> is specified by Xilinx as 3 pF.  
Therefore, we calculate the minimum acquisition time t<sub>ACQ</sub> for an unipolar auxiliary input as follows:

```math
t_{ACQ} = 9 \times ( 10000 + 10000 ) \times 3 \times 10^{-12} = 540\mskip3muns
```
For minimum acquisition time in bipolar mode, Xilinx is giving the [Equation 2-1](https://docs.amd.com/r/qOeib0vlzXa1isUAfuFzOQ/Jknshmzrw3DvMZgWJO73KQ?section=XREF_35025_Equation2_1) in the [UG480](https://docs.amd.com/r/en-US/ug480_7Series_XADC/Analog-Inputs):
```math
t_{ACQ} = 9 \times R_{MUX} \times C_{SAMPLE}
```
For a dedicated analog input, R<sub>MUX</sub> equals 100 Ω. This gives us the following value of the minimum acquisition time of a bipolar dedicated input:
```math
t_{ACQ} = 9 \times 100 \times 3 \times 10^{-12} = 2.7\mskip3muns
```
> [!IMPORTANT]
>
> The calculation of the acquisition times we did above is valid only for an ideal case when the only resistance present in the circuit is the resistance of the internal analog multiplexer of the Zynq XADC.
>
> In the next chapters, we will see a real-life example of calculating acquisition times for the development board  [Cora Z7-07S](https://digilent.com/shop/cora-z7-zynq-7000-single-core-for-arm-fpga-soc-development/), which has additional resistances in the circuitry outside the Zynq XADC. 

To understand how the calculated acquisition time translates to XADC configuration we need to see how the XADC works in terms of input clock and timing.

**TODO**  
**How the input clock DCLK translates to ADCCLK.**

In the default XADC setup of Continuous Sampling mode, 26 ADCCLK cycles are required to acquire an analog signal and perform a conversion.

The first 4 ADCCLK cycles are the so-called settling period during which the internal capacitor of XADC is being charged.  
XADC can be configured to extend the settling period to 10 ADCCLK cycles (thus resulting in a total of 32 ADCCLK cycles for acquisition and conversion).

After the settling period follows 22 ADCCLK cycles of the so-called conversion phase during which the XADC does the conversion and generates the output value.

The minimum acquisition time, which we calculated using [Equation 2-1](https://docs.amd.com/r/qOeib0vlzXa1isUAfuFzOQ/Jknshmzrw3DvMZgWJO73KQ?section=XREF_35025_Equation2_1) or [Equation 2-2](https://docs.amd.com/r/qOeib0vlzXa1isUAfuFzOQ/Jknshmzrw3DvMZgWJO73KQ?section=XREF_62490_Equation2_2) must fit into the settling period.  
Therefore, we must make sure to set the ADCCLK frequency in the way that 4 or 10 ADCCLK cycles are at least acquisition time t<sub>ACQ</sub> long.

Let's take the unipolar auxiliary input as an example:  
We determined the minimum acquisition time for an unipolar auxiliary input as 540 ns.  
To achieve the fastest possible sampling rate we will use the settling period of 10 ADCCLK cycles. We then calculate the ADCCLK frequency as
```math
f_{ADCCLK} ={ 1 \over {540 \times 10^{-9} \over 10} } = 18.519\mskip3muMhz
```
this will give us the sampling rate
```math
f_S ={ 1 \over { {1 \over f_{ADCCLK}} \times 32} } = 578.7\mskip3muksps
```
Please note that f<sub>ADCCLK</sub> and f<sub>S</sub> we calculated here are theoretical values. We probably won't be able to achieve f<sub>ADCCLK</sub> of exactly 18.519 MHz in the actual HW design. The Clocking Wizard IP can't generate any frequency we want, nevertheless, it can generate a frequency close to the desired value. We just need to make sure that the f<sub>ADCCLK</sub> in the actual HW design is <ins>lower or equal</ins> to the theoretical value calculated by a formula. 


### Unipolar input acquisition time of Cora Z7

In the case of Cora Z7, we need to take into account the unipolar input circuitry on the board as depicted in Figure 13.2.1 from the Cora Z7 [Reference Manual](https://digilent.com/reference/programmable-logic/cora-z7/reference-manual#shield_analog_io):

<img src="pictures\cora-analog-single-ended.png" title=""  width="550">

In our case, $R_{MUX}$ equals to 10 kΩ because we are using the auxiliary input VAUX1 (which is connected to pin A0 on the Cora Z7 board).  
In addition to $R_{MUX}$, we must include resistors in the signal path on the Cora Z7 board: 2.32&nbsp;kΩ, 140&nbsp;Ω, and 845&nbsp;Ω.  
$C_{SAMPLE}$ is specified by Xilinx as 3 pF.

We now calculate the needed acquisition time for VAUX1 as follows:
```math
t_{ACQ} = 9 \times ( 10000 + 10000 + 2320 + 140 + 845 ) \times 3 \times 10^{-12} = 629\mskip3muns
```
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

# Demo of Zynq XADC using DMA and data sent over the network
tbd

[Real Digital Signal Processing - Hackster.io](https://www.hackster.io/adam-taylor/real-digital-signal-processing-0bea44)

[Signal Processing with XADC and PYNQ - Hackster.io](https://www.hackster.io/adam-taylor/signal-processing-with-xadc-and-pynq-3c716c)

tbd

### Acquisition time

See Zynq 7000 XADC User Guide [UG480](https://docs.amd.com/r/en-US/ug480_7Series_XADC), chapter "Analog Input Description" (page 22 of the PDF version of UG480)

This is copy of [Figure 2-5](https://docs.amd.com/r/qOeib0vlzXa1isUAfuFzOQ/Jknshmzrw3DvMZgWJO73KQ?section=XREF_26771_X_Ref_Target) from UG480:

<img src="pictures\UG480_fig_2-5.png" title=""  width="650">

#### Unipolar input 

Equation 2-2 for acquisition time in unipolar mode:
$$t_{ACQ} = 9 \times ( R_{MUX} + R_{MUX} ) \times C_{SAMPLE}$$

In the case of Cora Z7, we need to take into account the unipolar input circuitry on the board as depicted in Figure 13.2.1 from the Cora Z7 [Reference Manual](https://digilent.com/reference/programmable-logic/cora-z7/reference-manual#shield_analog_io):

<img src="pictures\cora-analog-single-ended.png" title=""  width="550">

In our case, $R_{MUX}$ equals to 10 kΩ because we are using the auxiliary input VAUX1 (which is connected to pin A0 on the Cora Z7 board).  
In addition to $R_{MUX}$, we must include resistors in the signal path on the Cora Z7 board: 2.32 kΩ, 140 Ω, and 845 Ω.  
$C_{SAMPLE}$ is specified by Xilinx as 3 pF.

We now calculate the needed acquisition time for VAUX1 as follows:
$$t_{ACQ} = 9 \times ( 10000 + 10000 + 2320 + 140 + 845 ) \times 3 \times 10^{-12} = 629\mskip3muns$$



#### Bipolar input 

Equation 2-1 from UG480 for acquisition time in unipolar mode:
$$t_{ACQ} = 9 \times R_{MUX} \times C_{SAMPLE}$$

On Cora Z7, we need to take into account the bipolar input circuitry for dedicated V_P/V_N input on the board, as depicted in Figure 13.2.3 from the Cora Z7 [Reference Manual](https://digilent.com/reference/programmable-logic/cora-z7/reference-manual#shield_analog_io):

<img src="pictures\cora-analog-dedicated.png"  width="400">

The $R_{MUX}$ for a dedicated analog input is 100 Ω.  
In addition to $R_{MUX}$, we must include the 140 Ω resistor in the signal path on the Cora Z7 board  
$C_{SAMPLE}$ is 3 pF.

We now calculate the needed acquisition time for V_P/V_N as follows:
$$t_{ACQ} = 9 \times ( 100 + 140) \times 3 \times 10^{-12} = 6.5\mskip3muns$$

TODO:  
This allows a sampling rate of 1 Msps because an ADCCLK frequency of 26 MHz and four ADCCLKs allowed for the acquisition, and we get 150 ns acquisition time, which is more than enough.


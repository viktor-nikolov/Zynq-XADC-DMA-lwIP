# Demo of Zynq XADC using DMA and data sent over the network
tbd

[Real Digital Signal Processing - Hackster.io](https://www.hackster.io/adam-taylor/real-digital-signal-processing-0bea44)

[Signal Processing with XADC and PYNQ - Hackster.io](https://www.hackster.io/adam-taylor/signal-processing-with-xadc-and-pynq-3c716c)

tbd

### Acquisition Time

See Zynq 7000 XADC User Guide [UG480](https://docs.amd.com/r/en-US/ug480_7Series_XADC), chapter "Analog Input Description" (page 22 of the PDF version of UG480)

This is copy of [Figure 2-5](https://docs.amd.com/r/qOeib0vlzXa1isUAfuFzOQ/Jknshmzrw3DvMZgWJO73KQ?section=XREF_26771_X_Ref_Target) from UG480:

<img src="pictures\UG480_fig_2-5.png" title=""  width="650">

Equation 2-2 for acquisition time in unipolar mode>
$$t_{ACQ} = 9 \times ( R_{MUX} + R_{MUX} ) \times C_{SAMPLE}$$

In the case of Cora Z7 we need to take into account circuitry on the board as depicted on this Figure 13.2.1 from the Cora Z7 [Reference Manual](https://digilent.com/reference/programmable-logic/cora-z7/reference-manual#shield_analog_io):

<img src="pictures\cora-analog-single-ended.png" title=""  width="550">

In our case $R_{MUX}$
$$
R_{MUX} =
$$

$$
t_{ACQ} = 9 \times ( R_{MUX} + R_{MUX} ) \times C_{SAMPLE}
$$
# Building the application in Vitis 2024.1.1

I based the tutorial on Vitis Classic 2024.1.1. In this readme, I'm explaining how to build the demo application in Vitis 2024.1.1 (a.k.a. Vitis Unified 2024).

## Add a timer to the HW design in Vivado

Vitis Unified requires a HW timer to be present in the design. Otherwise FreeRTOS application can't be built in Vitis Unified. Therefore, we must add a timer to our HW design.

Open the original HW design. Save it as a new project.

Enable Timer 0 in Zynq MIO Configuration.

<img src="..\..\pictures\bd_zynq_timer.png" title=""  width="550">

Generate bitstream and export the HW to an XSA file.

## Building the application in Vitis Unified

Create an empty folder and open it as a workspace in Vitis Unified.  
Create a platform using the XSA of the HW design, which contains the Zynq HW timer.  
Select freertos as the operating system.

Enable lwip220 in the BSP configuration.

<img src="..\..\pictures\unif_lwip.png" title=""  width="800">

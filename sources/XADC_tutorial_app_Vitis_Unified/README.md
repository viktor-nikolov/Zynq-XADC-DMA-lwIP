# Building the application in Vitis 2024.1.1

I based the tutorial on Vitis Classic 2024.1.1. Vitis Classic is my first choice because I'm familiar with and it is much less buggy than Vitis 2024.1.1 (a.k.a. Vitis Unified 2024).

Nevertheless, the demo application from this XADC tutorial can also be built in Vitis Unified. This readme explains how.

## Add a timer to the HW design in Vivado

Vitis Unified requires a HW timer to be present in the HW design. Otherwise, a FreeRTOS application can't be built in Vitis Unified. Therefore, we must add a timer to the Zynq PS configuration.

If you have the Digilent [Cora Z7-07S](https://digilent.com/reference/programmable-logic/cora-z7/start) board, you can use the HW export XSA file from the archive [XADC_tutorial_timer_hw_2024.1.1.xpr.zip](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/blob/main/project_files/XADC_tutorial_timer_hw_2024.1.1.xpr.zip) in the repository and skip this step.

Open the original HW design, which we created in the [HW design](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP#hardware-design-in-vivado) chapter of the [tutorial](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP). Save it as a new project.

Open the Zynq PS IP configuration.  
Enable Timer 0 in Zynq MIO Configuration.

<img src="..\..\pictures\bd_zynq_timer.png" title=""  width="550">

Generate bitstream and export the HW to an XSA file.

## Building the application in Vitis Unified

Create an empty folder and open it as a workspace in Vitis Unified.  
Create a platform using the XSA of the HW design, which contains the Zynq PS HW timer.  
Select freertos as the operating system.

Enable lwip220 in the BSP configuration.

<img src="..\..\pictures\unif_lwip.png" title="">

In the lwip220 library configuration, set lwip220_api_mode to SOCKET_API and lwip220_dhcp to true.

<img src="..\..\pictures\unif_lwip_config.png" title="">

Create an empty embedded application using the platform we just created.

Import source files [FileViaSocket.h](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/blob/main/sources/XADC_tutorial_app/FileViaSocket.h), [FileViaSocket.cpp](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/blob/main/sources/XADC_tutorial_app/FileViaSocket.cpp), [button_debounce.h](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/blob/main/sources/XADC_tutorial_app/button_debounce.h) and [button_debounce.cpp](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/blob/main/sources/XADC_tutorial_app/button_debounce.cpp) from the folder [XADC_tutorial_app](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/tree/main/sources/XADC_tutorial_app) into the application's src folder.

The files [main.cpp](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/blob/main/sources/XADC_tutorial_app_Vitis_Unified/main.cpp) and [network_thread.cpp](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/blob/main/sources/XADC_tutorial_app_Vitis_Unified/network_thread.cpp) needed slight updates for the Vitis Unified toolchain, so you must import them from the folder [XADC_tutorial_app_Vitis_Unified](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/tree/main/sources/XADC_tutorial_app_Vitis_Unified).

This [readme file](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/blob/main/sources/XADC_tutorial_app/README.md) explains details about the six source files and their authors.

- **Note:** Please ignore the "problems" that Vitis Unified reports in the FileViaSocket.cpp in the PROBLEMS tab. The FileViaSocket.cpp uses conditional compilation heavily, and the clang in the Vitis Unified is not able to handle it correctly. The GCC compiler will report no errors or warnings for this source file.

Open the main.cpp and set your server IP address (i.e., the IP address where the Python script [file_via_socket.py](https://github.com/viktor-nikolov/lwIP-file-via-socket/blob/main/file_via_socket.py) will be running ).

```c++
const std::string SERVER_ADDR( "192.168.44.10" ); // Specify your actual server IP address
```

Build the project; there should be no errors or warnings. 

Now, you can continue with the tutorial. Go to the chapter [How to use the application](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP?#how-to-use-the-application).

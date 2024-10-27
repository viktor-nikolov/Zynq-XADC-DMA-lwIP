# Building the application in Vitis 2024.1.1

I based the tutorial on Vitis Classic 2024.1.1. Vitis Classic is my first choice because I'm familiar with and it is much less buggy than Vitis 2024.1.1 (a.k.a. Vitis Unified 2024).

Nevertheless the demo application from this XADC tutorial can be built also in the Vitis Unified. This readme explains how.

## Add a timer to the HW design in Vivado

Vitis Unified requires a HW timer to be present in the design. Otherwise, a FreeRTOS application can't be built in Vitis Unified. Therefore, we must add a timer to our HW design.

If you have the Digilent Cora Z7-

Open the original HW design. Save it as a new project.

Enable Timer 0 in Zynq MIO Configuration.

<img src="..\..\pictures\bd_zynq_timer.png" title=""  width="550">

Generate bitstream and export the HW to an XSA file.

## Building the application in Vitis Unified

Create an empty folder and open it as a workspace in Vitis Unified.  
Create a platform using the XSA of the HW design, which contains the Zynq HW timer.  
Select freertos as the operating system.

Enable lwip220 in the BSP configuration.

<img src="..\..\pictures\unif_lwip.png" title="">

In the lwip220 library configuration, set lwip220_api_mode to SOCKET_API and lwip220_dhcp to true.

<img src="..\..\pictures\unif_lwip_config.png" title="">

Create an empty embedded application using the platform we just created.  
**TODO** Import files from the folder [XADC_tutorial_app](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/tree/main/sources/XADC_tutorial_app) into the application's src folder.

Open the main.cpp and set your server IP address (i.e., the IP address where the Python script [file_via_socket.py](https://github.com/viktor-nikolov/lwIP-file-via-socket/blob/main/file_via_socket.py) will be running ).

```c++
const std::string SERVER_ADDR( "192.168.44.10" ); // Specify your actual server IP address
```

TODO Note: Ignore problems on the FileViaSocket.cpp, clang is not handling it correctly.

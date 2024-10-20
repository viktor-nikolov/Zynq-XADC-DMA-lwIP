## Project files

The repository's folder [project_files](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/tree/main/project_files) provides the following files:

- [XADC_tutorial_hw_2024.1.1.xpr.zip](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/blob/main/project_files/XADC_tutorial_hw_2024.1.1.xpr.zip)
  - Contains the HW design project export from Vivado 2024.1.1.
  - This is the HW design for the Digilent [Cora Z7-07S](https://digilent.com/reference/programmable-logic/cora-z7/start) board, which we created in the chapter [Hardware design in Vivado](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP?#hardware-design-in-vivado).
  - To use the export, simply extract the .zip into a folder and open XADC_tutorial_hw.xpr in Vivado.
  - The archive contains exported HW, the file system_wrapper.xsa, which can be directly used in Vitis 2024.1.1 for creating the platform.
  - Note: To have Vivado version 2024.1.1, you must install "Vivado™ Edition **Update 1** - 2024.1 Product Update" on top of "Vivado™ Edition - 2024.1 Full Product Installation". See the Xilinx [download page](https://www.xilinx.com/support/download.html).

- [vitis_export_archive.ide_Classic_2024.1.1.zip](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/blob/main/project_files/vitis_export_archive.ide_Classic_2024.1.1.zip)
  - Contains the SW project export from Vitis Classic 2024.1.1.
  - This is the XADC demo application for the Digilent [Cora Z7-07S](https://digilent.com/reference/programmable-logic/cora-z7/start) board, which I described in the chapter [Software](https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP/tree/main?#software).
  - To use the export, create an empty folder on your PC and open it as a workspace in Vitis Classic 2024.1.1.  
    Then select File|Import|"Vitis project exported zip file". Select the archive file, and select all projects in the archive.
  - Note: To have Vitis version 2024.1.1, you must install "Vivado™ Edition **Update 1** - 2024.1 Product Update" on top of "Vivado™ Edition - 2024.1 Full Product Installation". See the Xilinx [download page](https://www.xilinx.com/support/download.html).

<img src="..\pictures\vt_import.png" width="450">
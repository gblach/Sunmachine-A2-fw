# Sunmachine-A2-fw
Firmware for the [Sunmachine-A2-pcb](https://github.com/gblach/Sunmachine-A2-pcb) board. It allows to control WS2812, SK6812 or similar LED strips via Bluetooth LE. It supports both RGB and WWA LEDs.

## Installing pre-built firmware
Pre-built firmware is saved in [Sunmachine-A2-fw.hex](Sunmachine-A2-fw.hex) file.

The firmware can be uploaded using the [nRF Connect for Desktop](https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-for-desktop) or [nRF Command Line Tools](https://www.nordicsemi.com/Products/Development-tools/nrf-command-line-tools/download). Depends on whether you want to use GUI program or the command line.

You also need an appropriate programmer, the standalone [Segger J-Link](https://www.segger.com/products/debug-probes/j-link/) or the on-board Segger J-Link (this can be found for e.g. on [nrf52840dk](https://www.nordicsemi.com/Products/Development-hardware/nRF52840-DK) board). This programmer must be connected to Tag-Connect 2050 connector on the board and to your PC.

## Building from source code
Instead of using pre-built firmware, you can build it from source. To do this, you must have the [Zephyr development environment](https://www.zephyrproject.org/) installed. Follow [these instructions](https://docs.zephyrproject.org/latest/getting_started/index.html) to install it on your system.

To build the firmware, run the following command. *"./Sunmachine-A2-fw"* is a path to the source code directory.

	west build -b sma2 ./Sunmachine-A2-fw

And then, to upload the firmware to the board, you need an appropriate programmer. When connected, run the following command:

	west flash

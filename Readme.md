# Firmware for the RP2040 based Gameboy cartridge
Utilizing the power of PIO to create a cheap flashcartridge for the gameboy

## How to put ROMs on the cartridge
Connect the cartridge via USB with a PC. Use Chromium or Chrome browser (only those two support WebUSB atm) and open the [Webapp](https://croco.x-pantion.de) to connect to the cartridge.

## How to build
Open repository in VS Code with the [Dev Containers](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-containers) 
addon installed. VS Code should ask if it shall open the workspace in the devcontainer. 

Open terminal in VS Code and build with CMake:
```mkdir build
cd build
cmake ..
make
```

Alternatively install the PICO-SDK somewhere on your system and use CMake to build as above.

## How to flash firmware
Double tap the reset button. It should bring up the cartridge in the Pico Bootloader. Copy the UF2 file on to the virtual drive.

Alternatively fire the cartridge up in a Gameboy. In the ROM selector menu press SELECT (opens info screen) and press START on that screen.
This will also trigger the cartridge to go in Bootloader mode.

## Known limitations
- The data cannot be loaded fast enough from the external flash for double speed mode of the GBC.
  This means that most Gameboy Color games will not work. But there are exceptions which only use normal speed (eg Pokemon Silver/Gold).
  Classic Gameboy games should work fine and are tested on DMG, GBC, GBA
- As the RP2040 needs to be overclocked to achieve fastest reading speeds from the flash the power draw is pretty high.
  This could mean the power supply of the original Gameboy can't handle the cartridge if combined with a fancy IPS screen.
  But if you have installed an IPS screen you should consider upgrading the power supply anyway to get rid of the noise on the speakers.

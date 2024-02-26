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

## How to flash the firmware
Double tap the reset button. It should bring up the cartridge in the Pico Bootloader. Copy the UF2 file on to the virtual drive.

Alternatively fire the cartridge up in a Gameboy. In the ROM selector menu press SELECT (opens info screen) and press START on that screen.
This will also trigger the cartridge to the build in RP2040 bootloader mode.

## How does it work? Short answer please
The RP2040 has 8 PIO state-machines which are like small co-processors which can run arbitrary code. They are designed to be efficient in IO operations.
Which is what they are mostly doing in this cartridge. They follow the Gameboy cartridge parallel bus interface and detect if the cartridge needs
to spit out or receive some data. Those requests are forwarded to the ARM core through the FIFOs. There a cascade of DMAs will happily handle those
requests. To put this in the correct view: The RP2040 has 12 DMA channels and 8 PIO state-machine. The code in this repository uses all of those.
Well, one of the PIO state-machines is used to drive the LED.

## How do savegames work?
In original cartridges the savegames had been stored in a RAM which was using a coin cell battery to hold the data while the gameboy is switched off.
This has pros and cons. One of the biggest cons is that the savegame is lost if the battery dies. There was a big discussion on heartbroken users 
as the savegame of their beloved childhood Pokemon Blue/Red/Yellow was gone after 20 years. One has to remember that they started selling in 1998.
The biggest pro on the savegame RAM is that the save is instantaneous. And that is what the games expect. They do not wait for the save to be done.

Why this explanation? The QSPI flash on the RP2040 cartridge might not have those battery limitations on one side, but writing something to a flash
needs to follow a procedure of slowly erasing and then writing. Kinda crazy eh? 30 years later and our new tech is to slow to store things in time.

There is a RGB LED on the cartridge. If it starts lighting in red that means the Gameboy has written something to the emulated RAM on the cartridge.
Sadly most games do this already at startup although the savegame is not changed at all. So the LED is merely a reminder that the user needs to save
the game. In order to do this the user of the RP2040 cartridge has to hit the button which is on the cartridge. This will reset the cartridge and on
startup it will find the unsaved data and write it to the flash. The LED will light green to confirm this has happened.

There is also the possibility to manage the savegames via the WebUSB interface.

I have the idea to write some mechanism to hook into the VBlank interrupt of the Gameboy ROM to store the savegame with some Gameboy button
combination. That would ease up things as no reset would be needed. But this is just an plan/idea for now.

## Known limitations
- Gameboy Color games in double speed mode currently do not work on the GBA. Timings are very tight in this mode and it's unsure if there is ever
  a solution to this problem. It works fine on the normal GBC though. (Tested on 3 different consoles)
- Not all Gameboy Color ROMs are guaranteed to work. In theory all ROMs should work, but in practice the the cartridge has to detect if the GBC
  switched to double speed mode. This algorithm is not perfect and might need some adjustment. If a ROM is not working drop a hint through issues.
- As the RP2040 needs to be overclocked to achieve fastest reading speeds from the flash the power draw is higher than from an orignal cartridge.
  This could mean the power supply of the original Gameboy can't handle the cartridge. Especially if combined with a fancy IPS screen.
  But if you have installed an IPS screen you should consider upgrading the power supply anyway to get rid of the noise on the speakers.

## What else can it do?
Well, in the end this is open to imagination. The Gameboy has got a powerful co-processor which has an USB interface. Maybe use the Gameboy as
a controller? An interesting example is the actually the Bootloader of this project. The RP2040 and the Bootloader communicate via shared RAM. 
128k of the RP2040 RAM are shared with the Gameboy. All the heavy lifting is done by the PIO and DMAs. The second core of the RP2040 is completely
unused, the first core is only switching banks while a Gameboy game is running. Lots of unused CPU power.

## Does LSDJ work?
Yes. 128k saves are supported. With the newest releases it also runs on the GBC.

## How can I get the needed hardware?
Find the Kicad project files in this [repo](https://github.com/shilga/rp2040-gameboy-cartridge). Or order one on [Tindie](https://www.tindie.com/products/32710/).


## Discord
Find the Discord server to get help or discuss about this project. <https://discord.gg/dxRcBQSc>

# Ghost Mirror

A standalone practice and learning tool for *Super Mario Bros. 1* speedruns, featuring:
- Triple-emulator real-time viewports
- Translucent, high-fidelity ghost overlays (PB and TAS)
- In-game controls configuration and gamepad support
- Level comparison stats (PB vs. TAS frames, SOB vs. SOT delta)
- On-the-fly reference ROM/FM2 cycling and splitting

## Setup

### 1. ROM File
Provide your own NTSC Super Mario Bros. ROM:
- Place your NTSC `Super Mario Bros. (World).nes` ROM in the same folder as the executable.

### 2. Third-Party Dependencies
This repository does not include third-party dependencies. You must place them under the `3rd/` directory before building:
- **nestopia**: Place Nestopia emulator core source files in `3rd/nestopia/` (sourced from the Nestopia emulator project).
- **SDL2-Binaries**: Place the SDL2 VC development library files in `3rd/SDL2-Binaries/` (sourced from the SDL2 release binaries).

## Building
To compile the application:
1. Ensure CMake and Ninja are installed on your system.
2. Run `build.bat` from a Visual Studio Developer Command Prompt.

## Credits
- Based on the [RetroTournaments/static](https://github.com/RetroTournaments/static) project.
- Reference Warpless TAS movie by Maru.
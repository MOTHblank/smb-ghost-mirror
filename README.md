# Ghost Mirror

A standalone practice and learning tool for *Super Mario Bros. 1* speedruns, featuring:
- Triple-emulator real-time viewports (Player, PB, TAS)
- Translucent, high-fidelity ghost overlays with world-position accuracy
- RNG and framerule synchronization for accurate enemy spawns
- In-game controls configuration and gamepad support
- Level comparison stats (PB vs. TAS frames, SOB vs. SOT delta)
- On-the-fly reference ROM/FM2 cycling and splitting
- No ROM required at launch — select one from the menu

## Setup

### 1. ROM File
Provide your own NTSC Super Mario Bros. ROM.
Place it in the same folder as the executable, or select it at runtime from the menu (`R` to cycle ROMs).

### 2. TAS Reference
Generate per-level savestates and input segments by running:
```
ghost-mirror.exe --split <rom_path> <fm2_path>
```
Or press `S` in the menu after selecting ROM and FM2 files.

### 3. Third-Party Dependencies
This repository does not include third-party dependencies. Place them under `3rd/` before building:
- **nestopia**: Nestopia emulator core source in `3rd/nestopia/`.
- **SDL2-Binaries**: SDL2 VC development libraries in `3rd/SDL2-Binaries/`.

## Building
1. Ensure CMake and Ninja are installed.
2. Run `build.bat` from a Visual Studio Developer Command Prompt.

## Controls

| Action | Key | Gamepad |
|---|---|---|
| D-pad | Arrow keys | D-pad |
| A / B | Z / X | A / B |
| Start / Select | Enter / RShift | Start / Back |
| Load level 1–8 | 1–8 | — |
| Quicksave / Quickload | F5 / F9 | — |
| Save PB | F8 | — |
| Restart level | F10 | — |
| Toggle overlay | Tab | — |
| Menu | Esc | — |
| Configure bindings | C (in menu) | Y (in menu) |
| Cycle ROM / FM2 | R / F (in menu) | — |
| Re-split | S (in menu) | — |
| Toggle transition mode | T (in menu) | — |

## Credits
- Based on the [RetroTournaments/static](https://github.com/RetroTournaments/static) project.
- Reference Warpless TAS movie by Maru.
# Ghost Mirror

A standalone *Super Mario Bros. 1* speedrun practice tool featuring:
- Three synchronized Nestopia instances: Player, personal best and TAS
- Transparent, world-positioned ghost overlays and frame comparisons
- RNG and framerule synchronization
- Data-driven route selection, with Any% Warpless configured by default
- ROM/TAS/route-isolated savestates, tagged replay files and PB records
- Five persistent practice checkpoints per stage and reference profile
- Keyboard/gamepad bindings and a paginated level-selection menu

## Setup

### 1. ROM
Provide your own NTSC Super Mario Bros. ROM. Place it next to the executable,
pass `--rom <path>`, or use **R** in the menu to choose a ROM in the current directory.

### 2. TAS reference and profile
Place a matching FM2 movie alongside the executable or pass `--fm2 <path>`.
The FM2's **ROM checksum must match** the selected ROM. Ghost Mirror refuses to
split or compare against a mismatched reference.

Split the movie into stages:

```text
ghost-mirror.exe --split "Super Mario Bros. (World).nes" maru.fm2
```

Or choose ROM (**R**) and FM2 (**F**) in the menu and press **S** to split
and apply. When changing ROM or movie, the newly selected reference must
be applied with **S** before loading its levels.

The generated files are stored under

```text
savestates/profiles/<route-id-and-definition>/<rom-checksum>/<tas-content-identity>/
    profile.txt
    records.txt
    1-1.state
    1-1.tas.fm2
    1-1.pb.fm2
    1-1.practice.fm2
    checkpoints/
        1-1-1.gmc
        1-1-2.gmc
        ...
```

The profile marker and replay tags must match the active ROM, TAS reference,
route definition and stage. Invalid/missing markers or incompatible replays
are ignored. PB records are scoped to the same profile, so switching the
ROM or TAS never silently combines records from different references.

**Existing unscoped `savestates/*.state` and `*.tas.fm2` files remain
untouched.** To start using profiles, run **S** (or `--split`) again with the
original ROM and FM2. Old unscoped PB files cannot be automatically attributed
to a particular reference and are intentionally not imported.

### 3. Custom routes
The default manifest is `routes/warpless.route`:

```ini
id=warpless
name=SMB1 Any% Warpless
stage=1-1,1,1
stage=1-2,1,2
stage=4-1,4,1
stage=4-2,4,2
stage=8-1,8,1
stage=8-2,8,2
stage=8-3,8,3
stage=8-4,8,4
```

Each `stage` uses `id,world,level` (one-based SMB coordinates). You can
optionally append `,state_filename,tas_filename,pb_filename` to associate
custom files, which still live inside that profile's directory. Add
`shortcut=from-id,to-id` for an allowed forward jump over other stages.
Normally transitions follow manifest order and detect RAM world/level changes
at gameplay boundaries. Use `--route path/to/route.route` to select a
different manifest, including a set of individual-stage practice entries.

```text
ghost-mirror.exe --route routes/warpless.route --savestates savestates --split myrom.nes mytas.fm2
ghost-mirror.exe --route routes/warpless.route --rom myrom.nes --fm2 mytas.fm2
```

Level selections and splits follow the manifest. The menu shows eight stages
per page, with left/right navigation traversing the entire route. Number keys
1–8 remain direct shortcuts to the first eight stages.

## Controls

| Action | Keyboard | Gamepad |
|---|---|---|
| D-pad | Arrow keys | D-pad |
| A / B | Z / X | A / B |
| Start / Select | Enter / Right Shift | Start / Back |
| Load stage 1–8 | 1–8 | — |
| Select checkpoint slot 1–5 | Ctrl+1–5 or `[` / `]` | — |
| Save current checkpoint | F5 | Left shoulder |
| Restore selected checkpoint | F9 | Right shoulder |
| Export recording / eligible PB | F8 | Y |
| Restart stage with a clean attempt | F10 | Left stick |
| Toggle ghost overlay | Tab | Right stick |
| Open / close menu | Esc | Back |
| Configure bindings | C (in menu) | Y (in menu) |
| Cycle ROM / FM2 | R / F (in menu) | — |
| Re-split and apply reference | S (in menu) | — |
| Toggle transition mode | T (in menu) | — |

The current checkpoint slot is visible in the window title and menu. Each
stage has up to five persistent checkpoints **per profile**. Save with F5,
choose slots with Ctrl+1–5 or cycle with the square brackets, and load with F9 while practicing a
stage; F9 can also load a selected stage's checkpoint from the stage menu.
The checkpoint includes all three emulator states, the matching PB/TAS input
streams, frame counters, recorded player inputs and level-transition
synchronization state. Files are replaced
atomically; invalid or truncated checkpoints are refused.

**Restoring a checkpoint marks the attempt PRACTICE.** Such runs are ineligible
for PB promotion, including automatic PB detection at the stage transition.
F8 exports these inputs to `<stage>.practice.fm2` without changing the PB.
Restarting the stage with F10 or loading it fresh starts a new clean attempt.
A normal clean attempt still records PBs automatically on completion. A
manual slower clean recording goes to `<stage>.export.fm2`.

## Building

This repository does not bundle third-party dependencies. Provide:
- Nestopia source in `3rd/nestopia/`.
- SDL2 VC development libraries in `3rd/SDL2-Binaries/`.

Use CMake and Ninja, or run `build.bat` from a Visual Studio Developer
Command Prompt.

## Credits

- Based on [RetroTournaments/static](https://github.com/RetroTournaments/static).
- Reference Warpless TAS movie by Maru.

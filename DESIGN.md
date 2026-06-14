# Ghost Mirror — Triple-Emulator Ghost Racing for SMB

## Motivation

The smb-vanilla-port's C physics don't match the NES. Friction, acceleration curves, sub-frame timing, RNG from power-on frame counts — all subtly wrong. **Ghost Mirror** sidesteps all of that:

> Run **three real NES emulators** (Nestopia) in one process. One takes live player input **and records it to FM2**. The other two replay FM2 files — your PB and Maru's TAS. Physics accuracy is guaranteed because all three run the real ROM.

The core loop is **ghost racing**: play a level, see how you compare to your best and to the TAS in real time, then your run becomes the new PB ghost for next time.

---

## Architecture Overview

```
┌──────────────────────────────────────────────────────────────────────┐
│                        ghost-mirror.exe                              │
│                                                                      │
│  ┌────────────────┐   ┌────────────────┐   ┌────────────────┐       │
│  │  EMU: PLAYER   │   │  EMU: PB       │   │  EMU: TAS      │       │
│  │  (Nestopia)    │   │  (Nestopia)    │   │  (Nestopia)    │       │
│  │                │   │                │   │                │       │
│  │  Live input ──────► RECORDER        │   │                │       │
│  │  from SDL      │   │  FM2 replay    │   │  FM2 replay    │       │
│  │                │   │  (your PB)     │   │  (Maru's TAS)  │       │
│  └───────┬────────┘   └───────┬────────┘   └───────┬────────┘       │
│          │                    │                    │                  │
│          │         ┌──────────┴────────────────────┘                  │
│          │         │                                                  │
│          ▼         ▼                                                  │
│  ┌──────────────────────────────────────────────────┐                │
│  │           SDL2 Window (3 viewports)              │                │
│  │  ┌──────────┐   ┌──────────┐   ┌──────────┐    │                │
│  │  │   YOU    │   │    PB    │   │   TAS    │    │                │
│  │  │ 256×240  │   │ 256×240  │   │ 256×240  │    │                │
│  │  └──────────┘   └──────────┘   └──────────┘    │                │
│  │                                                  │                │
│  │  Frame: 0342  │  PB: +2F ahead  │  TAS: -5F    │                │
│  └──────────────────────────────────────────────────┘                │
│                                                                      │
│  ┌──────────────────────────────────────────────────┐                │
│  │          Savestate Organizer (Level Menu)         │                │
│  │  [1-1] [1-2] [4-1] [4-2] [8-1] [8-2] [8-3] [8-4]               │
│  │  Per-level: .state + .pb.fm2 + .tas.fm2           │                │
│  └──────────────────────────────────────────────────┘                │
└──────────────────────────────────────────────────────────────────────┘
```

---

## 1. Recording Architecture (Core Feature)

The recorder is **always active** during gameplay. There is no "record mode" toggle — if you're playing, you're recording.

### Data Flow — Every Single Frame

```
┌─────────────┐
│ SDL Input   │──► ControllerState (uint8_t bitmask: RLDUTSBA)
└──────┬──────┘
       │
       ▼
┌──────────────────────────────────────────────────────────────┐
│                    EmuTrio::StepFrame()                       │
│                                                              │
│  1. recorder.Append(live_input)  ◄── RECORD the input first  │
│                                                              │
│  2. player_emu->Execute(live_input)      ← live              │
│  3. pb_emu->Execute(pb_inputs[frame])    ← from PB FM2       │
│  4. tas_emu->Execute(tas_inputs[frame])  ← from TAS FM2      │
│  5. frame++                                                  │
│                                                              │
│  6. Check level transition on player_emu (RAM peek)          │
│     └─► If level completed → recorder.FinishLevel()          │
└──────────────────────────────────────────────────────────────┘
```

### Recorder Internal State

```cpp
struct Recorder {
    // Active recording buffer — grows every frame
    std::vector<nes::ControllerState> inputs;  // one entry per frame

    // Snapshot of emulator state at level start (for the FM2 header context)
    std::string level_start_state;   // Nestopia savestate string
    std::string level_id;            // "1-1", "4-2", etc.
    int start_frame;                 // absolute frame from power-on

    // Timing
    int frame_count;                 // frames since level load
    bool is_recording;               // true during gameplay, false in menu

    void Begin(const std::string& level, const std::string& state);
    void Append(nes::ControllerState input);
    void FinishLevel();              // called on level completion
    void SavePB(const std::string& path);  // write FM2 to disk
    void Discard();                  // player died / reset
};
```

### Recording Lifecycle

```
[Select Level 4-2]
    │
    ├─ Load 4-2.state into all 3 emus
    ├─ recorder.Begin("4-2", state_string)
    │
    ▼
[Gameplay Loop]
    │
    ├─ Every frame: recorder.Append(live_input)
    │  inputs[] grows: [0x00, 0x81, 0x81, 0x80, ...]
    │
    │  Meanwhile:
    │  - PB emu replays old pb_inputs[frame]
    │  - TAS emu replays tas_inputs[frame]
    │
    ├─ Level transition detected? ──► recorder.FinishLevel()
    │       │
    │       ├─ Compare frame_count to saved PB time
    │       ├─ If faster (or no PB exists):
    │       │    └─ WriteFM2File("savestates/4-2.pb.fm2", inputs)
    │       │       └─ This becomes the PB ghost next time
    │       ├─ If slower:
    │       │    └─ Prompt: "Save anyway? (F5=yes)"
    │       └─ Update records.txt with best time
    │
    ├─ Player dies? ──► recorder.Discard()
    │       └─ Clear inputs[], restart from savestate
    │
    └─ F5 pressed? ──► recorder.SavePB() (manual force-save)
         └─ Always writes, regardless of time

[Next session: Load 4-2]
    └─ The FM2 you just saved is now pb_inputs[]
       └─ Your previous self races alongside you
```

### FM2 Output Format

Standard FCEUX-compatible FM2. Uses Static's `WriteFM2File()`:

```
version 3
emuVersion 22020
rerecordCount 0
palFlag 0
romFilename Super Mario Bros. (World)
romChecksum base64:jjYwGG411HcjG/j9UOVM3Q==
guid 00000000-0000-0000-0000-000000000000
fourscore 0
microphone 0
port0 1
port1 0
port2 0
FDS 0
NewPPU 0
comment ghost-mirror PB for 4-2, 1334 frames
|0|........|||
|0|...T..B.||| ← frame 0: Start+B
|0|.......A||| ← frame 1: A
|0|R......A||| ← frame 2: Right+A
...
```

Each `|0|RLDUTSBA|||` line is one frame. The file can be loaded into FCEUX for verification or manual review.

---

## 2. Emulator Trio

Three `NestopiaNESEmulator` instances (from Static's `nestopiaimpl.h`), all loaded with the same iNES ROM (`Super Mario Bros. (World).nes`).

| Instance | Input Source | Records to FM2? | Purpose |
|---|---|---|---|
| `player` | Live SDL input | **Yes** (always) | The runner's screen |
| `pb` | `vector<ControllerState>` from `.pb.fm2` | No | Ghost of your personal best |
| `tas` | `vector<ControllerState>` from `.tas.fm2` | No | Ghost of Maru's TAS |

**Key invariant:** All three start from the **exact same Nestopia savestate**. Since the savestate encodes the complete CPU/PPU/APU state (including internal frame counter, RNG seeds, scroll positions), all three emus are in identical states at frame 0 of each level. No leeway hacks, no forced $77F, no timing guesswork.

**When a ghost runs out of inputs** (it finished the level before the player), it freezes on its last frame. The player can still see where the ghost ended up.

---

## 3. Savestate Organizer

### Directory Structure

```
savestates/
├── 1-1.state       ← Nestopia savestate at first controllable frame of 1-1
├── 1-1.pb.fm2      ← Your best segment recording (written by recorder)
├── 1-1.tas.fm2     ← Maru's TAS segment (written by splitter)
├── 1-2.state
├── 1-2.pb.fm2
├── 1-2.tas.fm2
├── 4-1.state
├── 4-1.pb.fm2
├── 4-1.tas.fm2
├── 4-2.state       ... (same pattern)
├── 8-1.state
├── 8-2.state
├── 8-3.state
├── 8-4.state
└── records.txt     ← Best frame counts per level
```

### Level Loading Flow

```
SelectLevel("4-2"):
    1. state_data = ReadFile("savestates/4-2.state")
    2. player_emu.LoadState(state_data)     ← all 3 from same state
       pb_emu.LoadState(state_data)
       tas_emu.LoadState(state_data)
    3. pb_inputs = ReadFM2File("savestates/4-2.pb.fm2")   ← may not exist
    4. tas_inputs = ReadFM2File("savestates/4-2.tas.fm2") ← from splitter
    5. recorder.Begin("4-2", state_data)
    6. frame = 0
    7. → Gameplay begins, all 3 emus step in lockstep
```

### Level Transition Detection

Every frame, peek into the player emu's RAM:

```cpp
uint8_t oper_mode         = player_emu->CPUPeek(0x0770);  // 0=title, 1=game, 2=victory
uint8_t engine_subroutine = player_emu->CPUPeek(0x000E);  // 0=init gameplay
uint8_t world             = player_emu->CPUPeek(0x075F);  // 0-indexed world
uint8_t level             = player_emu->CPUPeek(0x075C);  // 0-indexed level

// Transition when oper_mode==1 AND engine changes to 0 AND world/level changed
```

On transition: `recorder.FinishLevel()` → auto-save PB if faster → load next level.

---

## 4. FM2 Segment Splitter (Bootstrap Tool)

CLI mode: `ghost-mirror --split <rom_path> <fm2_path>`

One-time process to bootstrap the `savestates/` directory from Maru's full-run TAS.

```
1. Load ROM into a Nestopia instance
2. Parse full FM2 → vector<ControllerState> (17,000+ frames)
3. Execute frame-by-frame from power-on, feeding FM2 inputs
4. Every frame, check RAM for level transitions
5. At each transition:
   ├─ Save Nestopia state → savestates/<W>-<L>.state
   └─ Slice remaining FM2 inputs → savestates/<W>-<L>.tas.fm2
       (inputs from this frame until next transition)
6. Output: 8 .state files + 8 .tas.fm2 files
```

Expected output:
| Level | Approx. Frames | State File | TAS FM2 |
|---|---|---|---|
| 1-1 | ~1467 | 1-1.state | 1-1.tas.fm2 |
| 1-2 | ~1791 | 1-2.state | 1-2.tas.fm2 |
| 4-1 | ~1178 | 4-1.state | 4-1.tas.fm2 |
| 4-2 | ~1334 | 4-2.state | 4-2.tas.fm2 |
| 8-1 | ~2278 | 8-1.state | 8-1.tas.fm2 |
| 8-2 | ~1773 | 8-2.state | 8-2.tas.fm2 |
| 8-3 | ~1304 | 8-3.state | 8-3.tas.fm2 |
| 8-4 | ~3558 | 8-4.state | 8-4.tas.fm2 |

---

## 5. Renderer

SDL2 window with three NES screens side by side.

**Pipeline per frame:**
1. `emu.ScreenPeekFrame(&frame)` → 256×240 array of palette indices (0–63)
2. Convert each pixel via `DefaultPaletteRGB()` → RGBA pixel buffer (256×240×4 bytes)
3. `SDL_UpdateTexture(tex, NULL, rgba_buf, 256*4)` → upload to GPU
4. `SDL_RenderCopy(renderer, tex, NULL, &viewport_rect)` → draw scaled
5. Repeat for all 3 emus, then `SDL_RenderPresent()`

**Scale:** 2× integer scaling → 512×480 per viewport, 1536×480 total + HUD bar.

**HUD:** Frame counter, PB delta (frames ahead/behind), TAS delta. Computed by comparing Mario's world-X position across the three emus:
```cpp
int player_x = (player_emu->CPUPeek(0x071A) << 8) | player_emu->CPUPeek(0x071C) | player_emu->CPUPeek(0x0086);
// Same for pb_emu and tas_emu
```

---

## 6. Input Handler

| Key | NES Button | | Hotkey | Action |
|---|---|---|---|---|
| ← → ↑ ↓ | D-pad | | F1–F8 | Jump to level |
| Z | A (Jump) | | F5 | Force-save PB |
| X | B (Run) | | F9 | Restart level |
| Enter | Start | | Escape | Level selector |
| RShift | Select | | | |

Gamepad: SDL_GameController API, same mapping.

---

## Why Nestopia (from Static) Instead of FCEUX?

| | Nestopia (Static) | FCEUX |
|---|---|---|
| Multi-instance | ✅ Clean OOP, no globals | ❌ Global state everywhere |
| Headless API | ✅ `Execute()`, `ScreenPeekFrame()`, `CPUPeek()` | ❌ Requires full driver init |
| FM2 parsing | ✅ `ReadFM2File()` / `WriteFM2File()` in Static | ✅ Native format |
| Savestate | ✅ `SaveState(ostream)` / `LoadState(istream)` | ✅ But format differs |
| Build | ✅ CMake, already in workspace | ⚠️ Complex build, many deps |

FCEUX source stays in the workspace as **reference** for FM2 format edge cases and NES behavior.

---

## Build System

**CMake + Ninja** via MSVC (`vcvarsall.bat x64`).

Dependencies (all in-workspace, no downloads):
| Dependency | Source Path | What We Use |
|---|---|---|
| Nestopia core | `static/3rd/nestopia/` | `3rdnestopia` static library |
| SDL2 | `SDL2-Binaries/win32/lib64/` | `SDL2.lib` + `SDL2.dll` |
| Static NES lib | `static/src/nes/`, `static/include/nes/` | Cherry-picked `.cpp` + headers |
| Static util | `static/include/util/string.h`, `static/src/util/string.cpp` | `StringStartsWith()` for FM2 parsing |

**Not needed:** OpenCV, OpenSSL, SQLite3, GTK3, imgui, spdlog, zmq, SDL_mixer.

---

## File Structure

```
ghost-mirror/
├── CMakeLists.txt           ← links Nestopia + SDL2 + cherry-picked Static sources
├── DESIGN.md                ← this file
├── build.bat                ← vcvarsall + cmake + ninja
├── src/
│   ├── main.cpp             ← entry point, SDL init, main loop, --split mode
│   ├── emu_trio.h           ← three-emulator management
│   ├── emu_trio.cpp
│   ├── recorder.h           ← PB recording (always-on input capture + FM2 write)
│   ├── recorder.cpp
│   ├── renderer.h           ← SDL2 rendering, 3 viewports, HUD
│   ├── renderer.cpp
│   ├── input.h              ← keyboard/gamepad → NES controller mapping
│   ├── input.cpp
│   ├── savestate_organizer.h ← level menu, state/FM2 file management
│   ├── savestate_organizer.cpp
│   └── splitter.cpp         ← --split CLI: full FM2 → per-level segments
├── savestates/              ← generated by splitter + recorder
│   ├── 1-1.state
│   ├── 1-1.tas.fm2
│   ├── 1-1.pb.fm2           ← written by recorder after you play
│   └── ...
└── build/                   ← cmake build output
```

---

## Timing & Synchronization

**Why this solves the old problems:**

- All three emus start from the **same Nestopia savestate** → identical CPU/PPU/APU state, identical internal frame counter, identical RNG.
- The TAS segment was recorded from that exact state (by the splitter running the real ROM).
- The PB segment was recorded from that exact state (by the recorder during a previous session).
- **No leeway, no forced $77F, no timing approximation.** The emulator state IS the state.

---

## Stretch Goals (Post-MVP)

- **Audio:** Player emu's APU → SDL_Audio output.
- **Split timer:** Cumulative time across levels, sum-of-best.
- **Ghost overlay mode:** Render PB/TAS as translucent sprites ON the player's screen (one viewport instead of three).
- **Rewind:** Frame-by-frame rewind on player emu using periodic savestates.
- **FM2 export:** Export full-run FM2 by concatenating all PB segments.

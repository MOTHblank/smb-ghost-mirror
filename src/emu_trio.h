#ifndef GHOST_MIRROR_EMU_TRIO_H
#define GHOST_MIRROR_EMU_TRIO_H

#include <memory>
#include <vector>
#include <string>
#include "nes/nes.h"
#include "nes/nestopiaimpl.h"

// Manages three Nestopia emulator instances running in lockstep.
// - player: receives live input, is always recorded
// - pb:     replays PB FM2 segment
// - tas:    replays TAS FM2 segment
struct EmuTrio {
    std::unique_ptr<sta::nes::NestopiaNESEmulator> player;
    std::unique_ptr<sta::nes::NestopiaNESEmulator> pb;
    std::unique_ptr<sta::nes::NestopiaNESEmulator> tas;

    // Per-frame input vectors for ghost emus
    std::vector<sta::nes::ControllerState> pb_inputs;
    std::vector<sta::nes::ControllerState> tas_inputs;

    // Current frame index since level load
    int frame;
    int pb_frame_idx;
    int tas_frame_idx;

    // Cached screen frames (palette-indexed, 256x240)
    sta::nes::Frame player_frame;
    sta::nes::Frame pb_frame;
    sta::nes::Frame tas_frame;

    // ROM data (kept in memory for creating new instances)
    std::string rom_data;

    EmuTrio();
    ~EmuTrio();

    // Load ROM from file into all three emulators
    bool LoadROM(const std::string& rom_path);

    // Load a Nestopia savestate into all three emus (from file)
    bool LoadState(const std::string& state_path);

    // Load a Nestopia savestate into all three emus (from string)
    bool LoadStateString(const std::string& state_data);

    // Load FM2 input files for PB and TAS ghosts
    bool LoadPBInputs(const std::string& fm2_path);
    bool LoadTASInputs(const std::string& fm2_path);

    // Clear ghost inputs
    void ClearPBInputs();
    void ClearTASInputs();

    // Advance all three emus by one frame.
    // live_input goes to player emu; pb/tas get their next input from vectors.
    // Returns the live_input (for recording).
    void StepFrame(sta::nes::ControllerState live_input, bool step_pb = true, bool step_tas = true);

    // Read RAM from player emu
    uint8_t PlayerPeek(uint16_t address) const;

    // Reset frame counter
    void ResetFrame();
};

#endif // GHOST_MIRROR_EMU_TRIO_H

#ifndef GHOST_MIRROR_RECORDER_H
#define GHOST_MIRROR_RECORDER_H

#include <vector>
#include <string>
#include "nes/nes.h"

// Always-on input recorder. Captures every frame of player input during gameplay.
// Writes standard FCEUX-compatible FM2 files.
struct Recorder {
    std::vector<sta::nes::ControllerState> inputs;

    std::string level_id;           // e.g. "1-1", "4-2"
    std::string level_start_state;  // Nestopia savestate at level load
    int frame_count;
    bool active;

    Recorder();

    // Start recording for a new level
    void Begin(const std::string& level, const std::string& state_string);

    // Append one frame of input (called every StepFrame)
    void Append(sta::nes::ControllerState input);

    // Save recorded inputs as FM2 to the given path
    bool SaveFM2(const std::string& path) const;

    // Discard current recording (on death/reset)
    void Discard();

    // Stop recording (level completed)
    void Finish();

    // Get current frame count
    int FrameCount() const;
};

#endif // GHOST_MIRROR_RECORDER_H

#include "recorder.h"
#include <fstream>
#include <iostream>
#include "atomic_file.h"
#include "rom_metadata.h"

Recorder::Recorder()
    : frame_count(0)
    , active(false)
{
}

void Recorder::Begin(const std::string& level, const std::string& state_string) {
    inputs.clear();
    level_id = level;
    level_start_state = state_string;
    frame_count = 0;
    active = true;
    std::cout << "Recording started for level " << level << std::endl;
}

void Recorder::Append(sta::nes::ControllerState input) {
    if (!active) return;
    inputs.push_back(input);
    frame_count++;
}

bool Recorder::SetROM(const std::string& rom_path, const std::string& rom_data) {
    has_rom_header = MakeRomFM2Header(rom_path, rom_data, &rom_header);
    if (!has_rom_header) std::cerr << "ERROR: Cannot compute FM2 metadata for ROM: " << rom_path << std::endl;
    return has_rom_header;
}

bool Recorder::SaveFM2(const std::string& path) const {
    if (inputs.empty()) {
        std::cerr << "WARNING: No inputs to save" << std::endl;
        return false;
    }

    if (!has_rom_header) {
        std::cerr << "ERROR: No valid ROM metadata; refusing to save misleading FM2" << std::endl;
        return false;
    }
    sta::nes::FM2Header header = rom_header;

    // Add ghost-mirror metadata as additional lines
    std::string comment = "comment ghost-mirror PB for " + level_id +
                          ", " + std::to_string(inputs.size()) + " frames";
    header.additionalLines.push_back(comment);

    if (!AtomicWriteFile(path, [&](std::ostream& os) {
        sta::nes::WriteFM2File(os, inputs, header);
    })) {
        return false;
    }

    std::cout << "FM2 saved: " << path << " (" << inputs.size() << " frames)" << std::endl;
    return true;
}

void Recorder::Discard() {
    inputs.clear();
    frame_count = 0;
    // active stays true — we'll re-record from the restart
}

void Recorder::Finish() {
    active = false;
    std::cout << "Recording finished for level " << level_id
              << ": " << frame_count << " frames" << std::endl;
}

int Recorder::FrameCount() const {
    return frame_count;
}

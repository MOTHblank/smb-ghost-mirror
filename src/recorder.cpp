#include "recorder.h"
#include <fstream>
#include <iostream>

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

bool Recorder::SaveFM2(const std::string& path) const {
    if (inputs.empty()) {
        std::cerr << "WARNING: No inputs to save" << std::endl;
        return false;
    }

    std::ofstream ofs(path);
    if (!ofs) {
        std::cerr << "ERROR: Cannot open for writing: " << path << std::endl;
        return false;
    }

    sta::nes::FM2Header header = sta::nes::FM2Header::Defaults();
    header.romFilename = "Super Mario Bros. (World)";
    header.romChecksum = "base64:jjYwGG411HcjG/j9UOVM3Q==";

    // Add ghost-mirror metadata as additional lines
    std::string comment = "comment ghost-mirror PB for " + level_id +
                          ", " + std::to_string(inputs.size()) + " frames";
    header.additionalLines.push_back(comment);

    sta::nes::WriteFM2File(ofs, inputs, header);
    ofs.close();

    std::cout << "PB saved: " << path << " (" << inputs.size() << " frames)" << std::endl;
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

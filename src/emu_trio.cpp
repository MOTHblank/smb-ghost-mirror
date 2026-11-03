#include "emu_trio.h"
#include <fstream>
#include <iostream>
#include <sstream>

EmuTrio::EmuTrio()
    : frame(0)
{
    player_frame.fill(0);
    pb_frame.fill(0);
    tas_frame.fill(0);
}

EmuTrio::~EmuTrio() {}

bool EmuTrio::LoadROM(const std::string& rom_path) {
    // Read ROM file into memory
    std::ifstream ifs(rom_path, std::ios::binary);
    if (!ifs) {
        std::cerr << "ERROR: Cannot open ROM: " << rom_path << std::endl;
        return false;
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    rom_data = oss.str();
    ifs.close();

    // Create three emulator instances
    player = std::make_unique<sta::nes::NestopiaNESEmulator>();
    pb     = std::make_unique<sta::nes::NestopiaNESEmulator>();
    tas    = std::make_unique<sta::nes::NestopiaNESEmulator>();

    try {
        player->LoadINESString(rom_data);
        pb->LoadINESString(rom_data);
        tas->LoadINESString(rom_data);
    } catch (const std::exception& e) {
        std::cerr << "ERROR: Failed to load ROM: " << e.what() << std::endl;
        return false;
    }

    frame = 0;
    std::cout << "ROM loaded into 3 emulators: " << rom_path << std::endl;
    return true;
}

bool EmuTrio::LoadState(const std::string& state_path) {
    std::ifstream ifs(state_path, std::ios::binary);
    if (!ifs) {
        std::cerr << "ERROR: Cannot open state: " << state_path << std::endl;
        return false;
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    std::string state_data = oss.str();
    ifs.close();

    return LoadStateString(state_data);
}

bool EmuTrio::LoadStateString(const std::string& state_data) {
    try {
        player->LoadStateString(state_data);
        pb->LoadStateString(state_data);
        tas->LoadStateString(state_data);
    } catch (const std::exception& e) {
        std::cerr << "ERROR: Failed to load state: " << e.what() << std::endl;
        return false;
    }

    frame = 0;
    pb_frame_idx = 0;
    tas_frame_idx = 0;
    return true;
}

bool EmuTrio::LoadPBInputs(const std::string& path, const ReferenceProfile& profile,
                           const std::string& stage) {
    pb_inputs.clear();
    if (!GhostLoadTaggedReplay(path, profile, stage, &pb_inputs)) {
        std::cerr << "PB missing or incompatible: " << path << std::endl;
        return false;
    }
    std::cout << "PB loaded: " << pb_inputs.size() << " frames\n";
    return true;
}

bool EmuTrio::LoadTASInputs(const std::string& path, const ReferenceProfile& profile,
                            const std::string& stage) {
    tas_inputs.clear();
    if (!GhostLoadTaggedReplay(path, profile, stage, &tas_inputs)) {
        std::cerr << "TAS missing or incompatible: " << path << std::endl;
        return false;
    }
    std::cout << "TAS loaded: " << tas_inputs.size() << " frames\n";
    return true;
}

void EmuTrio::ClearPBInputs() {
    pb_inputs.clear();
}

void EmuTrio::ClearTASInputs() {
    tas_inputs.clear();
}

void EmuTrio::StepFrame(sta::nes::ControllerState live_input, bool step_pb, bool step_tas) {
    // 1. Player emu — live input
    player->Execute(live_input);

    // 2. PB emu — replay or freeze
    if (step_pb) {
        if (pb_frame_idx < static_cast<int>(pb_inputs.size())) {
            pb->Execute(pb_inputs[pb_frame_idx]);
            pb_frame_idx++;
        }
    }
    // else: frozen (do not execute or advance index)

    // 3. TAS emu — replay or freeze
    if (step_tas) {
        if (tas_frame_idx < static_cast<int>(tas_inputs.size())) {
            tas->Execute(tas_inputs[tas_frame_idx]);
            tas_frame_idx++;
        }
    }
    // else: frozen

    // 4. Capture screens
    player->ScreenPeekFrame(&player_frame);
    pb->ScreenPeekFrame(&pb_frame);
    tas->ScreenPeekFrame(&tas_frame);

    frame++;
}

uint8_t EmuTrio::PlayerPeek(uint16_t address) const {
    return player->CPUPeek(address);
}

void EmuTrio::ResetFrame() {
    frame = 0;
    pb_frame_idx = 0;
    tas_frame_idx = 0;
}

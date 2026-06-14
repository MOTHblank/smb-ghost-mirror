// FM2 Segment Splitter
// CLI mode: ghost-mirror --split <rom_path> <fm2_path>
//
// Runs Maru's full TAS through Nestopia from power-on, detects level transitions
// by monitoring RAM, and saves per-level savestates + FM2 segments.

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <filesystem>

#include "nes/nes.h"
#include "nes/nestopiaimpl.h"

namespace fs = std::filesystem;

// SMB RAM addresses for level detection
static constexpr uint16_t OPER_MODE           = 0x0770;
static constexpr uint16_t OPER_MODE_TASK      = 0x0772;
static constexpr uint16_t GAME_ENGINE_SUB     = 0x000E;
static constexpr uint16_t WORLD_NUMBER        = 0x075F;
static constexpr uint16_t LEVEL_NUMBER        = 0x075C;
static constexpr uint16_t INTERVAL_TIMER      = 0x077F;

// The any% route levels (world, level) - 0-indexed
struct RouteLevel {
    int world;
    int level;
    const char* name;
};

static const RouteLevel ROUTE[] = {
    { 0, 0, "1-1" },
    { 0, 1, "1-2" },
    { 3, 0, "4-1" },
    { 3, 1, "4-2" },
    { 7, 0, "8-1" },
    { 7, 1, "8-2" },
    { 7, 2, "8-3" },
    { 7, 3, "8-4" },
};
static constexpr int NUM_ROUTE_LEVELS = 8;

// Check if current world/level matches a route level
static int FindRouteIndex(int world, int level) {
    for (int i = 0; i < NUM_ROUTE_LEVELS; i++) {
        if (ROUTE[i].world == world && ROUTE[i].level == level) {
            return i;
        }
    }
    return -1;
}

int RunSplitter(const std::string& rom_path, const std::string& fm2_path,
                const std::string& output_dir)
{
    std::cout << "=== Ghost Mirror FM2 Splitter ===" << std::endl;
    std::cout << "ROM:    " << rom_path << std::endl;
    std::cout << "FM2:    " << fm2_path << std::endl;
    std::cout << "Output: " << output_dir << std::endl;

    // Load FM2
    std::ifstream fm2_ifs(fm2_path);
    if (!fm2_ifs) {
        std::cerr << "ERROR: Cannot open FM2: " << fm2_path << std::endl;
        return 1;
    }

    std::vector<sta::nes::ControllerState> all_inputs;
    sta::nes::FM2Header header;
    try {
        sta::nes::ReadFM2File(fm2_ifs, &all_inputs, &header);
    } catch (const std::exception& e) {
        std::cerr << "ERROR: Failed to parse FM2: " << e.what() << std::endl;
        return 1;
    }
    fm2_ifs.close();
    std::cout << "FM2 loaded: " << all_inputs.size() << " frames" << std::endl;

    // Create emulator and load ROM
    sta::nes::NestopiaNESEmulator emu;
    try {
        emu.LoadINESFile(rom_path);
    } catch (const std::exception& e) {
        std::cerr << "ERROR: Failed to load ROM: " << e.what() << std::endl;
        return 1;
    }

    // Ensure output directory exists
    fs::create_directories(output_dir);

    // Run frame-by-frame and detect transitions
    int prev_world = -1;
    int prev_level = -1;
    int current_route = -1;
    int level_start_frame = 0;
    bool in_gameplay = false;

    // Track which levels we've saved states for
    bool saved[NUM_ROUTE_LEVELS] = {};
    int level_start_frames[NUM_ROUTE_LEVELS] = {};

    std::cout << "Running emulation..." << std::endl;

    for (int f = 0; f < static_cast<int>(all_inputs.size()); f++) {
        emu.Execute(all_inputs[f]);

        uint8_t oper_mode    = emu.CPUPeek(OPER_MODE);
        uint8_t engine_sub   = emu.CPUPeek(GAME_ENGINE_SUB);
        uint8_t world        = emu.CPUPeek(WORLD_NUMBER);
        uint8_t level        = emu.CPUPeek(LEVEL_NUMBER);

        // Detect gameplay start: oper_mode == 1, engine_sub transitions to 0
        bool now_in_gameplay = (oper_mode == 1 && engine_sub == 0);

        if (now_in_gameplay && !in_gameplay) {
            // Transition into gameplay
            int route_idx = FindRouteIndex(world, level);

            if (route_idx >= 0 && !saved[route_idx]) {
                std::cout << "Level " << ROUTE[route_idx].name
                          << " started at frame " << f << std::endl;

                // Save the current state
                std::string state_path = output_dir + "/" + ROUTE[route_idx].name + ".state";
                try {
                    emu.SaveStateFile(state_path);
                    std::cout << "  Saved state: " << state_path << std::endl;
                } catch (const std::exception& e) {
                    std::cerr << "  ERROR saving state: " << e.what() << std::endl;
                }

                saved[route_idx] = true;
                level_start_frames[route_idx] = f;
            }

            prev_world = world;
            prev_level = level;
        }

        in_gameplay = now_in_gameplay;
    }

    // Now extract FM2 segments for each saved level
    std::cout << "\nExtracting FM2 segments..." << std::endl;
    for (int i = 0; i < NUM_ROUTE_LEVELS; i++) {
        if (!saved[i]) {
            std::cerr << "WARNING: Level " << ROUTE[i].name << " not found in FM2" << std::endl;
            continue;
        }

        int start = level_start_frames[i];
        int end;

        // Find the start of the next level (or end of inputs)
        if (i + 1 < NUM_ROUTE_LEVELS && saved[i + 1]) {
            end = level_start_frames[i + 1];
        } else {
            end = static_cast<int>(all_inputs.size());
        }

        // Slice inputs starting from the frame AFTER the state was saved
        std::vector<sta::nes::ControllerState> segment(
            all_inputs.begin() + (start + 1 < end ? start + 1 : start),
            all_inputs.begin() + end
        );

        // Write FM2 segment
        std::string fm2_out = output_dir + "/" + ROUTE[i].name + ".tas.fm2";
        std::ofstream ofs(fm2_out);
        if (!ofs) {
            std::cerr << "ERROR: Cannot write: " << fm2_out << std::endl;
            continue;
        }

        sta::nes::FM2Header seg_header = sta::nes::FM2Header::Defaults();
        seg_header.romFilename = header.romFilename;
        seg_header.romChecksum = header.romChecksum;
        std::string comment = "comment ghost-mirror TAS segment for " +
                              std::string(ROUTE[i].name) + ", " +
                              std::to_string(segment.size()) + " frames";
        seg_header.additionalLines.push_back(comment);

        sta::nes::WriteFM2File(ofs, segment, seg_header);
        ofs.close();

        std::cout << "  " << ROUTE[i].name << ": " << segment.size()
                  << " frames → " << fm2_out << std::endl;
    }

    std::cout << "\nSplitter done!" << std::endl;
    return 0;
}

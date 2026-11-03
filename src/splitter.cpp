// Route-driven FM2 segment splitter. Never writes outside the selected profile.
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <filesystem>

#include "nes/nes.h"
#include "nes/nestopiaimpl.h"
#include "atomic_file.h"
#include "route_manifest.h"
#include "reference_profile.h"

static constexpr uint16_t OPER_MODE = 0x0770;
static constexpr uint16_t GAME_ENGINE_SUB = 0x000E;
static constexpr uint16_t WORLD_NUMBER = 0x075F;
static constexpr uint16_t LEVEL_NUMBER = 0x075C;

int RunSplitter(const std::string& rom_path, const std::string& fm2_path,
                const RouteManifest& route, const ReferenceProfile& profile)
{
    std::cout << "=== Ghost Mirror FM2 Splitter: " << route.name << " ===\n"
              << "ROM: " << rom_path << "\nFM2: " << fm2_path
              << "\nProfile: " << profile.directory << std::endl;

    std::ifstream fm2(fm2_path);
    if (!fm2) {
        std::cerr << "ERROR: Cannot open FM2: " << fm2_path << std::endl;
        return 1;
    }
    std::vector<sta::nes::ControllerState> inputs;
    sta::nes::FM2Header header;
    try {
        sta::nes::ReadFM2File(fm2, &inputs, &header);
    } catch (const std::exception& e) {
        std::cerr << "ERROR: Cannot parse FM2: " << e.what() << std::endl;
        return 1;
    }
    if (header.romChecksum != profile.rom_checksum || inputs.empty()) {
        std::cerr << "ERROR: Empty movie or ROM checksum mismatch; refusing split\n";
        return 1;
    }
    sta::nes::NestopiaNESEmulator emu;
    try { emu.LoadINESFile(rom_path); }
    catch (const std::exception& e) {
        std::cerr << "ERROR: Cannot load ROM: " << e.what() << std::endl;
        return 1;
    }
    if (!profile.Prepare()) {
        std::cerr << "ERROR: Reference profile cannot be safely initialized\n";
        return 1;
    }

    std::vector<bool> saved(route.stages.size(), false);
    std::vector<int> start_frames(route.stages.size(), 0);
    bool in_gameplay = false;
    int last_index = -1;
    int count = 0;

    for (size_t frame = 0; frame < inputs.size(); ++frame) {
        emu.Execute(inputs[frame]);
        const bool playing = emu.CPUPeek(OPER_MODE) == 1 &&
                             emu.CPUPeek(GAME_ENGINE_SUB) == 0;
        if (playing && !in_gameplay) {
            const int index = route.Find(emu.CPUPeek(WORLD_NUMBER),
                                         emu.CPUPeek(LEVEL_NUMBER));
            if (index >= 0 && !saved[index]) {
                if (last_index >= 0 && !route.AllowsTransition(last_index, index)) {
                    std::cerr << "WARNING: Skipping unexpected stage transition to "
                              << route.stages[index].id << "\n";
                } else {
                    const auto path = (std::filesystem::path(profile.directory) /
                                       route.stages[index].state_file).string();
                    std::string state;
                    emu.SaveStateString(&state);
                    if (!AtomicWriteFile(path, [&](std::ostream& out) {
                        out.write(state.data(), static_cast<std::streamsize>(state.size()));
                    })) return 1;
                    saved[index] = true;
                    start_frames[index] = static_cast<int>(frame);
                    last_index = index;
                    ++count;
                    std::cout << "  " << route.stages[index].id << " at frame "
                              << frame << "\n";
                }
            }
        }
        in_gameplay = playing;
    }
    if (count == 0) {
        std::cerr << "ERROR: No route stages detected in the selected FM2\n";
        return 1;
    }

    for (size_t i = 0; i < route.stages.size(); ++i) {
        if (!saved[i]) {
            std::cerr << "WARNING: No segment for " << route.stages[i].id << "\n";
            continue;
        }
        const int begin = start_frames[i];
        int end = static_cast<int>(inputs.size());
        for (size_t next = i + 1; next < route.stages.size(); ++next) {
            if (saved[next]) { end = start_frames[next]; break; }
        }
        if (end <= begin) continue;
        const std::vector<sta::nes::ControllerState> segment(
            inputs.begin() + begin + 1, inputs.begin() + end);
        if (segment.empty()) continue;

        sta::nes::FM2Header segment_header = sta::nes::FM2Header::Defaults();
        segment_header.romFilename = header.romFilename;
        segment_header.romChecksum = profile.rom_checksum;
        segment_header.additionalLines.push_back(GhostProfileTag(profile.key));
        segment_header.additionalLines.push_back(GhostStageTag(route.stages[i].id));
        segment_header.additionalLines.push_back(
            "comment ghost-mirror TAS segment " + std::to_string(segment.size()) + " frames");
        const auto path = (std::filesystem::path(profile.directory) /
                           route.stages[i].tas_file).string();
        if (!AtomicWriteFile(path, [&](std::ostream& out) {
            sta::nes::WriteFM2File(out, segment, segment_header);
        })) return 1;
        std::cout << "  " << route.stages[i].id << ": " << segment.size()
                  << " frames -> " << path << "\n";
    }
    std::cout << "Splitter finished in profile " << profile.directory << std::endl;
    return 0;
}

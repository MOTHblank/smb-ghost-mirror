#include "savestate_organizer.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <filesystem>

namespace fs = std::filesystem;

SavestateOrganizer::SavestateOrganizer()
    : selected_index(0)
{
    // any% warpless route
    levels[0] = { "1-1", 0, 0, false, false, false, -1, -1 };
    levels[1] = { "1-2", 0, 1, false, false, false, -1, -1 };
    levels[2] = { "4-1", 3, 0, false, false, false, -1, -1 };
    levels[3] = { "4-2", 3, 1, false, false, false, -1, -1 };
    levels[4] = { "8-1", 7, 0, false, false, false, -1, -1 };
    levels[5] = { "8-2", 7, 1, false, false, false, -1, -1 };
    levels[6] = { "8-3", 7, 2, false, false, false, -1, -1 };
    levels[7] = { "8-4", 7, 3, false, false, false, -1, -1 };
}

void SavestateOrganizer::Init(const std::string& savestates_dir) {
    base_dir = savestates_dir;

    // Ensure directory exists
    fs::create_directories(base_dir);

    Refresh();
    LoadRecords();
}

static int CountFM2Frames(const std::string& filepath) {
    std::ifstream ifs(filepath);
    if (!ifs) return -1;
    std::string line;
    int count = 0;
    while (std::getline(ifs, line)) {
        if (!line.empty() && line[0] == '|') {
            count++;
        }
    }
    return count;
}

void SavestateOrganizer::Refresh() {
    for (int i = 0; i < NUM_LEVELS; i++) {
        levels[i].has_state = fs::exists(StatePath(i));
        levels[i].has_pb    = fs::exists(PBPath(i));
        levels[i].has_tas   = fs::exists(TASPath(i));
        levels[i].tas_frames = CountFM2Frames(TASPath(i));
    }
}

std::string SavestateOrganizer::StatePath(int index) const {
    return base_dir + "/" + levels[index].name + ".state";
}

std::string SavestateOrganizer::PBPath(int index) const {
    return base_dir + "/" + levels[index].name + ".pb.fm2";
}

std::string SavestateOrganizer::TASPath(int index) const {
    return base_dir + "/" + levels[index].name + ".tas.fm2";
}

std::string SavestateOrganizer::RecordsPath() const {
    return base_dir + "/records.txt";
}

void SavestateOrganizer::SelectNext() {
    selected_index = (selected_index + 1) % NUM_LEVELS;
}

void SavestateOrganizer::SelectPrev() {
    selected_index = (selected_index + NUM_LEVELS - 1) % NUM_LEVELS;
}

int SavestateOrganizer::SelectedIndex() const {
    return selected_index;
}

const LevelInfo& SavestateOrganizer::SelectedLevel() const {
    return levels[selected_index];
}

void SavestateOrganizer::UpdateBestTime(int index, int frames) {
    if (index < 0 || index >= NUM_LEVELS) return;
    if (levels[index].best_frames < 0 || frames < levels[index].best_frames) {
        levels[index].best_frames = frames;
        SaveRecords();
        std::cout << "New PB for " << levels[index].name << ": " << frames << " frames" << std::endl;
    }
}

void SavestateOrganizer::LoadRecords() {
    std::ifstream ifs(RecordsPath());
    if (!ifs) return;

    std::string line;
    while (std::getline(ifs, line)) {
        // Format: "level_name frames"
        std::istringstream iss(line);
        std::string name;
        int frames;
        if (iss >> name >> frames) {
            for (int i = 0; i < NUM_LEVELS; i++) {
                if (name == levels[i].name) {
                    levels[i].best_frames = frames;
                    break;
                }
            }
        }
    }
}

void SavestateOrganizer::SaveRecords() {
    std::ofstream ofs(RecordsPath());
    if (!ofs) {
        std::cerr << "WARNING: Cannot write records to " << RecordsPath() << std::endl;
        return;
    }

    for (int i = 0; i < NUM_LEVELS; i++) {
        if (levels[i].best_frames >= 0) {
            ofs << levels[i].name << " " << levels[i].best_frames << "\n";
        }
    }
}

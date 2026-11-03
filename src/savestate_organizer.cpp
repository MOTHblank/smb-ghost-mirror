#include "savestate_organizer.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <filesystem>
#include "atomic_file.h"

namespace fs = std::filesystem;

void SavestateOrganizer::Init(const RouteManifest& manifest) {
    route = manifest;
    levels.clear();
    for (const auto& stage : route.stages) {
        LevelInfo level;
        level.name = stage.id;
        level.world = stage.world;
        level.level = stage.level;
        level.state_file = stage.state_file;
        level.tas_file = stage.tas_file;
        level.pb_file = stage.pb_file;
        levels.push_back(level);
    }
    selected_index = 0;
    base_dir.clear();
    active = false;
}

bool SavestateOrganizer::Activate(const ReferenceProfile& reference) {
    if (!reference.IsConsistent()) {
        std::cerr << "ERROR: Missing or incompatible profile marker; re-split the selected ROM/TAS\n";
        return false;
    }
    const std::string old_stage = levels.empty() ? std::string() : SelectedLevel().name;
    profile = reference;
    base_dir = profile.directory;
    active = true;
    for (auto& level : levels) {
        level.best_frames = -1;
        level.tas_frames = -1;
    }
    const int previous_index = route.Find(old_stage);
    selected_index = previous_index < 0 ? 0 : previous_index;
    LoadRecords();
    Refresh();
    return true;
}

void SavestateOrganizer::Refresh() {
    for (int i = 0; i < Count(); ++i) {
        auto& level = levels[i];
        level.has_state = Ready() && fs::exists(StatePath(i));
        std::vector<sta::nes::ControllerState> replay;
        level.has_tas = Ready() &&
            GhostLoadTaggedReplay(TASPath(i), profile, level.name, &replay);
        level.tas_frames = level.has_tas ? static_cast<int>(replay.size()) : -1;
        replay.clear();
        level.has_pb = Ready() &&
            GhostLoadTaggedReplay(PBPath(i), profile, level.name, &replay);
        level.best_frames = level.has_pb ? static_cast<int>(replay.size()) : -1;
    }
    if (Ready() && !SaveRecords()) {
        std::cerr << "WARNING: PB records cache could not be synchronized\n";
    }
}

std::string SavestateOrganizer::StatePath(int index) const {
    return (fs::path(base_dir) / levels.at(index).state_file).string();
}
std::string SavestateOrganizer::PBPath(int index) const {
    return (fs::path(base_dir) / levels.at(index).pb_file).string();
}
std::string SavestateOrganizer::ExportPath(int index) const {
    return (fs::path(base_dir) / (levels.at(index).name + ".export.fm2")).string();
}
std::string SavestateOrganizer::PracticePath(int index) const {
    return (fs::path(base_dir) / (levels.at(index).name + ".practice.fm2")).string();
}
std::string SavestateOrganizer::TASPath(int index) const {
    return (fs::path(base_dir) / levels.at(index).tas_file).string();
}
std::string SavestateOrganizer::RecordsPath() const {
    return (fs::path(base_dir) / "records.txt").string();
}
void SavestateOrganizer::SelectNext() {
    if (Count()) selected_index = (selected_index + 1) % Count();
}
void SavestateOrganizer::SelectPrev() {
    if (Count()) selected_index = (selected_index + Count() - 1) % Count();
}
int SavestateOrganizer::SelectedIndex() const { return selected_index; }
const LevelInfo& SavestateOrganizer::SelectedLevel() const { return levels.at(selected_index); }

void SavestateOrganizer::UpdateBestTime(int index, int frames) {
    if (!Ready() || index < 0 || index >= Count()) return;
    if (levels[index].best_frames < 0 || frames < levels[index].best_frames) {
        levels[index].best_frames = frames;
        if (!SaveRecords())
            std::cerr << "WARNING: Cannot update records.txt; PB replay is authoritative\n";
        std::cout << "New PB for " << levels[index].name << ": " << frames << " frames\n";
    }
}

void SavestateOrganizer::LoadRecords() {
    if (!Ready()) return;
    std::ifstream ifs(RecordsPath());
    std::string name;
    int frames;
    while (ifs >> name >> frames) {
        for (auto& level : levels) if (name == level.name) level.best_frames = frames;
    }
}

bool SavestateOrganizer::SaveRecords() {
    if (!Ready()) return false;
    return AtomicWriteFile(RecordsPath(), [&](std::ostream& os) {
        for (const auto& level : levels)
            if (level.best_frames >= 0) os << level.name << " " << level.best_frames << "\n";
    });
}

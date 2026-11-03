#ifndef GHOST_MIRROR_SAVESTATE_ORGANIZER_H
#define GHOST_MIRROR_SAVESTATE_ORGANIZER_H

#include <string>
#include <vector>
#include "reference_profile.h"
#include "route_manifest.h"

struct LevelInfo {
    std::string name;
    int world = 0;
    int level = 0;
    std::string state_file, tas_file, pb_file;
    bool has_state = false;
    bool has_pb = false;
    bool has_tas = false;
    int best_frames = -1;
    int tas_frames = -1;
};

// A route-specific view of files belonging to exactly one validated ROM/TAS profile.
struct SavestateOrganizer {
    std::string base_dir;
    ReferenceProfile profile;
    RouteManifest route;
    std::vector<LevelInfo> levels;
    int selected_index = 0;
    bool active = false;

    void Init(const RouteManifest& manifest);
    bool Activate(const ReferenceProfile& reference);
    void Refresh();
    int Count() const { return static_cast<int>(levels.size()); }
    bool Ready() const { return active && profile.IsConsistent(); }

    std::string StatePath(int index) const;
    std::string PBPath(int index) const;
    std::string ExportPath(int index) const;
    std::string PracticePath(int index) const;
    std::string TASPath(int index) const;
    std::string RecordsPath() const;
    void SelectNext();
    void SelectPrev();
    int SelectedIndex() const;
    const LevelInfo& SelectedLevel() const;
    void UpdateBestTime(int index, int frames);
    void LoadRecords();
    bool SaveRecords();
};

#endif

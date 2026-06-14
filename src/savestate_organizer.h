#ifndef GHOST_MIRROR_SAVESTATE_ORGANIZER_H
#define GHOST_MIRROR_SAVESTATE_ORGANIZER_H

#include <string>
#include <vector>
#include <array>

// Manages the savestates/ directory and level selection for the any% route.
struct LevelInfo {
    const char* name;   // "1-1", "1-2", etc.
    int world;          // 0-indexed (0=world 1, 3=world 4, 7=world 8)
    int level;          // 0-indexed
    bool has_state;     // .state file exists
    bool has_pb;        // .pb.fm2 exists
    bool has_tas;       // .tas.fm2 exists
    int best_frames;    // best PB frame count (-1 if none)
    int tas_frames;     // TAS frame count (-1 if none)
};

struct SavestateOrganizer {
    static constexpr int NUM_LEVELS = 8;

    std::string base_dir;  // path to savestates/ directory
    std::array<LevelInfo, NUM_LEVELS> levels;
    int selected_index;

    SavestateOrganizer();

    // Initialize with the base directory and scan for existing files
    void Init(const std::string& savestates_dir);

    // Rescan the directory for file existence
    void Refresh();

    // Get paths for a level
    std::string StatePath(int index) const;
    std::string PBPath(int index) const;
    std::string TASPath(int index) const;
    std::string RecordsPath() const;

    // Level selection
    void SelectNext();
    void SelectPrev();
    int SelectedIndex() const;
    const LevelInfo& SelectedLevel() const;

    // Update best time for a level
    void UpdateBestTime(int index, int frames);

    // Load/save records from/to records.txt
    void LoadRecords();
    void SaveRecords();
};

#endif // GHOST_MIRROR_SAVESTATE_ORGANIZER_H

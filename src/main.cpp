// Ghost Mirror — Triple-Emulator Ghost Racing for SMB
// Main entry point

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <chrono>
#include <thread>
#include <filesystem>
#include <optional>
#include <memory>
#include <utility>
#include <cstdlib>
#include <vector>

#include <SDL.h>

#include "emu_trio.h"
#include "recorder.h"
#include "renderer.h"
#include "input.h"
#include "savestate_organizer.h"
#include "route_manifest.h"
#include "reference_profile.h"
#include "checkpoint_store.h"

// Forward declaration from splitter.cpp
int RunSplitter(const std::string& rom_path, const std::string& fm2_path,
                const RouteManifest& route, const ReferenceProfile& profile);

// SMB RAM addresses
static constexpr uint16_t OPER_MODE       = 0x0770;
static constexpr uint16_t GAME_ENGINE_SUB = 0x000E;
static constexpr uint16_t WORLD_NUMBER    = 0x075F;
static constexpr uint16_t LEVEL_NUMBER    = 0x075C;

// Default paths (relative to exe)
static std::string g_rom_path       = "Super Mario Bros. (World).nes";
static std::string g_fm2_path       = "maru.fm2";
static std::string g_savestates_dir = "savestates";
static std::string g_route_path = "routes/warpless.route";

// NES frame timing (~60.0988 Hz)
static constexpr double NES_FPS = 39375000.0 / 655171.0;
static constexpr double FRAME_DURATION_US = 1000000.0 / NES_FPS;

enum class AppState {
    MENU,
    PLAYING,
};

static bool g_overlay_mode = false;
static int g_max_level_x = 0;
static bool g_reset_player_on_transition = true;
static bool g_level_loading = false;
static bool g_rom_loaded = false;

static std::vector<int> SimulateGhostX(const std::string& rom_data, const std::string& start_state, const std::vector<sta::nes::ControllerState>& inputs) {
    std::vector<int> x_history;
    if (inputs.empty()) return x_history;

    sta::nes::NestopiaNESEmulator temp_emu;
    try {
        temp_emu.LoadINESString(rom_data);
        temp_emu.LoadStateString(start_state);
    } catch (const std::exception& e) {
        fprintf(stderr, "ERROR in ghost simulation setup: %s\n", e.what());
        return x_history;
    }

    x_history.reserve(inputs.size());
    for (size_t f = 0; f < inputs.size(); f++) {
        temp_emu.Execute(inputs[f]);
        int page = temp_emu.CPUPeek(0x071A);
        int scroll_x = temp_emu.CPUPeek(0x071C);
        int mario_x = temp_emu.CPUPeek(0x0086);
        x_history.push_back(page * 256 + scroll_x + mario_x);
    }
    return x_history;
}

// Prefix maxima form a sorted index: binary search still works after backtracking.
static std::vector<int> BuildReachIndex(std::vector<int> history) {
    for (size_t i = 1; i < history.size(); ++i) {
        history[i] = std::max(history[i], history[i - 1]);
    }
    return history;
}

static int CalculateDelta(int player_x, int current_frame, const std::vector<int>& reach_index) {
    if (reach_index.empty()) return 0;
    const auto reached = std::lower_bound(reach_index.begin(), reach_index.end(), player_x);
    return static_cast<int>(reached - reach_index.begin()) - current_frame;
}

static void PrintUsage() {
    printf("Usage: ghost-mirror [options]\n");
    printf("Options:\n");
    printf("  --split <rom> <fm2>   Split a full FM2 into per-level segments\n");
    printf("  --rom <path>          Path to SMB ROM (default: %s)\n", g_rom_path.c_str());
    printf("  --fm2 <path>          TAS reference movie to select\n");
    printf("  --route <path>        Route manifest (default: %s)\n", g_route_path.c_str());
    printf("  --savestates <dir>    Savestate directory (default: %s)\n", g_savestates_dir.c_str());
    printf("  --scale <2|3>         Window scale factor (default: 2)\n");
    printf("  --help                Show this help\n");
}

int main(int argc, char* argv[]) {
    int scale = 2;

    // Parse all options before running the splitter, regardless of argument order.
    std::string split_rom, split_fm2;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--split") == 0 && i + 2 < argc) {
            split_rom = argv[++i];
            split_fm2 = argv[++i];
        } else if (strcmp(argv[i], "--rom") == 0 && i + 1 < argc) {
            g_rom_path = argv[++i];
        } else if (strcmp(argv[i], "--fm2") == 0 && i + 1 < argc) {
            g_fm2_path = argv[++i];
        } else if (strcmp(argv[i], "--route") == 0 && i + 1 < argc) {
            g_route_path = argv[++i];
        } else if (strcmp(argv[i], "--savestates") == 0 && i + 1 < argc) {
            g_savestates_dir = argv[++i];
        } else if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc) {
            scale = atoi(argv[++i]);
            if (scale < 1 || scale > 4) scale = 2;
        } else if (strcmp(argv[i], "--help") == 0) {
            PrintUsage();
            return 0;
        } else {
            fprintf(stderr, "ERROR: Unknown or incomplete argument: %s\n", argv[i]);
            return 1;
        }
    }

    RouteManifest route;
    try {
        route = RouteManifest::Load(g_route_path);
        if (!split_rom.empty()) {
            ReferenceProfile profile = ReferenceProfile::Create(g_savestates_dir, split_rom, split_fm2, route);
            return RunSplitter(split_rom, split_fm2, route, profile);
        }
    } catch (const std::exception& e) {
        fprintf(stderr, "ERROR: %s\n", e.what());
        return 1;
    }

    // Discover ROM and FM2 files in the current directory
    namespace fs = std::filesystem;
    std::vector<std::string> rom_files;
    std::vector<std::string> fm2_files;

    try {
        for (const auto& entry : fs::directory_iterator(".")) {
            if (entry.is_regular_file()) {
                std::string ext = entry.path().extension().string();
                std::string filename = entry.path().filename().string();
                if (ext == ".nes" || ext == ".NES") {
                    rom_files.push_back(filename);
                } else if (ext == ".fm2" || ext == ".FM2") {
                    fm2_files.push_back(filename);
                }
            }
        }
    } catch (const std::exception& e) {
        fprintf(stderr, "WARNING: Directory scan failed: %s\n", e.what());
    }

    if (std::find(rom_files.begin(), rom_files.end(), g_rom_path) == rom_files.end())
        rom_files.insert(rom_files.begin(), g_rom_path);
    if (std::find(fm2_files.begin(), fm2_files.end(), g_fm2_path) == fm2_files.end())
        fm2_files.insert(fm2_files.begin(), g_fm2_path);

    int rom_file_idx = 0;
    int fm2_file_idx = 0;

    for (size_t i = 0; i < rom_files.size(); i++) {
        if (rom_files[i] == g_rom_path) {
            rom_file_idx = static_cast<int>(i);
            break;
        }
    }
    for (size_t i = 0; i < fm2_files.size(); i++) {
        if (fm2_files[i] == g_fm2_path) {
            fm2_file_idx = static_cast<int>(i);
            break;
        }
    }

    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "ERROR: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    InitInput();

    // Initialize components
    Renderer renderer;
    if (!renderer.Init(scale)) {
        ShutdownInput();
        SDL_Quit();
        return 1;
    }

    EmuTrio trio;
    if (trio.LoadROM(g_rom_path)) {
        g_rom_loaded = true;
    } else {
        fprintf(stderr, "WARNING: Default ROM not found ('%s'). Use the menu to select a ROM and split.\n", g_rom_path.c_str());
    }

    Recorder recorder;
    if (g_rom_loaded) recorder.SetROM(g_rom_path, trio.rom_data);
    SavestateOrganizer organizer;
    organizer.Init(route);
    std::optional<ReferenceProfile> active_profile;
    auto ActivateProfile = [&](const ReferenceProfile& reference) {
        if (!organizer.Activate(reference)) return false;
        active_profile = reference;
        recorder.profile_key = reference.key;
        return true;
    };
    try {
        const auto reference = ReferenceProfile::Create(g_savestates_dir, g_rom_path, g_fm2_path, route);
        if (reference.IsConsistent()) ActivateProfile(reference);
    } catch (const std::exception& e) {
        fprintf(stderr, "WARNING: No active reference profile yet: %s\n", e.what());
    }

    AppState app_state = AppState::MENU;
    bool menu_open = true;
    bool running = true;

    enum class MenuSubState {
        LEVEL_SELECT,
        BINDINGS_CONFIG
    };
    MenuSubState menu_sub_state = MenuSubState::LEVEL_SELECT;
    int bindings_selected_row = 0;
    bool in_rebinding = false;

    // Level transition detection state
    int prev_world = -1;
    int prev_level = -1;
    bool was_in_gameplay = false;

    std::vector<int> pb_x_history;
    std::vector<int> tas_x_history;
    bool level_active = false;
    int active_level_idx = 0;
    bool run_assisted = false;
    int checkpoint_slot = 1;
    auto UpdateCaption = [&]() {
        std::string caption = "Ghost Mirror";
        if (level_active && active_level_idx < organizer.Count())
            caption += " | " + organizer.levels[active_level_idx].name +
                       " | Frame " + std::to_string(trio.frame);
        caption += " | Checkpoint " + std::to_string(checkpoint_slot) +
                   "/5" + (run_assisted ? " | PRACTICE (PB locked)" : " | CLEAN");
        SDL_SetWindowTitle(renderer.window, caption.c_str());
    };
    UpdateCaption();

    // Lambda: load a level by index
    auto LoadLevel = [&](int index) {
        if (index < 0 || index >= organizer.Count() || !organizer.Ready()) {
            fprintf(stderr, "No active compatible reference profile. Split/apply ROM and FM2 first.\n");
            return false;
        }
        if (menu_open && (rom_files[rom_file_idx] != g_rom_path ||
                          fm2_files[fm2_file_idx] != g_fm2_path)) {
            fprintf(stderr, "Pending ROM/FM2 selection: press S to apply the selected reference.\n");
            return false;
        }

        // If ROM isn't loaded yet, try loading the currently selected ROM
        if (!g_rom_loaded) {
            std::string selected_rom = rom_files[rom_file_idx];
            if (trio.LoadROM(selected_rom)) {
                g_rom_loaded = true;
                g_rom_path = selected_rom;
                recorder.SetROM(g_rom_path, trio.rom_data);
            } else {
                printf("Cannot load level: no ROM loaded. Select a valid ROM in the menu.\n");
                return false;
            }
        }


        const auto& lvl = organizer.levels[index];
        if (!lvl.has_state) {
            printf("No savestate for %s. Run --split first.\n", lvl.name.c_str());
            return false;
        }

        // Load state into all 3 emus
        if (!trio.LoadState(organizer.StatePath(index))) {
            return false;
        }

        // Load ghost inputs
        trio.LoadPBInputs(organizer.PBPath(index), organizer.profile, lvl.name);
        trio.LoadTASInputs(organizer.TASPath(index), organizer.profile, lvl.name);

        // Start recording
        std::string state_str;
        trio.player->SaveStateString(&state_str);
        recorder.Begin(lvl.name, state_str);

        // Precompute ghost X position histories for HUD deltas
        pb_x_history.clear();
        if (!trio.pb_inputs.empty()) {
            pb_x_history = BuildReachIndex(SimulateGhostX(trio.rom_data, state_str, trio.pb_inputs));
        }
        tas_x_history.clear();
        if (!trio.tas_inputs.empty()) {
            tas_x_history = BuildReachIndex(SimulateGhostX(trio.rom_data, state_str, trio.tas_inputs));
        }

        g_max_level_x = 0;
        if (!tas_x_history.empty()) {
            g_max_level_x = tas_x_history.back();
        } else if (!pb_x_history.empty()) {
            g_max_level_x = pb_x_history.back();
        }

        // Reset detection state
        prev_world = lvl.world;
        prev_level = lvl.level;
        was_in_gameplay = false;
        g_level_loading = false;

        organizer.selected_index = index;
        active_level_idx = index;
        level_active = true;
        run_assisted = false;
        UpdateCaption();
        menu_open = false;
        app_state = AppState::PLAYING;

        printf("Loaded level %s\n", lvl.name.c_str());
        return true;
    };

    // Lambda: save PB for current level
    auto SavePB = [&](bool force) {
        if (!organizer.Ready() || !level_active) return;
        int idx = active_level_idx;
        int frames = recorder.FrameCount();
        if (run_assisted) {
            if (force && recorder.SaveFM2(organizer.PracticePath(idx)))
                printf("Checkpoint-assisted recording exported as PRACTICE ONLY.\n");
            else printf("Checkpoint-assisted attempt is ineligible for PB.\n");
            return;
        }

        if (frames <= 0) return;

        bool is_pb = organizer.levels[idx].best_frames < 0 ||
                     frames < organizer.levels[idx].best_frames;

        if (is_pb) {
            if (!recorder.SaveFM2(organizer.PBPath(idx))) {
                fprintf(stderr, "ERROR: PB replay could not be saved; record unchanged.\n");
                return;
            }
            // The replay is authoritative. Refresh recovers it even if records.txt fails.
            organizer.UpdateBestTime(idx, frames);
            organizer.Refresh();
        } else if (force) {
            // Never replace the best replay with a slower manual recording.
            const auto export_path = organizer.ExportPath(idx);
            if (recorder.SaveFM2(export_path)) {
                printf("Recording exported (PB unchanged): %s\n", export_path.c_str());
            } else {
                fprintf(stderr, "ERROR: Could not export recording.\n");
            }
        } else {
            printf("Not a PB (%d vs %d). Press F8 to export this recording.\n",
                   frames, organizer.levels[idx].best_frames);
        }
    };

    printf("\n=== Ghost Mirror ===\n");
    printf("Controls: Arrows=D-pad, Z=A, X=B, Enter=Start, RShift=Select\n");
    printf("Hotkeys:  1-8=Level, [ ]=Checkpoint Slot, F5=Save, F9=Load, F8=Save PB, F10=Restart, Tab=View, Esc=Menu\n");
    printf("Use --split to generate savestates first.\n\n");

    // Main loop
    using clock = std::chrono::high_resolution_clock;
    auto next_frame_time = clock::now();

    while (running) {
        if (in_rebinding) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) {
                    running = false;
                    in_rebinding = false;
                    break;
                }
                else if (event.type == SDL_KEYDOWN) {
                    if (event.key.repeat) continue;
                    SDL_Scancode code = event.key.keysym.scancode;
                    RebindKeyboard(bindings_selected_row, code);
                    in_rebinding = false;
                    SaveConfig();
                    break;
                }
                else if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                    SDL_GameControllerButton btn = static_cast<SDL_GameControllerButton>(event.cbutton.button);
                    RebindGamepad(bindings_selected_row, btn);
                    in_rebinding = false;
                    SaveConfig();
                    break;
                }
            }
            if (menu_open && menu_sub_state == MenuSubState::BINDINGS_CONFIG) {
                renderer.DrawBindingsMenu(bindings_selected_row, true);
            }
            SDL_Delay(16);
            continue;
        }

        InputState input = PollInput();

        if (input.quit_requested) {
            running = false;
            break;
        }

        // Handle hotkeys
        switch (input.hotkey) {
        case Hotkey::TOGGLE_MENU:
            if (menu_open) {
                if (menu_sub_state == MenuSubState::BINDINGS_CONFIG) {
                    menu_sub_state = MenuSubState::LEVEL_SELECT;
                } else {
                    menu_open = !level_active;
                    if (level_active) {
                        organizer.selected_index = active_level_idx;
                        app_state = AppState::PLAYING;
                    }
                }
            } else {
                menu_open = true;
                app_state = AppState::MENU;
                menu_sub_state = MenuSubState::LEVEL_SELECT;
            }
            break;

        case Hotkey::QUICKSAVE:
            if (app_state == AppState::PLAYING && level_active && organizer.Ready()) {
                PracticeCheckpoint cp;
                cp.profile_key = organizer.profile.key;
                cp.stage = organizer.levels[active_level_idx].name;
                trio.player->SaveStateString(&cp.player_state);
                trio.pb->SaveStateString(&cp.pb_state);
                trio.tas->SaveStateString(&cp.tas_state);
                cp.frame = trio.frame;
                cp.pb_frame = trio.pb_frame_idx;
                cp.tas_frame = trio.tas_frame_idx;
                cp.recorder_inputs = recorder.inputs;
                cp.pb_inputs = trio.pb_inputs;
                cp.tas_inputs = trio.tas_inputs;
                cp.recorder_start_state = recorder.level_start_state;
                cp.recorder_active = recorder.active;
                cp.level_loading = g_level_loading;
                cp.prev_world = prev_world;
                cp.prev_level = prev_level;
                if (CheckpointStore::Save(organizer.base_dir, checkpoint_slot, cp))
                    printf("Checkpoint %d saved for %s at frame %d.\n",
                           checkpoint_slot, cp.stage.c_str(), cp.frame);
                else fprintf(stderr, "ERROR: Checkpoint save failed. Previous slot remains intact.\n");
            }
            break;

        case Hotkey::QUICKLOAD:
            if (organizer.Ready() && (app_state == AppState::PLAYING || menu_open)) {
                if (menu_open && (rom_files[rom_file_idx] != g_rom_path ||
                                  fm2_files[fm2_file_idx] != g_fm2_path)) {
                    printf("Pending ROM/FM2 selection; press S to apply before loading checkpoints.\n");
                    break;
                }
                const int index = menu_open ? organizer.SelectedIndex() : active_level_idx;
                PracticeCheckpoint cp;
                if (!CheckpointStore::Load(organizer.base_dir, organizer.levels[index].name,
                                           checkpoint_slot, organizer.profile.key, &cp)) {
                    printf("No compatible checkpoint in slot %d for %s.\n",
                           checkpoint_slot, organizer.levels[index].name.c_str());
                    break;
                }
                // Validate all three states before modifying live emulators.
                bool valid = true;
                try {
                    sta::nes::NestopiaNESEmulator probe;
                    probe.LoadINESString(trio.rom_data);
                    probe.LoadStateString(cp.player_state);
                    probe.LoadStateString(cp.pb_state);
                    probe.LoadStateString(cp.tas_state);
                } catch (const std::exception& e) {
                    fprintf(stderr, "ERROR: Checkpoint validation failed: %s\n", e.what());
                    valid = false;
                }
                if (!valid) break;
                if ((menu_open || active_level_idx != index) && !LoadLevel(index)) break;
                try {
                    trio.player->LoadStateString(cp.player_state);
                    trio.pb->LoadStateString(cp.pb_state);
                    trio.tas->LoadStateString(cp.tas_state);
                } catch (const std::exception& e) {
                    fprintf(stderr, "ERROR: Checkpoint restore failed: %s\n", e.what());
                    LoadLevel(index);
                    break;
                }
                trio.pb_inputs = std::move(cp.pb_inputs);
                trio.tas_inputs = std::move(cp.tas_inputs);
                trio.frame = cp.frame;
                trio.pb_frame_idx = cp.pb_frame;
                trio.tas_frame_idx = cp.tas_frame;
                recorder.level_id = cp.stage;
                recorder.level_start_state = cp.recorder_start_state;
                recorder.inputs = std::move(cp.recorder_inputs);
                recorder.frame_count = static_cast<int>(recorder.inputs.size());
                recorder.active = cp.recorder_active;
                prev_world = cp.prev_world;
                prev_level = cp.prev_level;
                g_level_loading = cp.level_loading;
                was_in_gameplay = !g_level_loading;
                pb_x_history.clear();
                tas_x_history.clear();
                if (!recorder.level_start_state.empty()) {
                    if (!trio.pb_inputs.empty())
                        pb_x_history = BuildReachIndex(SimulateGhostX(
                            trio.rom_data, recorder.level_start_state, trio.pb_inputs));
                    if (!trio.tas_inputs.empty())
                        tas_x_history = BuildReachIndex(SimulateGhostX(
                            trio.rom_data, recorder.level_start_state, trio.tas_inputs));
                }
                g_max_level_x = !tas_x_history.empty() ? tas_x_history.back() :
                                (!pb_x_history.empty() ? pb_x_history.back() : 0);
                trio.player->ScreenPeekFrame(&trio.player_frame);
                trio.pb->ScreenPeekFrame(&trio.pb_frame);
                trio.tas->ScreenPeekFrame(&trio.tas_frame);
                run_assisted = true;
                UpdateCaption();
                printf("Restored PRACTICE checkpoint %d for %s at frame %d. PB disabled.\n",
                       checkpoint_slot, cp.stage.c_str(), cp.frame);
            }
            break;

        case Hotkey::CHECKPOINT_SLOT_1:
        case Hotkey::CHECKPOINT_SLOT_2:
        case Hotkey::CHECKPOINT_SLOT_3:
        case Hotkey::CHECKPOINT_SLOT_4:
        case Hotkey::CHECKPOINT_SLOT_5:
            checkpoint_slot = static_cast<int>(input.hotkey) -
                              static_cast<int>(Hotkey::CHECKPOINT_SLOT_1) + 1;
            printf("Practice checkpoint slot: %d/%d\n", checkpoint_slot, CheckpointStore::SLOTS);
            UpdateCaption();
            break;

        case Hotkey::CHECKPOINT_PREV:
            checkpoint_slot = (checkpoint_slot + CheckpointStore::SLOTS - 2) %
                              CheckpointStore::SLOTS + 1;
            printf("Practice checkpoint slot: %d/%d\n", checkpoint_slot, CheckpointStore::SLOTS);
            UpdateCaption();
            break;

        case Hotkey::CHECKPOINT_NEXT:
            checkpoint_slot = checkpoint_slot % CheckpointStore::SLOTS + 1;
            printf("Practice checkpoint slot: %d/%d\n", checkpoint_slot, CheckpointStore::SLOTS);
            UpdateCaption();
            break;

        case Hotkey::SAVE_PB:
            if (menu_open) {
                if (menu_sub_state == MenuSubState::LEVEL_SELECT) {
                    menu_sub_state = MenuSubState::BINDINGS_CONFIG;
                    bindings_selected_row = 0;
                } else {
                    menu_sub_state = MenuSubState::LEVEL_SELECT;
                }
            } else {
                if (app_state == AppState::PLAYING) SavePB(true);
            }
            break;

        case Hotkey::CONFIGURE_BINDINGS:
            if (menu_open) {
                if (menu_sub_state == MenuSubState::LEVEL_SELECT) {
                    menu_sub_state = MenuSubState::BINDINGS_CONFIG;
                    bindings_selected_row = 0;
                } else {
                    menu_sub_state = MenuSubState::LEVEL_SELECT;
                }
            }
            break;

        case Hotkey::RESTART_LEVEL:
            if (app_state == AppState::PLAYING) {
                recorder.Discard();
                LoadLevel(active_level_idx);
            }
            break;

        case Hotkey::TOGGLE_VIEW_MODE:
            g_overlay_mode = !g_overlay_mode;
            renderer.SetOverlayMode(g_overlay_mode);
            printf("Toggled view mode: %s\n", g_overlay_mode ? "OVERLAY" : "3-SCREEN");
            break;

        case Hotkey::LEVEL_1: LoadLevel(0); break;
        case Hotkey::LEVEL_2: LoadLevel(1); break;
        case Hotkey::LEVEL_3: LoadLevel(2); break;
        case Hotkey::LEVEL_4: LoadLevel(3); break;
        case Hotkey::LEVEL_5: LoadLevel(4); break;
        case Hotkey::LEVEL_6: LoadLevel(5); break;
        case Hotkey::LEVEL_7: LoadLevel(6); break;
        case Hotkey::LEVEL_8: LoadLevel(7); break;

        case Hotkey::MENU_LEFT:
            if (menu_open) organizer.SelectPrev();
            break;
        case Hotkey::MENU_RIGHT:
            if (menu_open) organizer.SelectNext();
            break;
        case Hotkey::MENU_CONFIRM:
            if (menu_open) {
                if (rom_files[rom_file_idx] != g_rom_path || fm2_files[fm2_file_idx] != g_fm2_path)
                    printf("ROM/FM2 selection pending. Press S to split and apply first.\n");
                else LoadLevel(organizer.SelectedIndex());
            }
            break;

        case Hotkey::CYCLE_ROM:
            if (menu_open && menu_sub_state == MenuSubState::LEVEL_SELECT && !rom_files.empty()) {
                rom_file_idx = (rom_file_idx + 1) % rom_files.size();
                printf("Cycled ROM to: %s\n", rom_files[rom_file_idx].c_str());
            }
            break;
        case Hotkey::CYCLE_FM2:
            if (menu_open && menu_sub_state == MenuSubState::LEVEL_SELECT && !fm2_files.empty()) {
                fm2_file_idx = (fm2_file_idx + 1) % fm2_files.size();
                printf("Cycled FM2 reference to: %s\n", fm2_files[fm2_file_idx].c_str());
            }
            break;
        case Hotkey::RUN_SPLIT:
            if (menu_open && menu_sub_state == MenuSubState::LEVEL_SELECT) {
                std::string new_rom = rom_files[rom_file_idx];
                std::string new_fm2 = fm2_files[fm2_file_idx];
                printf("RE-SPLITTING! ROM: %s, FM2: %s...\n", new_rom.c_str(), new_fm2.c_str());

                // The active game is retained if reference splitting fails.

                // Run splitter
                int split_res = 1;
                std::optional<ReferenceProfile> new_profile;
                try {
                    new_profile = ReferenceProfile::Create(g_savestates_dir, new_rom, new_fm2, route);
                    split_res = RunSplitter(new_rom, new_fm2, route, *new_profile);
                } catch (const std::exception& e) {
                    fprintf(stderr, "ERROR applying reference: %s\n", e.what());
                }
                if (split_res == 0) {
                    // Update paths and reload ROM
                    if (trio.LoadROM(new_rom) && new_profile && ActivateProfile(*new_profile)) {
                        g_rom_loaded = true;
                        g_rom_path = new_rom;
                        g_fm2_path = new_fm2;
                        recorder.SetROM(g_rom_path, trio.rom_data);
                        if (LoadLevel(organizer.selected_index))
                            printf("Split & Load Successful!\n");
                        else printf("Split completed; select a stage with a saved TAS state.\n");
                    } else {
                        g_rom_loaded = false;
                        level_active = false;
                        menu_open = true;
                        app_state = AppState::MENU;
                        fprintf(stderr, "ERROR: Split completed but profile could not be activated.\n");
                    }
                } else {
                    printf("ERROR: Split failed.\n");
                }
            }
            break;

        case Hotkey::TOGGLE_RESET_PLAYER:
            if (menu_open && menu_sub_state == MenuSubState::LEVEL_SELECT) {
                g_reset_player_on_transition = !g_reset_player_on_transition;
                printf("Toggled reset player on transition: %s\n", g_reset_player_on_transition ? "ON" : "OFF");
            }
            break;

        default: break;
        }

        // Menu navigation with arrow keys when menu is open
        if (menu_open) {
            if (menu_sub_state == MenuSubState::BINDINGS_CONFIG) {
                static bool up_was_pressed = false;
                static bool down_was_pressed = false;
                static bool confirm_was_pressed = false;
                static bool back_was_pressed = false;

                bool up_pressed = (input.controller & sta::nes::Button::UP) != 0;
                bool down_pressed = (input.controller & sta::nes::Button::DOWN) != 0;
                bool confirm_pressed = (input.controller & sta::nes::Button::A) != 0 || 
                                       (input.controller & sta::nes::Button::START) != 0 ||
                                       (input.hotkey == Hotkey::MENU_CONFIRM);
                bool back_pressed = (input.controller & sta::nes::Button::B) != 0 || 
                                    (input.controller & sta::nes::Button::SELECT) != 0;

                if (up_pressed && !up_was_pressed) {
                    bindings_selected_row = (bindings_selected_row - 1 + 14) % 14;
                }
                if (down_pressed && !down_was_pressed) {
                    bindings_selected_row = (bindings_selected_row + 1) % 14;
                }
                if (confirm_pressed && !confirm_was_pressed) {
                    in_rebinding = true;
                }
                if (back_pressed && !back_was_pressed) {
                    menu_sub_state = MenuSubState::LEVEL_SELECT;
                }

                up_was_pressed = up_pressed;
                down_was_pressed = down_pressed;
                confirm_was_pressed = confirm_pressed;
                back_was_pressed = back_pressed;

                renderer.DrawBindingsMenu(bindings_selected_row, false);
            }
            else {
                // Draw menu
                const int count = organizer.Count();
                std::vector<const char*> names(count);
                std::unique_ptr<bool[]> has_pb(new bool[count]);
                std::unique_ptr<bool[]> has_tas(new bool[count]);
                std::vector<int> best_frames(count), tas_frames(count);
                for (int i = 0; i < count; ++i) {
                    names[i] = organizer.levels[i].name.c_str();
                    has_pb[i] = organizer.levels[i].has_pb;
                    has_tas[i] = organizer.levels[i].has_tas;
                    best_frames[i] = organizer.levels[i].best_frames;
                    tas_frames[i] = organizer.levels[i].tas_frames;
                }
                renderer.DrawMenu(
                    organizer.SelectedIndex(),
                    names.data(),
                    count,
                    has_pb.get(),
                    has_tas.get(),
                    best_frames.data(),
                    tas_frames.data(),
                    rom_files[rom_file_idx].c_str(),
                    fm2_files[fm2_file_idx].c_str(),
                    g_reset_player_on_transition,
                    checkpoint_slot,
                    route.name.c_str()
                );
            }

            // Sleep at menu rate
            SDL_Delay(16);
            continue;
        }

        if (app_state == AppState::PLAYING) {
            // Check for level transition on player emu
            uint8_t oper_mode  = trio.PlayerPeek(OPER_MODE);
            uint8_t engine_sub = trio.PlayerPeek(GAME_ENGINE_SUB);
            uint8_t world      = trio.PlayerPeek(WORLD_NUMBER);
            uint8_t level      = trio.PlayerPeek(LEVEL_NUMBER);

            bool now_in_gameplay = (oper_mode == 1 && engine_sub == 0);

            // Level transition detection:
            // We detect a transition if the world/level RAM variables change to a new route level
            if (world != prev_world || level != prev_level) {
                // Find the route index for the new world/level
                int next_idx = -1;
                for (int i = 0; i < organizer.Count(); i++) {
                    if (organizer.levels[i].world == world &&
                        organizer.levels[i].level == level)
                    {
                        next_idx = i;
                        break;
                    }
                }

                if (next_idx >= 0 && next_idx != active_level_idx &&
                    organizer.route.AllowsTransition(active_level_idx, next_idx) &&
                    organizer.levels[next_idx].has_state) {
                    printf("Level transition detected: %d-%d → %d-%d\n",
                           prev_world + 1, prev_level + 1, world + 1, level + 1);

                    recorder.Finish();
                    SavePB(false);
                    const bool assisted_before_transition = run_assisted;

                    if (g_reset_player_on_transition) {
                        // Reset player directly to next level's start state (skips black screen)
                        if (LoadLevel(next_idx)) {
                            // Loading a new level during an assisted route cannot wash its PB taint.
                            run_assisted = assisted_before_transition;
                            UpdateCaption();
                        }
                    } else {
                        // Start a new stage's recording after gameplay resumes. Do not
                        // preserve the completed stage's inputs in transition checkpoints.
                        recorder.Discard();
                        recorder.level_id = organizer.levels[next_idx].name;
                        recorder.level_start_state.clear();
                        // Play naturally. Retain player state, load PB/TAS ghosts level states and freeze them
                        trio.pb->LoadStateFile(organizer.StatePath(next_idx));
                        trio.tas->LoadStateFile(organizer.StatePath(next_idx));

                        // Load ghost inputs
                        trio.LoadPBInputs(organizer.PBPath(next_idx), organizer.profile, organizer.levels[next_idx].name);
                        trio.LoadTASInputs(organizer.TASPath(next_idx), organizer.profile, organizer.levels[next_idx].name);

                        // Sync IntervalTimerControl ($077F) and RNG registers ($07A7-$07AD) from TAS state to player
                        uint8_t tas_timer = trio.tas->CPUPeek(0x077F);
                        trio.player->CPUPoke(0x077F, tas_timer);
                        for (uint16_t addr = 0x07A7; addr <= 0x07AD; addr++) {
                            trio.player->CPUPoke(addr, trio.tas->CPUPeek(addr));
                        }

                        // Reset frame indices (trio.frame is player's frame, which stays 0 until gameplay starts)
                        trio.frame = 0;
                        trio.pb_frame_idx = 0;
                        trio.tas_frame_idx = 0;

                        g_level_loading = true;
                        organizer.selected_index = next_idx;
                        active_level_idx = next_idx;

                        pb_x_history.clear();
                        tas_x_history.clear();
                        g_max_level_x = 0;

                        printf("Natural transition: Loaded ghosts for %s. Waiting for player gameplay...\n", organizer.levels[next_idx].name.c_str());
                    }

                    prev_world = world;
                    prev_level = level;
                }
            }

            // If we are playing naturally and in loader screen, wait until active gameplay start to begin recording
            if (g_level_loading) {
                if (now_in_gameplay) {
                    // Player has reached active gameplay! Start recording now.
                    g_level_loading = false;

                    std::string player_start_state;
                    trio.player->SaveStateString(&player_start_state);
                    recorder.Begin(organizer.levels[active_level_idx].name, player_start_state);

                    // Precompute ghost X histories for HUD deltas starting from active gameplay frame 0
                    if (!trio.pb_inputs.empty()) {
                        pb_x_history = BuildReachIndex(SimulateGhostX(trio.rom_data, player_start_state, trio.pb_inputs));
                    }
                    if (!trio.tas_inputs.empty()) {
                        tas_x_history = BuildReachIndex(SimulateGhostX(trio.rom_data, player_start_state, trio.tas_inputs));
                    }

                    g_max_level_x = 0;
                    if (!tas_x_history.empty()) {
                        g_max_level_x = tas_x_history.back();
                    } else if (!pb_x_history.empty()) {
                        g_max_level_x = pb_x_history.back();
                    }

                    printf("Gameplay started for %s! Ghosts and recorder active.\n", organizer.levels[active_level_idx].name.c_str());
                }
            }

            // Step emulators and record player input
            if (g_level_loading) {
                // Hold ghosts, step player only
                trio.StepFrame(input.controller, false, false);
            } else {
                // Gameplay active: step player (record input) and step ghosts
                recorder.Append(input.controller);
                trio.StepFrame(input.controller, true, true);
            }

            was_in_gameplay = now_in_gameplay;

            // Calculate Mario absolute X position and HUD deltas
            int player_x = (trio.player->CPUPeek(0x071A) * 256) + trio.player->CPUPeek(0x071C) + trio.player->CPUPeek(0x0086);

            int pb_delta = 0;
            bool has_pb = !trio.pb_inputs.empty();
            if (has_pb) {
                pb_delta = CalculateDelta(player_x, trio.pb_frame_idx, pb_x_history);
            }

            int tas_delta = 0;
            bool has_tas = !trio.tas_inputs.empty();
            if (has_tas) {
                tas_delta = CalculateDelta(player_x, trio.tas_frame_idx, tas_x_history);
            }

            // Draw
            renderer.DrawFrame(
                trio.player_frame,
                trio.pb_frame,
                trio.tas_frame,
                trio.frame,
                static_cast<int>(trio.pb_inputs.size()),
                static_cast<int>(trio.tas_inputs.size()),
                organizer.levels[active_level_idx].name.c_str(),
                false,
                pb_delta,
                has_pb,
                tas_delta,
                has_tas,
                g_overlay_mode,
                trio.player.get(),
                trio.pb.get(),
                trio.tas.get(),
                g_max_level_x
            );
            // DrawFrame refreshes the window title; keep checkpoint/practice status visible.
            UpdateCaption();

            // Frame timing — maintain NES cadence
            next_frame_time += std::chrono::microseconds(static_cast<int64_t>(FRAME_DURATION_US));
            auto now = clock::now();
            if (next_frame_time > now) {
                std::this_thread::sleep_until(next_frame_time);
            } else {
                // Behind schedule — reset timing
                next_frame_time = clock::now();
            }
        }
    }

    renderer.Shutdown();
    ShutdownInput();
    SDL_Quit();
    return 0;
}

#ifndef GHOST_MIRROR_INPUT_H
#define GHOST_MIRROR_INPUT_H

#include <SDL.h>
#include "nes/nes.h"

#include <string>

// Hotkey actions returned by the input handler
enum class Hotkey {
    NONE,
    QUICKSAVE,        // F5
    QUICKLOAD,        // F9
    SAVE_PB,          // F8
    RESTART_LEVEL,    // F10
    TOGGLE_VIEW_MODE, // Tab
    TOGGLE_MENU,      // Escape
    QUIT,             // window close / Alt+F4
    LEVEL_1,        // F1  → 1-1
    LEVEL_2,        // F2  → 1-2
    LEVEL_3,        // F3  → 4-1
    LEVEL_4,        // F4  → 4-2
    LEVEL_5,        // F5+Shift or dedicated → 8-1 (we use number keys instead)
    LEVEL_6,        // 8-2
    LEVEL_7,        // 8-3
    LEVEL_8,        // 8-4
    MENU_UP,
    MENU_DOWN,
    MENU_LEFT,
    MENU_RIGHT,
    MENU_CONFIRM,
    CYCLE_ROM,
    CYCLE_FM2,
    RUN_SPLIT,
    CONFIGURE_BINDINGS,
};

struct InputState {
    sta::nes::ControllerState controller;  // current NES button state
    Hotkey hotkey;                          // single-shot hotkey this frame
    bool quit_requested;
};

// Keyboard scan codes mapping
struct KeyBindings {
    SDL_Scancode nes_up     = SDL_SCANCODE_UP;
    SDL_Scancode nes_down   = SDL_SCANCODE_DOWN;
    SDL_Scancode nes_left   = SDL_SCANCODE_LEFT;
    SDL_Scancode nes_right  = SDL_SCANCODE_RIGHT;
    SDL_Scancode nes_a      = SDL_SCANCODE_Z;
    SDL_Scancode nes_b      = SDL_SCANCODE_X;
    SDL_Scancode nes_start  = SDL_SCANCODE_RETURN;
    SDL_Scancode nes_select = SDL_SCANCODE_RSHIFT;

    SDL_Scancode hk_quicksave = SDL_SCANCODE_F5;
    SDL_Scancode hk_quickload = SDL_SCANCODE_F9;
    SDL_Scancode hk_save_pb   = SDL_SCANCODE_F8;
    SDL_Scancode hk_restart   = SDL_SCANCODE_F10;
    SDL_Scancode hk_view_mode = SDL_SCANCODE_TAB;
    SDL_Scancode hk_menu      = SDL_SCANCODE_ESCAPE;
};

// Gamepad button mapping
struct GamepadBindings {
    SDL_GameControllerButton nes_up     = SDL_CONTROLLER_BUTTON_DPAD_UP;
    SDL_GameControllerButton nes_down   = SDL_CONTROLLER_BUTTON_DPAD_DOWN;
    SDL_GameControllerButton nes_left   = SDL_CONTROLLER_BUTTON_DPAD_LEFT;
    SDL_GameControllerButton nes_right  = SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
    SDL_GameControllerButton nes_a      = SDL_CONTROLLER_BUTTON_A;
    SDL_GameControllerButton nes_b      = SDL_CONTROLLER_BUTTON_X;
    SDL_GameControllerButton nes_start  = SDL_CONTROLLER_BUTTON_START;
    SDL_GameControllerButton nes_select = SDL_CONTROLLER_BUTTON_BACK;

    SDL_GameControllerButton hk_quicksave = SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
    SDL_GameControllerButton hk_quickload = SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
    SDL_GameControllerButton hk_save_pb   = SDL_CONTROLLER_BUTTON_Y;
    SDL_GameControllerButton hk_restart   = SDL_CONTROLLER_BUTTON_LEFTSTICK;
    SDL_GameControllerButton hk_view_mode = SDL_CONTROLLER_BUTTON_RIGHTSTICK;
    SDL_GameControllerButton hk_menu      = SDL_CONTROLLER_BUTTON_BACK;
};

extern KeyBindings g_keys;
extern GamepadBindings g_gamepad;

// Initialize bindings and gamepads
void InitInput();

// Shutdown/cleanup gamepads
void ShutdownInput();

// Process all pending SDL events and return the current input state
InputState PollInput();

// Save the current config to bindings.cfg
void SaveConfig();

// Retrieve names of mapped key/button for actions (0-13)
std::string GetActionKeyName(int action_idx);
std::string GetActionButtonName(int action_idx);

// Rebind functions for keyboard and gamepad
void RebindKeyboard(int action_idx, SDL_Scancode code);
void RebindGamepad(int action_idx, SDL_GameControllerButton btn);

#endif // GHOST_MIRROR_INPUT_H

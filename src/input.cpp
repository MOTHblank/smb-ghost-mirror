#include "input.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

// Global bindings objects
KeyBindings g_keys;
GamepadBindings g_gamepad;

// Active gamepad device
static SDL_GameController* s_controller_device = nullptr;
static uint8_t s_controller = 0x00;

static std::string Trim(std::string str) {
    str.erase(0, str.find_first_not_of(" \t\r\n"));
    if (str.empty()) return str;
    str.erase(str.find_last_not_of(" \t\r\n") + 1);
    return str;
}

static void SaveDefaultConfig() {
    std::ofstream ofs("bindings.cfg");
    if (!ofs) return;

    ofs << "# Ghost Mirror Keyboard & Gamepad Bindings Configuration\n";
    ofs << "# Keyboard key names use standard SDL scancode names (e.g. A, B, Z, X, Up, Down, F5, F9, F8, F10, Tab, Escape, Return, Space)\n";
    ofs << "# Gamepad button names use standard SDL button names (e.g. a, b, x, y, back, start, leftshoulder, rightshoulder, leftstick, rightstick, dpup, dpdown, dpleft, dpright)\n\n";

    ofs << "[KeyboardNES]\n";
    ofs << "UP = Up\n";
    ofs << "DOWN = Down\n";
    ofs << "LEFT = Left\n";
    ofs << "RIGHT = Right\n";
    ofs << "A = Z\n";
    ofs << "B = X\n";
    ofs << "START = Return\n";
    ofs << "SELECT = Right Shift\n\n";

    ofs << "[KeyboardHotkeys]\n";
    ofs << "QUICKSAVE = F5\n";
    ofs << "QUICKLOAD = F9\n";
    ofs << "SAVE_PB = F8\n";
    ofs << "RESTART_LEVEL = F10\n";
    ofs << "TOGGLE_VIEW = Tab\n";
    ofs << "TOGGLE_MENU = Escape\n\n";

    ofs << "[GamepadNES]\n";
    ofs << "UP = dpup\n";
    ofs << "DOWN = dpdown\n";
    ofs << "LEFT = dpleft\n";
    ofs << "RIGHT = dpright\n";
    ofs << "A = a\n";
    ofs << "B = x\n";
    ofs << "START = start\n";
    ofs << "SELECT = back\n\n";

    ofs << "[GamepadHotkeys]\n";
    ofs << "QUICKSAVE = leftshoulder\n";
    ofs << "QUICKLOAD = rightshoulder\n";
    ofs << "SAVE_PB = y\n";
    ofs << "RESTART_LEVEL = leftstick\n";
    ofs << "TOGGLE_VIEW = rightstick\n";
    ofs << "TOGGLE_MENU = back\n";

    ofs.close();
    std::cout << "Created default bindings.cfg!" << std::endl;
}

static void LoadConfig() {
    std::ifstream ifs("bindings.cfg");
    if (!ifs) {
        SaveDefaultConfig();
        return;
    }

    std::string line;
    std::string section = "";
    while (std::getline(ifs, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#') continue;

        if (line[0] == '[' && line[line.length() - 1] == ']') {
            section = Trim(line.substr(1, line.length() - 2));
            continue;
        }

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = Trim(line.substr(0, eq));
        std::string val = Trim(line.substr(eq + 1));

        if (section == "KeyboardNES") {
            SDL_Scancode code = SDL_GetScancodeFromName(val.c_str());
            if (code != SDL_SCANCODE_UNKNOWN) {
                if (key == "UP") g_keys.nes_up = code;
                else if (key == "DOWN") g_keys.nes_down = code;
                else if (key == "LEFT") g_keys.nes_left = code;
                else if (key == "RIGHT") g_keys.nes_right = code;
                else if (key == "A") g_keys.nes_a = code;
                else if (key == "B") g_keys.nes_b = code;
                else if (key == "START") g_keys.nes_start = code;
                else if (key == "SELECT") g_keys.nes_select = code;
            }
        }
        else if (section == "KeyboardHotkeys") {
            SDL_Scancode code = SDL_GetScancodeFromName(val.c_str());
            if (code != SDL_SCANCODE_UNKNOWN) {
                if (key == "QUICKSAVE") g_keys.hk_quicksave = code;
                else if (key == "QUICKLOAD") g_keys.hk_quickload = code;
                else if (key == "SAVE_PB") g_keys.hk_save_pb = code;
                else if (key == "RESTART_LEVEL") g_keys.hk_restart = code;
                else if (key == "TOGGLE_VIEW") g_keys.hk_view_mode = code;
                else if (key == "TOGGLE_MENU") g_keys.hk_menu = code;
            }
        }
        else if (section == "GamepadNES") {
            SDL_GameControllerButton btn = SDL_GameControllerGetButtonFromString(val.c_str());
            if (btn != SDL_CONTROLLER_BUTTON_INVALID) {
                if (key == "UP") g_gamepad.nes_up = btn;
                else if (key == "DOWN") g_gamepad.nes_down = btn;
                else if (key == "LEFT") g_gamepad.nes_left = btn;
                else if (key == "RIGHT") g_gamepad.nes_right = btn;
                else if (key == "A") g_gamepad.nes_a = btn;
                else if (key == "B") g_gamepad.nes_b = btn;
                else if (key == "START") g_gamepad.nes_start = btn;
                else if (key == "SELECT") g_gamepad.nes_select = btn;
            }
        }
        else if (section == "GamepadHotkeys") {
            SDL_GameControllerButton btn = SDL_GameControllerGetButtonFromString(val.c_str());
            if (btn != SDL_CONTROLLER_BUTTON_INVALID) {
                if (key == "QUICKSAVE") g_gamepad.hk_quicksave = btn;
                else if (key == "QUICKLOAD") g_gamepad.hk_quickload = btn;
                else if (key == "SAVE_PB") g_gamepad.hk_save_pb = btn;
                else if (key == "RESTART_LEVEL") g_gamepad.hk_restart = btn;
                else if (key == "TOGGLE_VIEW") g_gamepad.hk_view_mode = btn;
                else if (key == "TOGGLE_MENU") g_gamepad.hk_menu = btn;
            }
        }
    }
    ifs.close();
    std::cout << "Bindings configuration loaded from bindings.cfg" << std::endl;
}

void InitInput() {
    LoadConfig();

    // Open first connected gamepad
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) {
            s_controller_device = SDL_GameControllerOpen(i);
            if (s_controller_device) {
                std::cout << "Opened game controller: " << SDL_GameControllerName(s_controller_device) << std::endl;
                break;
            }
        }
    }
}

void ShutdownInput() {
    if (s_controller_device) {
        SDL_GameControllerClose(s_controller_device);
        s_controller_device = nullptr;
    }
}

InputState PollInput() {
    InputState state = {};
    state.hotkey = Hotkey::NONE;
    state.quit_requested = false;

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_QUIT:
            state.quit_requested = true;
            state.hotkey = Hotkey::QUIT;
            break;

        case SDL_CONTROLLERDEVICEADDED:
            if (!s_controller_device) {
                s_controller_device = SDL_GameControllerOpen(event.cdevice.which);
                if (s_controller_device) {
                    std::cout << "Opened game controller: " << SDL_GameControllerName(s_controller_device) << std::endl;
                }
            }
            break;

        case SDL_CONTROLLERDEVICEREMOVED:
            if (s_controller_device) {
                SDL_Joystick* joystick = SDL_GameControllerGetJoystick(s_controller_device);
                if (joystick && SDL_JoystickInstanceID(joystick) == event.cdevice.which) {
                    SDL_GameControllerClose(s_controller_device);
                    s_controller_device = nullptr;
                    std::cout << "Game controller disconnected." << std::endl;
                }
            }
            break;

        case SDL_KEYDOWN:
            if (event.key.repeat) break;

            if (event.key.keysym.scancode == g_keys.nes_up)         s_controller |= sta::nes::Button::UP;
            else if (event.key.keysym.scancode == g_keys.nes_down)   s_controller |= sta::nes::Button::DOWN;
            else if (event.key.keysym.scancode == g_keys.nes_left) {
                s_controller |= sta::nes::Button::LEFT;
                state.hotkey = Hotkey::MENU_LEFT;
            }
            else if (event.key.keysym.scancode == g_keys.nes_right) {
                s_controller |= sta::nes::Button::RIGHT;
                state.hotkey = Hotkey::MENU_RIGHT;
            }
            else if (event.key.keysym.scancode == g_keys.nes_a)      s_controller |= sta::nes::Button::A;
            else if (event.key.keysym.scancode == g_keys.nes_b)      s_controller |= sta::nes::Button::B;
            else if (event.key.keysym.scancode == g_keys.nes_start)  s_controller |= sta::nes::Button::START;
            else if (event.key.keysym.scancode == g_keys.nes_select) s_controller |= sta::nes::Button::SELECT;

            // Hotkeys
            else if (event.key.keysym.scancode == g_keys.hk_quicksave) state.hotkey = Hotkey::QUICKSAVE;
            else if (event.key.keysym.scancode == g_keys.hk_quickload) state.hotkey = Hotkey::QUICKLOAD;
            else if (event.key.keysym.scancode == g_keys.hk_save_pb)   state.hotkey = Hotkey::SAVE_PB;
            else if (event.key.keysym.scancode == g_keys.hk_restart)   state.hotkey = Hotkey::RESTART_LEVEL;
            else if (event.key.keysym.scancode == g_keys.hk_view_mode) state.hotkey = Hotkey::TOGGLE_VIEW_MODE;
            else if (event.key.keysym.scancode == g_keys.hk_menu)      state.hotkey = Hotkey::TOGGLE_MENU;

            // Level quick-jump (1-8 keys)
            else if (event.key.keysym.scancode == SDL_SCANCODE_1)      state.hotkey = Hotkey::LEVEL_1;
            else if (event.key.keysym.scancode == SDL_SCANCODE_2)      state.hotkey = Hotkey::LEVEL_2;
            else if (event.key.keysym.scancode == SDL_SCANCODE_3)      state.hotkey = Hotkey::LEVEL_3;
            else if (event.key.keysym.scancode == SDL_SCANCODE_4)      state.hotkey = Hotkey::LEVEL_4;
            else if (event.key.keysym.scancode == SDL_SCANCODE_5)      state.hotkey = Hotkey::LEVEL_5;
            else if (event.key.keysym.scancode == SDL_SCANCODE_6)      state.hotkey = Hotkey::LEVEL_6;
            else if (event.key.keysym.scancode == SDL_SCANCODE_7)      state.hotkey = Hotkey::LEVEL_7;
            else if (event.key.keysym.scancode == SDL_SCANCODE_8)      state.hotkey = Hotkey::LEVEL_8;

            // Menu shortcuts
            else if (event.key.keysym.scancode == SDL_SCANCODE_SPACE)  state.hotkey = Hotkey::MENU_CONFIRM;
            else if (event.key.keysym.scancode == SDL_SCANCODE_R)      state.hotkey = Hotkey::CYCLE_ROM;
            else if (event.key.keysym.scancode == SDL_SCANCODE_F)      state.hotkey = Hotkey::CYCLE_FM2;
            else if (event.key.keysym.scancode == SDL_SCANCODE_S)      state.hotkey = Hotkey::RUN_SPLIT;
            else if (event.key.keysym.scancode == SDL_SCANCODE_C)      state.hotkey = Hotkey::CONFIGURE_BINDINGS;
            else if (event.key.keysym.scancode == SDL_SCANCODE_T)      state.hotkey = Hotkey::TOGGLE_RESET_PLAYER;
            break;

        case SDL_KEYUP:
            if (event.key.keysym.scancode == g_keys.nes_up)         s_controller &= ~sta::nes::Button::UP;
            else if (event.key.keysym.scancode == g_keys.nes_down)   s_controller &= ~sta::nes::Button::DOWN;
            else if (event.key.keysym.scancode == g_keys.nes_left)   s_controller &= ~sta::nes::Button::LEFT;
            else if (event.key.keysym.scancode == g_keys.nes_right)  s_controller &= ~sta::nes::Button::RIGHT;
            else if (event.key.keysym.scancode == g_keys.nes_a)      s_controller &= ~sta::nes::Button::A;
            else if (event.key.keysym.scancode == g_keys.nes_b)      s_controller &= ~sta::nes::Button::B;
            else if (event.key.keysym.scancode == g_keys.nes_start)  s_controller &= ~sta::nes::Button::START;
            else if (event.key.keysym.scancode == g_keys.nes_select) s_controller &= ~sta::nes::Button::SELECT;
            break;

        case SDL_CONTROLLERBUTTONDOWN:
            if (event.cbutton.button == g_gamepad.nes_up)         s_controller |= sta::nes::Button::UP;
            else if (event.cbutton.button == g_gamepad.nes_down)   s_controller |= sta::nes::Button::DOWN;
            else if (event.cbutton.button == g_gamepad.nes_left) {
                s_controller |= sta::nes::Button::LEFT;
                state.hotkey = Hotkey::MENU_LEFT;
            }
            else if (event.cbutton.button == g_gamepad.nes_right) {
                s_controller |= sta::nes::Button::RIGHT;
                state.hotkey = Hotkey::MENU_RIGHT;
            }
            else if (event.cbutton.button == g_gamepad.nes_a)      s_controller |= sta::nes::Button::A;
            else if (event.cbutton.button == g_gamepad.nes_b)      s_controller |= sta::nes::Button::B;
            else if (event.cbutton.button == g_gamepad.nes_start) {
                s_controller |= sta::nes::Button::START;
                state.hotkey = Hotkey::MENU_CONFIRM;
            }
            else if (event.cbutton.button == g_gamepad.nes_select)  s_controller |= sta::nes::Button::SELECT;

            // Hotkeys
            else if (event.cbutton.button == g_gamepad.hk_quicksave) state.hotkey = Hotkey::QUICKSAVE;
            else if (event.cbutton.button == g_gamepad.hk_quickload) state.hotkey = Hotkey::QUICKLOAD;
            else if (event.cbutton.button == g_gamepad.hk_save_pb)   state.hotkey = Hotkey::SAVE_PB;
            else if (event.cbutton.button == g_gamepad.hk_restart)   state.hotkey = Hotkey::RESTART_LEVEL;
            else if (event.cbutton.button == g_gamepad.hk_view_mode) state.hotkey = Hotkey::TOGGLE_VIEW_MODE;
            else if (event.cbutton.button == g_gamepad.hk_menu)      state.hotkey = Hotkey::TOGGLE_MENU;
            break;

        case SDL_CONTROLLERBUTTONUP:
            if (event.cbutton.button == g_gamepad.nes_up)         s_controller &= ~sta::nes::Button::UP;
            else if (event.cbutton.button == g_gamepad.nes_down)   s_controller &= ~sta::nes::Button::DOWN;
            else if (event.cbutton.button == g_gamepad.nes_left)   s_controller &= ~sta::nes::Button::LEFT;
            else if (event.cbutton.button == g_gamepad.nes_right)  s_controller &= ~sta::nes::Button::RIGHT;
            else if (event.cbutton.button == g_gamepad.nes_a)      s_controller &= ~sta::nes::Button::A;
            else if (event.cbutton.button == g_gamepad.nes_b)      s_controller &= ~sta::nes::Button::B;
            else if (event.cbutton.button == g_gamepad.nes_start)  s_controller &= ~sta::nes::Button::START;
            else if (event.cbutton.button == g_gamepad.nes_select)  s_controller &= ~sta::nes::Button::SELECT;
            break;
        }
    }

    state.controller = static_cast<sta::nes::ControllerState>(s_controller);
    return state;
}

void SaveConfig() {
    std::ofstream ofs("bindings.cfg");
    if (!ofs) return;

    ofs << "# Ghost Mirror Keyboard & Gamepad Bindings Configuration\n";
    ofs << "# Keyboard key names use standard SDL scancode names (e.g. A, B, Z, X, Up, Down, F5, F9, F8, F10, Tab, Escape, Return, Space)\n";
    ofs << "# Gamepad button names use standard SDL button names (e.g. a, b, x, y, back, start, leftshoulder, rightshoulder, leftstick, rightstick, dpup, dpdown, dpleft, dpright)\n\n";

    ofs << "[KeyboardNES]\n";
    ofs << "UP = " << SDL_GetScancodeName(g_keys.nes_up) << "\n";
    ofs << "DOWN = " << SDL_GetScancodeName(g_keys.nes_down) << "\n";
    ofs << "LEFT = " << SDL_GetScancodeName(g_keys.nes_left) << "\n";
    ofs << "RIGHT = " << SDL_GetScancodeName(g_keys.nes_right) << "\n";
    ofs << "A = " << SDL_GetScancodeName(g_keys.nes_a) << "\n";
    ofs << "B = " << SDL_GetScancodeName(g_keys.nes_b) << "\n";
    ofs << "START = " << SDL_GetScancodeName(g_keys.nes_start) << "\n";
    ofs << "SELECT = " << SDL_GetScancodeName(g_keys.nes_select) << "\n\n";

    ofs << "[KeyboardHotkeys]\n";
    ofs << "QUICKSAVE = " << SDL_GetScancodeName(g_keys.hk_quicksave) << "\n";
    ofs << "QUICKLOAD = " << SDL_GetScancodeName(g_keys.hk_quickload) << "\n";
    ofs << "SAVE_PB = " << SDL_GetScancodeName(g_keys.hk_save_pb) << "\n";
    ofs << "RESTART_LEVEL = " << SDL_GetScancodeName(g_keys.hk_restart) << "\n";
    ofs << "TOGGLE_VIEW = " << SDL_GetScancodeName(g_keys.hk_view_mode) << "\n";
    ofs << "TOGGLE_MENU = " << SDL_GetScancodeName(g_keys.hk_menu) << "\n\n";

    auto btn_to_str = [](SDL_GameControllerButton btn) -> std::string {
        const char* s = SDL_GameControllerGetStringForButton(btn);
        return s ? s : "invalid";
    };

    ofs << "[GamepadNES]\n";
    ofs << "UP = " << btn_to_str(g_gamepad.nes_up) << "\n";
    ofs << "DOWN = " << btn_to_str(g_gamepad.nes_down) << "\n";
    ofs << "LEFT = " << btn_to_str(g_gamepad.nes_left) << "\n";
    ofs << "RIGHT = " << btn_to_str(g_gamepad.nes_right) << "\n";
    ofs << "A = " << btn_to_str(g_gamepad.nes_a) << "\n";
    ofs << "B = " << btn_to_str(g_gamepad.nes_b) << "\n";
    ofs << "START = " << btn_to_str(g_gamepad.nes_start) << "\n";
    ofs << "SELECT = " << btn_to_str(g_gamepad.nes_select) << "\n\n";

    ofs << "[GamepadHotkeys]\n";
    ofs << "QUICKSAVE = " << btn_to_str(g_gamepad.hk_quicksave) << "\n";
    ofs << "QUICKLOAD = " << btn_to_str(g_gamepad.hk_quickload) << "\n";
    ofs << "SAVE_PB = " << btn_to_str(g_gamepad.hk_save_pb) << "\n";
    ofs << "RESTART_LEVEL = " << btn_to_str(g_gamepad.hk_restart) << "\n";
    ofs << "TOGGLE_VIEW = " << btn_to_str(g_gamepad.hk_view_mode) << "\n";
    ofs << "TOGGLE_MENU = " << btn_to_str(g_gamepad.hk_menu) << "\n";

    ofs.close();
    std::cout << "Bindings configuration saved to bindings.cfg" << std::endl;
}

std::string GetActionKeyName(int action_idx) {
    SDL_Scancode code = SDL_SCANCODE_UNKNOWN;
    switch (action_idx) {
        case 0: code = g_keys.nes_up; break;
        case 1: code = g_keys.nes_down; break;
        case 2: code = g_keys.nes_left; break;
        case 3: code = g_keys.nes_right; break;
        case 4: code = g_keys.nes_a; break;
        case 5: code = g_keys.nes_b; break;
        case 6: code = g_keys.nes_start; break;
        case 7: code = g_keys.nes_select; break;
        case 8: code = g_keys.hk_quicksave; break;
        case 9: code = g_keys.hk_quickload; break;
        case 10: code = g_keys.hk_save_pb; break;
        case 11: code = g_keys.hk_restart; break;
        case 12: code = g_keys.hk_view_mode; break;
        case 13: code = g_keys.hk_menu; break;
        default: break;
    }
    const char* name = SDL_GetScancodeName(code);
    return name ? name : "UNKNOWN";
}

std::string GetActionButtonName(int action_idx) {
    SDL_GameControllerButton btn = SDL_CONTROLLER_BUTTON_INVALID;
    switch (action_idx) {
        case 0: btn = g_gamepad.nes_up; break;
        case 1: btn = g_gamepad.nes_down; break;
        case 2: btn = g_gamepad.nes_left; break;
        case 3: btn = g_gamepad.nes_right; break;
        case 4: btn = g_gamepad.nes_a; break;
        case 5: btn = g_gamepad.nes_b; break;
        case 6: btn = g_gamepad.nes_start; break;
        case 7: btn = g_gamepad.nes_select; break;
        case 8: btn = g_gamepad.hk_quicksave; break;
        case 9: btn = g_gamepad.hk_quickload; break;
        case 10: btn = g_gamepad.hk_save_pb; break;
        case 11: btn = g_gamepad.hk_restart; break;
        case 12: btn = g_gamepad.hk_view_mode; break;
        case 13: btn = g_gamepad.hk_menu; break;
        default: break;
    }
    const char* name = SDL_GameControllerGetStringForButton(btn);
    return name ? name : "NONE";
}

void RebindKeyboard(int action_idx, SDL_Scancode code) {
    switch (action_idx) {
        case 0: g_keys.nes_up = code; break;
        case 1: g_keys.nes_down = code; break;
        case 2: g_keys.nes_left = code; break;
        case 3: g_keys.nes_right = code; break;
        case 4: g_keys.nes_a = code; break;
        case 5: g_keys.nes_b = code; break;
        case 6: g_keys.nes_start = code; break;
        case 7: g_keys.nes_select = code; break;
        case 8: g_keys.hk_quicksave = code; break;
        case 9: g_keys.hk_quickload = code; break;
        case 10: g_keys.hk_save_pb = code; break;
        case 11: g_keys.hk_restart = code; break;
        case 12: g_keys.hk_view_mode = code; break;
        case 13: g_keys.hk_menu = code; break;
        default: break;
    }
}

void RebindGamepad(int action_idx, SDL_GameControllerButton btn) {
    switch (action_idx) {
        case 0: g_gamepad.nes_up = btn; break;
        case 1: g_gamepad.nes_down = btn; break;
        case 2: g_gamepad.nes_left = btn; break;
        case 3: g_gamepad.nes_right = btn; break;
        case 4: g_gamepad.nes_a = btn; break;
        case 5: g_gamepad.nes_b = btn; break;
        case 6: g_gamepad.nes_start = btn; break;
        case 7: g_gamepad.nes_select = btn; break;
        case 8: g_gamepad.hk_quicksave = btn; break;
        case 9: g_gamepad.hk_quickload = btn; break;
        case 10: g_gamepad.hk_save_pb = btn; break;
        case 11: g_gamepad.hk_restart = btn; break;
        case 12: g_gamepad.hk_view_mode = btn; break;
        case 13: g_gamepad.hk_menu = btn; break;
        default: break;
    }
}

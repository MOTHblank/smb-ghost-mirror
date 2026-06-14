#ifndef GHOST_MIRROR_RENDERER_H
#define GHOST_MIRROR_RENDERER_H

#include <SDL.h>
#include <string>
#include "nes/nes.h"
#include "font8x8.h"

// Renders three NES screens side-by-side in an SDL2 window with a HUD bar.
struct Renderer {
    SDL_Window*   window;
    SDL_Renderer* sdl_renderer;
    SDL_Texture*  player_tex;
    SDL_Texture*  pb_tex;
    SDL_Texture*  tas_tex;

    int scale;       // integer scale factor (2 or 3)
    int screen_w;    // scaled NES width  (256 * scale)
    int screen_h;    // scaled NES height (240 * scale)
    int padding;     // gap between screens
    int hud_height;  // bottom bar for HUD text
    int window_w;    // total window width
    int window_h;    // total window height

    // RGBA pixel buffer for palette conversion
    uint32_t rgba_buf[256 * 240];

    // NES palette LUT (64 entries, ARGB format for SDL)
    uint32_t palette_lut[64];

    Renderer();
    ~Renderer();

    bool Init(int scale = 2);
    void Shutdown();

    // Convert a palette-indexed NES frame to an SDL texture
    void UploadFrame(SDL_Texture* tex, const sta::nes::Frame& frame);

    // Toggle overlay view mode (resizes window)
    void SetOverlayMode(bool enabled);

    // Draw screens + HUD (supports side-by-side and ghost overlays)
    void DrawFrame(
        const sta::nes::Frame& player_frame,
        const sta::nes::Frame& pb_frame,
        const sta::nes::Frame& tas_frame,
        int frame_count,
        int pb_total_frames,
        int tas_total_frames,
        const char* level_id,
        bool menu_open,
        int pb_delta,
        bool has_pb,
        int tas_delta,
        bool has_tas,
        bool overlay_mode,
        const sta::nes::INESEmulator* player_emu = nullptr,
        const sta::nes::INESEmulator* pb_emu = nullptr,
        const sta::nes::INESEmulator* tas_emu = nullptr,
        int max_level_x = 0
    );

    // Private helper to draw emulator OAM sprites
    void DrawEmulatorSprites(
        uint32_t* rgba_buf,
        const sta::nes::INESEmulator& emu,
        SDL_Color tint_color,
        float opacity,
        int scroll_delta = 0
    );

    // Draw the level selection overlay
    void DrawMenu(int selected_index, const char* level_names[], int num_levels,
                  const bool has_pb[], const bool has_tas[], const int best_frames[], const int tas_frames[],
                  const char* current_rom, const char* current_fm2);

    // Draw the bindings configuration overlay
    void DrawBindingsMenu(int selected_row, bool in_rebinding);

    // Helper to draw text using standard 8x8 font
    void DrawText(const std::string& text, int x, int y, int scale, SDL_Color color);
};

#endif // GHOST_MIRROR_RENDERER_H

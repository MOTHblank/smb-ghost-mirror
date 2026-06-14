#include "renderer.h"
#include "input.h"
#include <cstdio>
#include <cstring>

Renderer::Renderer()
    : window(nullptr)
    , sdl_renderer(nullptr)
    , player_tex(nullptr)
    , pb_tex(nullptr)
    , tas_tex(nullptr)
    , scale(2)
    , padding(8)
    , hud_height(40)
{
    memset(rgba_buf, 0, sizeof(rgba_buf));

    // Build palette LUT from Static's DefaultPaletteRGB
    const sta::nes::Palette& pal = sta::nes::DefaultPaletteRGB();
    for (int i = 0; i < 64; i++) {
        uint8_t r = pal[i * 3 + 0];
        uint8_t g = pal[i * 3 + 1];
        uint8_t b = pal[i * 3 + 2];
        // ARGB format for SDL_PIXELFORMAT_ARGB8888
        palette_lut[i] = (0xFF << 24) | (r << 16) | (g << 8) | b;
    }
}

Renderer::~Renderer() {
    Shutdown();
}

bool Renderer::Init(int s) {
    scale = s;
    screen_w = 256 * scale;
    screen_h = 240 * scale;
    window_w = screen_w * 3 + padding * 4;
    window_h = screen_h + padding * 2 + hud_height;

    window = SDL_CreateWindow(
        "Ghost Mirror — SMB Ghost Racing",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        window_w, window_h,
        SDL_WINDOW_SHOWN
    );
    if (!window) {
        fprintf(stderr, "ERROR: SDL_CreateWindow: %s\n", SDL_GetError());
        return false;
    }

    sdl_renderer = SDL_CreateRenderer(window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!sdl_renderer) {
        fprintf(stderr, "ERROR: SDL_CreateRenderer: %s\n", SDL_GetError());
        return false;
    }

    // Create streaming textures for each NES screen
    player_tex = SDL_CreateTexture(sdl_renderer,
        SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 256, 240);
    pb_tex = SDL_CreateTexture(sdl_renderer,
        SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 256, 240);
    tas_tex = SDL_CreateTexture(sdl_renderer,
        SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 256, 240);

    if (!player_tex || !pb_tex || !tas_tex) {
        fprintf(stderr, "ERROR: SDL_CreateTexture: %s\n", SDL_GetError());
        return false;
    }

    printf("Renderer initialized: %dx%d, scale=%d\n", window_w, window_h, scale);
    return true;
}

void Renderer::Shutdown() {
    if (player_tex) { SDL_DestroyTexture(player_tex); player_tex = nullptr; }
    if (pb_tex)     { SDL_DestroyTexture(pb_tex);     pb_tex = nullptr; }
    if (tas_tex)    { SDL_DestroyTexture(tas_tex);     tas_tex = nullptr; }
    if (sdl_renderer) { SDL_DestroyRenderer(sdl_renderer); sdl_renderer = nullptr; }
    if (window)     { SDL_DestroyWindow(window);     window = nullptr; }
}

void Renderer::UploadFrame(SDL_Texture* tex, const sta::nes::Frame& frame) {
    for (int i = 0; i < 256 * 240; i++) {
        uint8_t idx = frame[i] & 0x3F; // mask to 6-bit palette index
        rgba_buf[i] = palette_lut[idx];
    }
    SDL_UpdateTexture(tex, nullptr, rgba_buf, 256 * sizeof(uint32_t));
}

void Renderer::SetOverlayMode(bool enabled) {
    if (enabled) {
        window_w = screen_w + padding * 2;
    } else {
        window_w = screen_w * 3 + padding * 4;
    }
    SDL_SetWindowSize(window, window_w, window_h);
}

void Renderer::DrawEmulatorSprites(
    uint32_t* rgba_buf_val,
    const sta::nes::INESEmulator& emu,
    SDL_Color tint_color,
    float opacity,
    int scroll_delta
) {
    // 1. Get the PPU frame palette (colors 16 to 31 are sprite palettes)
    uint8_t fpal[32];
    for (int p = 0; p < 32; p++) {
        fpal[p] = emu.PPUPeek8(0x3F00 + p) & 0x3F;
    }

    // 2. Iterate OAM entries 1 to 63 (skip sprite 0 used for hit-detection)
    for (int i = 1; i < 64; i++) {
        uint8_t y_val = emu.OAMPeek8(i * 4 + 0);
        uint8_t tile_index = emu.OAMPeek8(i * 4 + 1);
        uint8_t attributes = emu.OAMPeek8(i * 4 + 2);
        uint8_t x_val = emu.OAMPeek8(i * 4 + 3);

        // A Y coordinate >= 240 is off-screen
        if (y_val >= 240) continue;

        int start_y = y_val; // Y coordinate on screen
        int start_x = static_cast<int>(x_val) + scroll_delta; // World-relative X coordinate on screen

        bool flip_h = (attributes & 0x40) != 0;
        bool flip_v = (attributes & 0x80) != 0;
        uint8_t palette_idx = attributes & 0x03;

        // Check if this tile belongs to Mario's sprite set
        bool is_mario = (tile_index <= 0x4F) ||
                        (tile_index >= 0x58 && tile_index <= 0x5A) ||
                        (tile_index >= 0x5C && tile_index <= 0x5F) ||
                        (tile_index >= 0x90 && tile_index <= 0x93) ||
                        (tile_index == 0x9E) || (tile_index == 0x9F);

        float current_opacity = is_mario ? opacity : (opacity * 0.3f);

        // Fetch 16 bytes of tile data from Pattern Table 0 (0x0000)
        uint16_t tile_addr = tile_index * 16;
        uint8_t tile_bytes[16];
        for (int b = 0; b < 16; b++) {
            tile_bytes[b] = emu.PPUPeek8(tile_addr + b);
        }

        // Draw 8x8 pixels
        for (int r = 0; r < 8; r++) {
            int py = start_y + r;
            if (py < 0 || py >= 240) continue;

            uint8_t low = tile_bytes[flip_v ? (7 - r) : r];
            uint8_t high = tile_bytes[8 + (flip_v ? (7 - r) : r)];

            for (int c = 0; c < 8; c++) {
                int px = start_x + c;
                if (px < 0 || px >= 256) continue;

                int bit_pos = flip_h ? c : (7 - c);
                uint8_t bit0 = (low >> bit_pos) & 1;
                uint8_t bit1 = (high >> bit_pos) & 1;
                uint8_t color_idx = (bit1 << 1) | bit0;

                // Color index 0 is transparent
                if (color_idx == 0) continue;

                // Fetch actual color index from the sprite palette
                uint8_t col_val = fpal[16 + palette_idx * 4 + color_idx];
                uint32_t ghost_color = palette_lut[col_val];

                // Blend with background pixel in rgba_buf_val
                uint32_t player_color = rgba_buf_val[py * 256 + px];

                uint8_t pr = (player_color >> 16) & 0xFF;
                uint8_t pg = (player_color >> 8) & 0xFF;
                uint8_t pb_b = player_color & 0xFF;

                uint8_t gr = (ghost_color >> 16) & 0xFF;
                uint8_t gg = (ghost_color >> 8) & 0xFF;
                uint8_t gb = ghost_color & 0xFF;

                // Apply custom tint
                gr = static_cast<uint8_t>((gr + tint_color.r) / 2);
                gg = static_cast<uint8_t>((gg + tint_color.g) / 2);
                gb = static_cast<uint8_t>((gb + tint_color.b) / 2);

                // Blend using current opacity
                uint8_t r_blended = static_cast<uint8_t>((pr * (1.0f - current_opacity)) + (gr * current_opacity));
                uint8_t g_blended = static_cast<uint8_t>((pg * (1.0f - current_opacity)) + (gg * current_opacity));
                uint8_t b_blended = static_cast<uint8_t>((pb_b * (1.0f - current_opacity)) + (gb * current_opacity));

                rgba_buf_val[py * 256 + px] = (0xFF << 24) | (r_blended << 16) | (g_blended << 8) | b_blended;
            }
        }
    }
}

void Renderer::DrawFrame(
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
    const sta::nes::INESEmulator* player_emu,
    const sta::nes::INESEmulator* pb_emu,
    const sta::nes::INESEmulator* tas_emu,
    int max_level_x)
{
    // Compute scroll difference for world-relative positioning in overlay mode
    int player_scroll = 0;
    if (player_emu) {
        player_scroll = static_cast<int>(player_emu->CPUPeek(0x071A)) * 256 + static_cast<int>(player_emu->CPUPeek(0x071C));
    }

    int pb_scroll_delta = 0;
    if (has_pb && pb_emu) {
        int pb_scroll = static_cast<int>(pb_emu->CPUPeek(0x071A)) * 256 + static_cast<int>(pb_emu->CPUPeek(0x071C));
        pb_scroll_delta = pb_scroll - player_scroll;
    }

    int tas_scroll_delta = 0;
    if (has_tas && tas_emu) {
        int tas_scroll = static_cast<int>(tas_emu->CPUPeek(0x071A)) * 256 + static_cast<int>(tas_emu->CPUPeek(0x071C));
        tas_scroll_delta = tas_scroll - player_scroll;
    }

    if (overlay_mode) {
        // Build rgba_buf with player's frame
        for (int i = 0; i < 256 * 240; i++) {
            uint8_t idx = player_frame[i] & 0x3F;
            rgba_buf[i] = palette_lut[idx];
        }

        // Draw PB Sprites overlay (tinted blue, 50% opacity, world-relative offset)
        if (has_pb && pb_emu) {
            DrawEmulatorSprites(rgba_buf, *pb_emu, { 50, 100, 255, 255 }, 0.5f, pb_scroll_delta);
        }

        // Draw TAS Sprites overlay (tinted gold, 50% opacity, world-relative offset)
        if (has_tas && tas_emu) {
            DrawEmulatorSprites(rgba_buf, *tas_emu, { 255, 180, 50, 255 }, 0.5f, tas_scroll_delta);
        }

        // Upload combined frame to player_tex
        SDL_UpdateTexture(player_tex, nullptr, rgba_buf, 256 * sizeof(uint32_t));

        // Clear background
        SDL_SetRenderDrawColor(sdl_renderer, 16, 16, 24, 255);
        SDL_RenderClear(sdl_renderer);

        // Draw single centered viewport
        SDL_Rect rect = { padding, padding, screen_w, screen_h };
        SDL_RenderCopy(sdl_renderer, player_tex, nullptr, &rect);

        // Border (green)
        SDL_SetRenderDrawColor(sdl_renderer, 80, 200, 120, 255);
        SDL_RenderDrawRect(sdl_renderer, &rect);

        // HUD background
        SDL_Rect hud_rect = { 0, screen_h + padding * 2, window_w, hud_height };
        SDL_SetRenderDrawColor(sdl_renderer, 24, 24, 32, 255);
        SDL_RenderFillRect(sdl_renderer, &hud_rect);

        // Draw level progress bar/map
        if (max_level_x > 0) {
            int bar_x = padding + 8;
            int bar_y = screen_h + padding * 2 + 4;
            int bar_w = window_w - 2 * bar_x;

            // Draw bar track (dark gray)
            SDL_SetRenderDrawColor(sdl_renderer, 60, 60, 70, 255);
            SDL_Rect track_rect = { bar_x, bar_y, bar_w, 4 };
            SDL_RenderFillRect(sdl_renderer, &track_rect);

            auto draw_marker = [&](int world_x, SDL_Color color) {
                float progress = static_cast<float>(world_x) / static_cast<float>(max_level_x);
                if (progress < 0.0f) progress = 0.0f;
                if (progress > 1.0f) progress = 1.0f;

                int dot_x = bar_x + static_cast<int>(progress * bar_w);
                SDL_Rect dot_rect = { dot_x - 3, bar_y - 2, 6, 8 };
                SDL_SetRenderDrawColor(sdl_renderer, color.r, color.g, color.b, color.a);
                SDL_RenderFillRect(sdl_renderer, &dot_rect);
            };

            // Draw markers: TAS (gold), PB (blue), Player (white)
            if (has_tas && tas_emu) {
                int tas_x = static_cast<int>(tas_emu->CPUPeek(0x071A)) * 256 + static_cast<int>(tas_emu->CPUPeek(0x071C)) + static_cast<int>(tas_emu->CPUPeek(0x0086));
                draw_marker(tas_x, { 255, 180, 50, 255 });
            }
            if (has_pb && pb_emu) {
                int pb_x = static_cast<int>(pb_emu->CPUPeek(0x071A)) * 256 + static_cast<int>(pb_emu->CPUPeek(0x071C)) + static_cast<int>(pb_emu->CPUPeek(0x0086));
                draw_marker(pb_x, { 100, 140, 255, 255 });
            }
            if (player_emu) {
                int player_x = static_cast<int>(player_emu->CPUPeek(0x071A)) * 256 + static_cast<int>(player_emu->CPUPeek(0x071C)) + static_cast<int>(player_emu->CPUPeek(0x0086));
                draw_marker(player_x, { 255, 255, 255, 255 });
            }
        }

        // HUD text (compact scale 2)
        char hud_str[128];
        char pb_str[32] = "PB:--";
        char tas_str[32] = "TAS:--";

        if (has_pb) {
            if (pb_delta > 0) snprintf(pb_str, sizeof(pb_str), "PB:+%dF", pb_delta);
            else if (pb_delta < 0) snprintf(pb_str, sizeof(pb_str), "PB:-%dF", -pb_delta);
            else snprintf(pb_str, sizeof(pb_str), "PB:+0F");
        }
        if (has_tas) {
            if (tas_delta > 0) snprintf(tas_str, sizeof(tas_str), "TAS:+%dF", tas_delta);
            else if (tas_delta < 0) snprintf(tas_str, sizeof(tas_str), "TAS:-%dF", -tas_delta);
            else snprintf(tas_str, sizeof(tas_str), "TAS:+0F");
        }

        snprintf(hud_str, sizeof(hud_str), "%s | F:%-4d | %s | %s",
                 level_id ? level_id : "---", frame_count, pb_str, tas_str);

        SDL_Color white = { 255, 255, 255, 255 };
        DrawText(hud_str, padding + 8, screen_h + padding * 2 + 12, 2, white);
    }
    else {
        // Upload frames to textures
        UploadFrame(player_tex, player_frame);
        UploadFrame(pb_tex, pb_frame);
        UploadFrame(tas_tex, tas_frame);

        // Clear background
        SDL_SetRenderDrawColor(sdl_renderer, 16, 16, 24, 255);
        SDL_RenderClear(sdl_renderer);

        // Draw three viewports side-by-side
        SDL_Rect rects[3];
        for (int i = 0; i < 3; i++) {
            rects[i].x = padding + i * (screen_w + padding);
            rects[i].y = padding;
            rects[i].w = screen_w;
            rects[i].h = screen_h;
        }

        SDL_RenderCopy(sdl_renderer, player_tex, nullptr, &rects[0]);
        SDL_RenderCopy(sdl_renderer, pb_tex,     nullptr, &rects[1]);
        SDL_RenderCopy(sdl_renderer, tas_tex,    nullptr, &rects[2]);

        // Draw viewport borders
        SDL_SetRenderDrawColor(sdl_renderer, 80, 200, 120, 255); // green for player
        SDL_RenderDrawRect(sdl_renderer, &rects[0]);
        SDL_SetRenderDrawColor(sdl_renderer, 100, 140, 255, 255); // blue for PB
        SDL_RenderDrawRect(sdl_renderer, &rects[1]);
        SDL_SetRenderDrawColor(sdl_renderer, 255, 180, 60, 255);  // orange for TAS
        SDL_RenderDrawRect(sdl_renderer, &rects[2]);

        // Draw HUD bar background
        SDL_Rect hud_rect = { 0, screen_h + padding * 2, window_w, hud_height };
        SDL_SetRenderDrawColor(sdl_renderer, 24, 24, 32, 255);
        SDL_RenderFillRect(sdl_renderer, &hud_rect);

        // Draw level progress bar/map
        if (max_level_x > 0) {
            int bar_x = padding + 8;
            int bar_y = screen_h + padding * 2 + 4;
            int bar_w = window_w - 2 * bar_x;

            // Draw bar track (dark gray)
            SDL_SetRenderDrawColor(sdl_renderer, 60, 60, 70, 255);
            SDL_Rect track_rect = { bar_x, bar_y, bar_w, 4 };
            SDL_RenderFillRect(sdl_renderer, &track_rect);

            auto draw_marker = [&](int world_x, SDL_Color color) {
                float progress = static_cast<float>(world_x) / static_cast<float>(max_level_x);
                if (progress < 0.0f) progress = 0.0f;
                if (progress > 1.0f) progress = 1.0f;

                int dot_x = bar_x + static_cast<int>(progress * bar_w);
                SDL_Rect dot_rect = { dot_x - 3, bar_y - 2, 6, 8 };
                SDL_SetRenderDrawColor(sdl_renderer, color.r, color.g, color.b, color.a);
                SDL_RenderFillRect(sdl_renderer, &dot_rect);
            };

            // Draw markers: TAS (gold), PB (blue), Player (white)
            if (has_tas && tas_emu) {
                int tas_x = static_cast<int>(tas_emu->CPUPeek(0x071A)) * 256 + static_cast<int>(tas_emu->CPUPeek(0x071C)) + static_cast<int>(tas_emu->CPUPeek(0x0086));
                draw_marker(tas_x, { 255, 180, 50, 255 });
            }
            if (has_pb && pb_emu) {
                int pb_x = static_cast<int>(pb_emu->CPUPeek(0x071A)) * 256 + static_cast<int>(pb_emu->CPUPeek(0x071C)) + static_cast<int>(pb_emu->CPUPeek(0x0086));
                draw_marker(pb_x, { 100, 140, 255, 255 });
            }
            if (player_emu) {
                int player_x = static_cast<int>(player_emu->CPUPeek(0x071A)) * 256 + static_cast<int>(player_emu->CPUPeek(0x071C)) + static_cast<int>(player_emu->CPUPeek(0x0086));
                draw_marker(player_x, { 255, 255, 255, 255 });
            }
        }

        // Draw HUD text
        SDL_Color white = { 255, 255, 255, 255 };

        // Player HUD info
        char player_str[64];
        snprintf(player_str, sizeof(player_str), "%s | FRAME: %d",
                 level_id ? level_id : "---", frame_count);
        DrawText(player_str, padding + 12, screen_h + padding * 2 + 12, 2, white);

        // PB HUD info
        char pb_str[64];
        SDL_Color pb_color;
        if (has_pb) {
            if (pb_delta > 0) {
                snprintf(pb_str, sizeof(pb_str), "PB: +%dF", pb_delta);
                pb_color = { 80, 200, 120, 255 }; // green (ahead)
            } else if (pb_delta < 0) {
                snprintf(pb_str, sizeof(pb_str), "PB: -%dF", -pb_delta);
                pb_color = { 255, 100, 100, 255 }; // red (behind)
            } else {
                snprintf(pb_str, sizeof(pb_str), "PB: +0F");
                pb_color = { 255, 255, 255, 255 }; // white
            }
        } else {
            snprintf(pb_str, sizeof(pb_str), "PB: NONE");
            pb_color = { 150, 150, 150, 255 }; // gray
        }
        DrawText(pb_str, padding + screen_w + padding + 12, screen_h + padding * 2 + 12, 2, pb_color);

        // TAS HUD info
        char tas_str[64];
        SDL_Color tas_color;
        if (has_tas) {
            if (tas_delta > 0) {
                snprintf(tas_str, sizeof(tas_str), "TAS: +%dF", tas_delta);
                tas_color = { 80, 200, 120, 255 }; // green (ahead)
            } else if (tas_delta < 0) {
                snprintf(tas_str, sizeof(tas_str), "TAS: -%dF", -tas_delta);
                tas_color = { 255, 180, 60, 255 }; // orange (behind)
            } else {
                snprintf(tas_str, sizeof(tas_str), "TAS: +0F");
                tas_color = { 255, 255, 255, 255 }; // white
            }
        } else {
            snprintf(tas_str, sizeof(tas_str), "TAS: NONE");
            tas_color = { 150, 150, 150, 255 }; // gray
        }
        DrawText(tas_str, padding + 2 * (screen_w + padding) + 12, screen_h + padding * 2 + 12, 2, tas_color);
    }

    // Update window title
    char title[256];
    snprintf(title, sizeof(title),
        "Ghost Mirror | %s | Frame: %d | PB: %d frames | TAS: %d frames",
        level_id ? level_id : "---",
        frame_count,
        pb_total_frames,
        tas_total_frames);
    SDL_SetWindowTitle(window, title);

    // Menu overlay
    if (menu_open) {
        SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
        SDL_Rect overlay = { 0, 0, window_w, window_h };
        SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 180);
        SDL_RenderFillRect(sdl_renderer, &overlay);
        SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_NONE);
    }

    SDL_RenderPresent(sdl_renderer);
}

void Renderer::DrawMenu(int selected_index, const char* level_names[], int num_levels,
                        const bool has_pb[], const bool has_tas[], const int best_frames[], const int tas_frames[],
                        const char* current_rom, const char* current_fm2, bool reset_player_on_transition)
{
    // Semi-transparent overlay
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
    SDL_Rect overlay = { 0, 0, window_w, window_h };
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 200);
    SDL_RenderFillRect(sdl_renderer, &overlay);

    // Dynamic grid parameters based on window width and scale
    int box_w = (scale == 1) ? 55 : 120;
    int box_h = (scale == 1) ? 45 : 60;
    int text_scale = (scale == 1) ? 1 : 2;
    int title_scale = (scale == 1) ? 2 : 3;

    int cols = 8;
    int rows = 1;
    if (window_w < 600) { // Overlay mode
        cols = 4;
        rows = 2;
    }

    int total_w = cols * (box_w + padding) - padding;
    int total_h = rows * (box_h + padding) - padding;
    int start_x = (window_w - total_w) / 2;
    int start_y = (window_h - total_h) / 2;

    // Draw SELECT LEVEL title
    SDL_Color title_color = { 255, 220, 100, 255 }; // gold
    std::string title_text = "SELECT LEVEL";
    int title_w = static_cast<int>(title_text.length()) * 8 * title_scale;
    DrawText(title_text, (window_w - title_w) / 2, start_y - (scale == 1 ? 30 : 60), title_scale, title_color);

    // Calculate Sum of Best (SOB) and Sum of TAS (SOT)
    int sum_pb = 0;
    int sum_tas = 0;
    bool any_pb = false;
    bool any_tas = false;
    for (int i = 0; i < num_levels; i++) {
        if (best_frames && best_frames[i] >= 0) {
            sum_pb += best_frames[i];
            any_pb = true;
        }
        if (tas_frames && tas_frames[i] >= 0) {
            sum_tas += tas_frames[i];
            any_tas = true;
        }
    }

    for (int i = 0; i < num_levels; i++) {
        int r = i / cols;
        int c = i % cols;
        SDL_Rect box = {
            start_x + c * (box_w + padding),
            start_y + r * (box_h + padding),
            box_w,
            box_h
        };

        SDL_Color text_color;
        SDL_Color record_color;
        if (i == selected_index) {
            SDL_SetRenderDrawColor(sdl_renderer, 80, 200, 120, 255); // green background
            SDL_RenderFillRect(sdl_renderer, &box);
            SDL_SetRenderDrawColor(sdl_renderer, 255, 255, 255, 255);
            text_color = { 16, 16, 24, 255 }; // dark text
            record_color = { 16, 64, 120, 255 }; // dark blue text
        } else {
            SDL_SetRenderDrawColor(sdl_renderer, 40, 40, 60, 255); // dark gray background
            SDL_RenderFillRect(sdl_renderer, &box);
            SDL_SetRenderDrawColor(sdl_renderer, 100, 100, 130, 255);
            text_color = { 220, 220, 240, 255 }; // light text
            record_color = { 100, 140, 255, 255 }; // light blue text
        }
        SDL_RenderDrawRect(sdl_renderer, &box);

        // Draw level name
        std::string lvl_name = level_names[i];
        int lvl_w = static_cast<int>(lvl_name.length()) * 8 * text_scale;
        int lvl_y_offset = (scale == 1) ? 4 : 10;
        DrawText(lvl_name, box.x + (box_w - lvl_w) / 2, box.y + lvl_y_offset, text_scale, text_color);

        // Draw PB & TAS statistics and difference
        if (scale >= 2) {
            // Row 2: PB
            char pb_buf[32];
            SDL_Color pb_col = (i == selected_index) ? SDL_Color{ 40, 40, 40, 255 } : SDL_Color{ 180, 200, 255, 255 };
            if (best_frames && best_frames[i] >= 0) {
                snprintf(pb_buf, sizeof(pb_buf), "PB:%dF", best_frames[i]);
            } else {
                snprintf(pb_buf, sizeof(pb_buf), "PB:--F");
                pb_col = (i == selected_index) ? SDL_Color{ 80, 80, 80, 255 } : SDL_Color{ 120, 120, 140, 255 };
            }
            int pb_w = static_cast<int>(strlen(pb_buf)) * 8;
            DrawText(pb_buf, box.x + (box_w - pb_w) / 2, box.y + 24, 1, pb_col);

            // Row 3: TAS
            char tas_buf[32];
            SDL_Color tas_col = (i == selected_index) ? SDL_Color{ 100, 60, 16, 255 } : SDL_Color{ 255, 200, 100, 255 };
            if (tas_frames && tas_frames[i] >= 0) {
                snprintf(tas_buf, sizeof(tas_buf), "TAS:%dF", tas_frames[i]);
            } else {
                snprintf(tas_buf, sizeof(tas_buf), "TAS:--F");
                tas_col = (i == selected_index) ? SDL_Color{ 80, 80, 80, 255 } : SDL_Color{ 120, 120, 140, 255 };
            }
            int tas_w = static_cast<int>(strlen(tas_buf)) * 8;
            DrawText(tas_buf, box.x + (box_w - tas_w) / 2, box.y + 34, 1, tas_col);

            // Row 4: Diff
            if (best_frames && best_frames[i] >= 0 && tas_frames && tas_frames[i] >= 0) {
                int diff = best_frames[i] - tas_frames[i];
                char diff_buf[32];
                SDL_Color diff_col = { 255, 255, 255, 255 };
                if (diff > 0) {
                    snprintf(diff_buf, sizeof(diff_buf), "Diff:+%dF", diff);
                    diff_col = (i == selected_index) ? SDL_Color{ 180, 30, 30, 255 } : SDL_Color{ 255, 100, 100, 255 }; // red
                } else if (diff < 0) {
                    snprintf(diff_buf, sizeof(diff_buf), "Diff:-%dF", -diff);
                    diff_col = (i == selected_index) ? SDL_Color{ 16, 100, 16, 255 } : SDL_Color{ 80, 220, 100, 255 }; // green
                } else {
                    snprintf(diff_buf, sizeof(diff_buf), "Diff:+0F");
                    diff_col = (i == selected_index) ? SDL_Color{ 40, 40, 40, 255 } : SDL_Color{ 255, 255, 255, 255 }; // white
                }
                int diff_w = static_cast<int>(strlen(diff_buf)) * 8;
                DrawText(diff_buf, box.x + (box_w - diff_w) / 2, box.y + 44, 1, diff_col);
            } else {
                char diff_buf[32] = "Diff:--";
                SDL_Color diff_col = (i == selected_index) ? SDL_Color{ 80, 80, 80, 255 } : SDL_Color{ 120, 120, 140, 255 };
                int diff_w = static_cast<int>(strlen(diff_buf)) * 8;
                DrawText(diff_buf, box.x + (box_w - diff_w) / 2, box.y + 44, 1, diff_col);
            }
        } else {
            // Scale 1 compact rendering
            // Row 2: PB
            char pb_buf[32];
            SDL_Color pb_col = (i == selected_index) ? SDL_Color{ 40, 40, 40, 255 } : SDL_Color{ 180, 200, 255, 255 };
            if (best_frames && best_frames[i] >= 0) snprintf(pb_buf, sizeof(pb_buf), "P:%d", best_frames[i]);
            else {
                snprintf(pb_buf, sizeof(pb_buf), "P:--");
                pb_col = (i == selected_index) ? SDL_Color{ 80, 80, 80, 255 } : SDL_Color{ 120, 120, 140, 255 };
            }
            int pb_w = static_cast<int>(strlen(pb_buf)) * 8;
            DrawText(pb_buf, box.x + (box_w - pb_w) / 2, box.y + 14, 1, pb_col);

            // Row 3: TAS
            char tas_buf[32];
            SDL_Color tas_col = (i == selected_index) ? SDL_Color{ 100, 60, 16, 255 } : SDL_Color{ 255, 200, 100, 255 };
            if (tas_frames && tas_frames[i] >= 0) snprintf(tas_buf, sizeof(tas_buf), "T:%d", tas_frames[i]);
            else {
                snprintf(tas_buf, sizeof(tas_buf), "T:--");
                tas_col = (i == selected_index) ? SDL_Color{ 80, 80, 80, 255 } : SDL_Color{ 120, 120, 140, 255 };
            }
            int tas_w = static_cast<int>(strlen(tas_buf)) * 8;
            DrawText(tas_buf, box.x + (box_w - tas_w) / 2, box.y + 23, 1, tas_col);

            // Row 4: Diff
            if (best_frames && best_frames[i] >= 0 && tas_frames && tas_frames[i] >= 0) {
                int diff = best_frames[i] - tas_frames[i];
                char diff_buf[32];
                SDL_Color diff_col = { 255, 255, 255, 255 };
                if (diff > 0) {
                    snprintf(diff_buf, sizeof(diff_buf), "+%d", diff);
                    diff_col = (i == selected_index) ? SDL_Color{ 180, 30, 30, 255 } : SDL_Color{ 255, 100, 100, 255 }; // red
                } else if (diff < 0) {
                    snprintf(diff_buf, sizeof(diff_buf), "-%d", -diff);
                    diff_col = (i == selected_index) ? SDL_Color{ 16, 100, 16, 255 } : SDL_Color{ 80, 220, 100, 255 }; // green
                } else {
                    snprintf(diff_buf, sizeof(diff_buf), "+0");
                    diff_col = (i == selected_index) ? SDL_Color{ 40, 40, 40, 255 } : SDL_Color{ 255, 255, 255, 255 }; // white
                }
                int diff_w = static_cast<int>(strlen(diff_buf)) * 8;
                DrawText(diff_buf, box.x + (box_w - diff_w) / 2, box.y + 32, 1, diff_col);
            } else {
                char diff_buf[32] = "--";
                SDL_Color diff_col = (i == selected_index) ? SDL_Color{ 80, 80, 80, 255 } : SDL_Color{ 120, 120, 140, 255 };
                int diff_w = static_cast<int>(strlen(diff_buf)) * 8;
                DrawText(diff_buf, box.x + (box_w - diff_w) / 2, box.y + 32, 1, diff_col);
            }
        }
    }

    // Y position calculations for bottom menu elements
    int sob_y = start_y + total_h + (scale == 1 ? 12 : 25);
    int hint_y = start_y + total_h + (scale == 1 ? 22 : 45);
    int hotkey_y = start_y + total_h + (scale == 1 ? 32 : 60);
    int reset_player_y = start_y + total_h + (scale == 1 ? 42 : 75);
    int files_y1 = start_y + total_h + (scale == 1 ? 52 : 92);
    int files_y2 = start_y + total_h + (scale == 1 ? 62 : 107);
    int files_y3 = start_y + total_h + (scale == 1 ? 72 : 122);

    // Draw Sum of Best (SOB) vs Sum of TAS (SOT)
    char sob_str[256];
    SDL_Color sob_color = { 255, 220, 100, 255 }; // gold
    if (any_pb && any_tas) {
        int diff = sum_pb - sum_tas;
        if (window_w < 500) {
            snprintf(sob_str, sizeof(sob_str), "SOB:%dF | TAS:%dF | D:+%dF", sum_pb, sum_tas, diff);
        } else {
            snprintf(sob_str, sizeof(sob_str), "SUM OF BEST: %dF | SUM OF TAS: %dF | DIFF: +%dF", sum_pb, sum_tas, diff);
        }
    } else {
        if (window_w < 500) {
            snprintf(sob_str, sizeof(sob_str), "SOB:-- | TAS:-- | D:--");
        } else {
            snprintf(sob_str, sizeof(sob_str), "SUM OF BEST: -- | SUM OF TAS: -- | DIFF: --");
        }
    }
    int sob_w = static_cast<int>(strlen(sob_str)) * 8;
    DrawText(sob_str, (window_w - sob_w) / 2, sob_y, 1, sob_color);

    // Draw control hints at the bottom
    SDL_Color hint_color = { 160, 160, 180, 255 };
    std::string hint_text = "ARROWS: SELECT | SPACE: LOAD | C: BINDINGS | ESC: CLOSE";
    std::string hotkey_hint = "F5: Q-SAVE | F9: Q-LOAD | F8: SAVE PB | F10: RESET | TAB: VIEW";

    int hint_w = static_cast<int>(hint_text.length()) * 8;
    int hotkey_w = static_cast<int>(hotkey_hint.length()) * 8;

    if (window_w < 350) {
        std::string short_hint_text = "ARROWS:SEL | SPACE:LOAD | ESC:CLS";
        int short_hint_w = static_cast<int>(short_hint_text.length()) * 8;
        DrawText(short_hint_text, (window_w - short_hint_w) / 2, hint_y, 1, hint_color);
    } else if (window_w < 500) {
        std::string mid_hint_text = "ARROWS:SEL | SPACE:LOAD | C:BIN | ESC:CLS";
        int mid_hint_w = static_cast<int>(mid_hint_text.length()) * 8;
        DrawText(mid_hint_text, (window_w - mid_hint_w) / 2, hint_y, 1, hint_color);
    } else {
        DrawText(hint_text, (window_w - hint_w) / 2, hint_y, 1, hint_color);
    }

    if (window_w < 500) {
        std::string short_hotkey_hint = "F5:Q-SAVE | F9:Q-LOAD | TAB:VIEW";
        int short_w = static_cast<int>(short_hotkey_hint.length()) * 8;
        DrawText(short_hotkey_hint, (window_w - short_w) / 2, hotkey_y, 1, hint_color);
    } else {
        DrawText(hotkey_hint, (window_w - hotkey_w) / 2, hotkey_y, 1, hint_color);
    }

    // Draw Reset Player toggle setting
    SDL_Color toggle_color = reset_player_on_transition ? SDL_Color{ 180, 255, 180, 255 } : SDL_Color{ 255, 180, 180, 255 };
    char reset_str[128];
    snprintf(reset_str, sizeof(reset_str), "RESET PLAYER ON TRANSITION: %s (T to toggle)", reset_player_on_transition ? "ON" : "OFF");
    int reset_w = static_cast<int>(strlen(reset_str)) * 8;
    DrawText(reset_str, (window_w - reset_w) / 2, reset_player_y, 1, toggle_color);

    // Draw ROM & FM2 selection info
    SDL_Color file_label_color = { 100, 200, 255, 255 }; // cyan/light blue
    SDL_Color action_color = { 255, 180, 50, 255 }; // orange/gold

    auto truncate_str = [](const std::string& str, size_t max_len) -> std::string {
        if (str.length() <= max_len) return str;
        return str.substr(0, max_len - 3) + "...";
    };

    size_t max_name_len = (window_w < 500) ? 22 : 36;
    std::string rom_disp = truncate_str(current_rom ? current_rom : "NONE", max_name_len);
    std::string fm2_disp = truncate_str(current_fm2 ? current_fm2 : "NONE", max_name_len);

    char rom_str[128];
    char fm2_str[128];
    snprintf(rom_str, sizeof(rom_str), "ROM: [%s] (R to cycle)", rom_disp.c_str());
    snprintf(fm2_str, sizeof(fm2_str), "FM2: [%s] (F to cycle)", fm2_disp.c_str());
    std::string split_str = "Press S to SPLIT & APPLY SELECTED REFERENCE";

    int rom_w = static_cast<int>(strlen(rom_str)) * 8;
    int fm2_w = static_cast<int>(strlen(fm2_str)) * 8;
    int split_w = static_cast<int>(split_str.length()) * 8;

    DrawText(rom_str, (window_w - rom_w) / 2, files_y1, 1, file_label_color);
    DrawText(fm2_str, (window_w - fm2_w) / 2, files_y2, 1, file_label_color);
    DrawText(split_str, (window_w - split_w) / 2, files_y3, 1, action_color);

    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_NONE);
    SDL_RenderPresent(sdl_renderer);
}

void Renderer::DrawText(const std::string& text, int x, int y, int scale_val, SDL_Color color) {
    SDL_SetRenderDrawColor(sdl_renderer, color.r, color.g, color.b, color.a);

    int current_x = x;
    for (char c : text) {
        if (c < 32 || c > 127) {
            current_x += 8 * scale_val;
            continue;
        }

        const uint8_t* glyph = font8x8[c - 32];
        for (int r = 0; r < 8; ++r) {
            uint8_t row = glyph[r];
            for (int col = 0; col < 8; ++col) {
                if ((row >> (7 - col)) & 1) {
                    SDL_Rect rect = {
                        current_x + col * scale_val,
                        y + r * scale_val,
                        scale_val,
                        scale_val
                    };
                    SDL_RenderFillRect(sdl_renderer, &rect);
                }
            }
        }

        current_x += 8 * scale_val;
    }
}

void Renderer::DrawBindingsMenu(int selected_row, bool in_rebinding) {
    // Semi-transparent overlay
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
    SDL_Rect overlay = { 0, 0, window_w, window_h };
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 220);
    SDL_RenderFillRect(sdl_renderer, &overlay);

    // Title
    SDL_Color title_color = { 255, 220, 100, 255 }; // gold
    std::string title_text = "CONFIGURE CONTROLS";
    int title_scale = (scale == 1) ? 2 : 3;
    int title_w = static_cast<int>(title_text.length()) * 8 * title_scale;
    int start_y = (scale == 1) ? 35 : 60;
    DrawText(title_text, (window_w - title_w) / 2, start_y, title_scale, title_color);

    // Columns positioning
    int col_width = (scale == 1) ? 240 : 480;
    int col1_x = (window_w - col_width) / 2;
    int col2_x = col1_x + (scale == 1 ? 100 : 200);
    int col3_x = col1_x + (scale == 1 ? 180 : 360);

    // Headers
    SDL_Color header_color = { 100, 200, 255, 255 }; // cyan
    int header_y = start_y + (scale == 1 ? 24 : 40);
    int text_scale = 1;
    DrawText("ACTION", col1_x, header_y, text_scale, header_color);
    DrawText("KEYBOARD", col2_x, header_y, text_scale, header_color);
    DrawText("GAMEPAD", col3_x, header_y, text_scale, header_color);

    const char* action_names[] = {
        "NES UP", "NES DOWN", "NES LEFT", "NES RIGHT",
        "NES A", "NES B", "NES START", "NES SELECT",
        "QUICKSAVE", "QUICKLOAD", "SAVE PB", "RESTART LEVEL",
        "TOGGLE VIEW", "TOGGLE MENU"
    };
    int num_actions = 14;

    int spacing = (scale == 1) ? 11 : 20;
    int list_start_y = header_y + (scale == 1 ? 12 : 24);

    for (int i = 0; i < num_actions; i++) {
        int row_y = list_start_y + i * spacing;

        SDL_Color text_color = { 220, 220, 240, 255 }; // normal text
        if (i == selected_row) {
            // Draw a subtle highlighted background for selected row
            SDL_Rect row_rect = { col1_x - 4, row_y - 2, col_width + 8, spacing };
            SDL_SetRenderDrawColor(sdl_renderer, 40, 60, 90, 255); // blue-gray highlight
            SDL_RenderFillRect(sdl_renderer, &row_rect);
            text_color = { 255, 255, 255, 255 };
        }

        // Action Name
        DrawText(action_names[i], col1_x, row_y, text_scale, text_color);

        // Key Name and Button Name
        std::string key_str = GetActionKeyName(i);
        std::string btn_str = GetActionButtonName(i);

        if (i == selected_row && in_rebinding) {
            SDL_Color rebind_color = { 255, 120, 50, 255 }; // orange
            DrawText("PRESS KEY/BTN...", col2_x, row_y, text_scale, rebind_color);
        } else {
            DrawText(key_str, col2_x, row_y, text_scale, text_color);
            DrawText(btn_str, col3_x, row_y, text_scale, text_color);
        }
    }

    // Prompts at the bottom
    SDL_Color hint_color = { 160, 160, 180, 255 };
    int bottom_y1 = list_start_y + num_actions * spacing + (scale == 1 ? 10 : 20);
    int bottom_y2 = bottom_y1 + (scale == 1 ? 10 : 16);

    std::string prompt1 = "UP/DOWN: SELECT ACTION | SPACE/ENTER: REBIND";
    std::string prompt2 = "ESC/C: SAVE AND CLOSE";

    if (window_w < 500) {
        prompt1 = "UP/DN:NAV | SPACE:REBIND";
        prompt2 = "ESC/C:SAVE & CLOSE";
    }

    int p1_w = static_cast<int>(prompt1.length()) * 8;
    int p2_w = static_cast<int>(prompt2.length()) * 8;

    DrawText(prompt1, (window_w - p1_w) / 2, bottom_y1, text_scale, hint_color);
    DrawText(prompt2, (window_w - p2_w) / 2, bottom_y2, text_scale, hint_color);

    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_NONE);
    SDL_RenderPresent(sdl_renderer);
}

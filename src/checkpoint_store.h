#ifndef GHOST_MIRROR_CHECKPOINT_STORE_H
#define GHOST_MIRROR_CHECKPOINT_STORE_H

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "atomic_file.h"
#include "nes/nes.h"
#include "route_manifest.h"

struct PracticeCheckpoint {
    std::string profile_key;
    std::string stage;
    std::string player_state, pb_state, tas_state;
    std::string recorder_start_state;
    std::vector<sta::nes::ControllerState> recorder_inputs;
    int frame = 0, pb_frame = 0, tas_frame = 0;
    int prev_world = -1, prev_level = -1;
    bool recorder_active = false, level_loading = false;
};

struct CheckpointStore {
    static constexpr int SLOTS = 5;

    static std::string Path(const std::string& profile_dir, const std::string& stage, int slot) {
        if (!RouteManifest::SafeName(stage) || slot < 1 || slot > SLOTS)
            throw std::invalid_argument("Invalid checkpoint stage or slot");
        return (std::filesystem::path(profile_dir) / "checkpoints" /
                (stage + "-" + std::to_string(slot) + ".gmc")).string();
    }

    static void Number(std::ostream& os, uint32_t v) {
        for (int i = 0; i < 4; ++i) os.put(static_cast<char>(v >> (8 * i)));
    }
    static uint32_t Number(std::istream& is) {
        uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            const int c = is.get();
            if (c == EOF) throw std::runtime_error("Truncated checkpoint");
            value |= uint32_t(static_cast<unsigned char>(c)) << (8 * i);
        }
        return value;
    }
    static void String(std::ostream& os, const std::string& s) {
        Number(os, static_cast<uint32_t>(s.size()));
        os.write(s.data(), static_cast<std::streamsize>(s.size()));
    }
    static std::string String(std::istream& is, uint32_t max = 8 * 1024 * 1024) {
        const uint32_t len = Number(is);
        if (len > max) throw std::runtime_error("Checkpoint field too large");
        std::string s(len, '\0');
        is.read(s.data(), len);
        if (!is) throw std::runtime_error("Truncated checkpoint content");
        return s;
    }
    static bool Save(const std::string& profile_dir, int slot, const PracticeCheckpoint& cp) {
        try {
            if (cp.profile_key.empty() || cp.stage.empty() || cp.player_state.empty() ||
                cp.pb_state.empty() || cp.tas_state.empty() ||
                cp.recorder_inputs.size() > 1000000 || cp.frame < 0 ||
                cp.pb_frame < 0 || cp.tas_frame < 0) return false;
            const std::string path = Path(profile_dir, cp.stage, slot);
            std::filesystem::create_directories(std::filesystem::path(path).parent_path());
            return AtomicWriteFile(path, [&](std::ostream& os) {
                os.write("GMCP3", 5);
                String(os, cp.profile_key);
                String(os, cp.stage);
                Number(os, static_cast<uint32_t>(cp.frame));
                Number(os, static_cast<uint32_t>(cp.pb_frame));
                Number(os, static_cast<uint32_t>(cp.tas_frame));
                Number(os, static_cast<uint32_t>(cp.prev_world + 1));
                Number(os, static_cast<uint32_t>(cp.prev_level + 1));
                Number(os, cp.recorder_active ? 1 : 0);
                Number(os, cp.level_loading ? 1 : 0);
                String(os, cp.player_state);
                String(os, cp.pb_state);
                String(os, cp.tas_state);
                String(os, cp.recorder_start_state);
                Number(os, static_cast<uint32_t>(cp.recorder_inputs.size()));
                for (auto c : cp.recorder_inputs) os.put(static_cast<char>(c));
            });
        } catch (const std::exception& e) {
            std::cerr << "ERROR saving checkpoint: " << e.what() << std::endl;
            return false;
        }
    }

    static bool Load(const std::string& profile_dir, const std::string& stage, int slot,
                     const std::string& expected_profile, PracticeCheckpoint* out) {
        try {
            std::ifstream is(Path(profile_dir, stage, slot), std::ios::binary);
            if (!is) return false;
            char magic[5];
            is.read(magic, sizeof magic);
            if (!is || std::string(magic, sizeof magic) != "GMCP3") return false;
            PracticeCheckpoint cp;
            cp.profile_key = String(is, 512);
            cp.stage = String(is, 64);
            if (cp.profile_key != expected_profile || cp.stage != stage) return false;
            const auto frame = Number(is), pb = Number(is), tas = Number(is);
            const auto prev_world = Number(is), prev_level = Number(is);
            const auto active = Number(is), loading = Number(is);
            if (frame > 100000000 || pb > frame || tas > frame ||
                prev_world > 8 || prev_level > 4 || active > 1 || loading > 1) return false;
            cp.frame = static_cast<int>(frame);
            cp.pb_frame = static_cast<int>(pb);
            cp.tas_frame = static_cast<int>(tas);
            cp.prev_world = static_cast<int>(prev_world) - 1;
            cp.prev_level = static_cast<int>(prev_level) - 1;
            cp.recorder_active = active != 0;
            cp.level_loading = loading != 0;
            cp.player_state = String(is);
            cp.pb_state = String(is);
            cp.tas_state = String(is);
            cp.recorder_start_state = String(is);
            const auto count = Number(is);
            if (count > 1000000 || count > frame) return false;
            cp.recorder_inputs.resize(count);
            is.read(reinterpret_cast<char*>(cp.recorder_inputs.data()), count);
            if (!is || is.peek() != EOF || cp.player_state.empty() ||
                cp.pb_state.empty() || cp.tas_state.empty()) return false;
            *out = std::move(cp);
            return true;
        } catch (const std::exception& e) {
            std::cerr << "ERROR loading checkpoint: " << e.what() << std::endl;
            return false;
        }
    }
};

#endif

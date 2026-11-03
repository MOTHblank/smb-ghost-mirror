#ifndef GHOST_MIRROR_REFERENCE_PROFILE_H
#define GHOST_MIRROR_REFERENCE_PROFILE_H

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "atomic_file.h"
#include "rom_metadata.h"
#include "route_manifest.h"

inline std::string GhostHash(const std::string& bytes) {
    uint64_t hash = 14695981039346656037ULL;
    for (unsigned char c : bytes) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    std::ostringstream os;
    os << std::hex << std::setfill('0') << std::setw(16) << hash
       << "-" << std::dec << bytes.size();
    return os.str();
}

inline std::string GhostReadBinary(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot open reference: " + path);
    std::ostringstream data;
    data << stream.rdbuf();
    if (!stream.eof() && stream.fail())
        throw std::runtime_error("Cannot read reference: " + path);
    return data.str();
}

struct ReferenceProfile {
    std::string rom_checksum;
    std::string tas_identity;
    std::string route_identity;
    std::string key;
    std::string directory;

    static ReferenceProfile Create(const std::string& root, const std::string& rom_path,
                                   const std::string& fm2_path, const RouteManifest& route) {
        ReferenceProfile profile;
        const std::string rom_data = GhostReadBinary(rom_path);
        const std::string fm2_data = GhostReadBinary(fm2_path);
        sta::nes::FM2Header rom_header;
        if (!MakeRomFM2Header(rom_path, rom_data, &rom_header))
            throw std::runtime_error("Invalid ROM; cannot identify reference profile");
        profile.rom_checksum = rom_header.romChecksum;
        // Refuse TAS input recorded against another ROM before any split or replay.
        std::istringstream fm2_stream(fm2_data);
        sta::nes::FM2Header tas_header;
        std::vector<sta::nes::ControllerState> inputs;
        sta::nes::ReadFM2File(fm2_stream, &inputs, &tas_header);
        if (tas_header.romChecksum != profile.rom_checksum)
            throw std::runtime_error("TAS ROM checksum does not match selected ROM");
        profile.tas_identity = GhostHash(fm2_data);
        profile.route_identity = GhostHash(route.IdentitySource());
        std::string rom_component = profile.rom_checksum.substr(7);
        for (char& c : rom_component) {
            if (c == '/') c = '_';
            else if (c == '+') c = '-';
            else if (c == '=') c = '~';
        }
        profile.key = route.id + "|" + profile.route_identity + "|" +
                      profile.rom_checksum + "|" + profile.tas_identity;
        profile.directory = (std::filesystem::path(root) / "profiles" /
            (route.id + "-" + profile.route_identity) /
            rom_component / profile.tas_identity).string();
        return profile;
    }

    std::string MarkerPath() const {
        return (std::filesystem::path(directory) / "profile.txt").string();
    }

    bool IsConsistent() const {
        std::ifstream in(MarkerPath(), std::ios::binary);
        if (!in) return false;
        std::string existing((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        return existing == "ghost-mirror-profile-v1\n" + key + "\n";
    }

    bool Prepare() const {
        try {
            std::filesystem::create_directories(directory);
            if (std::filesystem::exists(MarkerPath())) return IsConsistent();
            return AtomicWriteFile(MarkerPath(), [&](std::ostream& os) {
                os << "ghost-mirror-profile-v1\n" << key << "\n";
            });
        } catch (const std::exception& e) {
            std::cerr << "ERROR: Cannot prepare profile: " << e.what() << std::endl;
            return false;
        }
    }
};

inline std::string GhostProfileTag(const std::string& key) {
    return "comment ghost-mirror profile " + key;
}
inline std::string GhostStageTag(const std::string& id) {
    return "comment ghost-mirror stage " + id;
}

// Reject replays copied across profiles, stages or ROMs even when filenames match.
inline bool GhostLoadTaggedReplay(const std::string& path, const ReferenceProfile& profile,
                                  const std::string& stage, std::vector<sta::nes::ControllerState>* inputs) {
    std::ifstream stream(path);
    if (!stream || !profile.IsConsistent()) return false;
    try {
        sta::nes::FM2Header header;
        std::vector<sta::nes::ControllerState> parsed;
        sta::nes::ReadFM2File(stream, &parsed, &header);
        if (header.romChecksum != profile.rom_checksum) return false;
        bool profile_ok = false, stage_ok = false;
        for (const auto& line : header.additionalLines) {
            if (line == GhostProfileTag(profile.key)) profile_ok = true;
            if (line == GhostStageTag(stage)) stage_ok = true;
        }
        if (!profile_ok || !stage_ok || parsed.empty()) return false;
        if (inputs) *inputs = std::move(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

#endif

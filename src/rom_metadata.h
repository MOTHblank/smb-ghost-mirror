#ifndef GHOST_MIRROR_ROM_METADATA_H
#define GHOST_MIRROR_ROM_METADATA_H

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
#include "nes/nes.h"

// FCEUX's ROM checksum is a base64-encoded MD5 of the cartridge PRG+CHR
// contents, not the iNES header or optional 512-byte trainer.
inline bool MakeRomFM2Header(const std::string& rom_path,
                            const std::string& rom, sta::nes::FM2Header* header) {
    if (rom.size() < 16 || rom.compare(0, 4, std::string("NES\x1a", 4)) != 0) return false;
    const auto u8 = [&](size_t i) { return static_cast<uint8_t>(rom[i]); };
    const bool nes2 = (u8(7) & 0x0c) == 0x08;
    const auto rom_size = [&](size_t lsb, size_t upper, uint64_t unit) -> uint64_t {
        if (!nes2) return static_cast<uint64_t>(u8(lsb)) * unit;
        const uint8_t high = upper == 0 ? (u8(9) & 0x0f) : (u8(9) >> 4);
        if (high != 15) return (static_cast<uint64_t>(high) << 8 | u8(lsb)) * unit;
        const unsigned shift = u8(lsb) >> 2;
        return shift < 48 ? (uint64_t{1} << shift) * (2 * (u8(lsb) & 3) + 1) : 0;
    };
    const uint64_t bytes = rom_size(4, 0, 16384) + rom_size(5, 1, 8192);
    const size_t start = 16 + ((u8(6) & 4) ? 512 : 0);
    if (bytes == 0 || start > rom.size() || bytes > rom.size() - start) return false;

    std::vector<uint8_t> msg(rom.begin() + start, rom.begin() + start + static_cast<size_t>(bytes));
    const uint64_t bit_length = static_cast<uint64_t>(msg.size()) * 8;
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) msg.push_back(0);
    for (int i = 0; i < 8; ++i) msg.push_back(static_cast<uint8_t>(bit_length >> (8 * i)));

    const uint32_t initial[4] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u};
    uint32_t state[4] = {initial[0], initial[1], initial[2], initial[3]};
    static constexpr unsigned shifts[64] = {
        7,12,17,22, 7,12,17,22, 7,12,17,22, 7,12,17,22,
        5,9,14,20, 5,9,14,20, 5,9,14,20, 5,9,14,20,
        4,11,16,23, 4,11,16,23, 4,11,16,23, 4,11,16,23,
        6,10,15,21, 6,10,15,21, 6,10,15,21, 6,10,15,21
    };
    for (size_t offset = 0; offset < msg.size(); offset += 64) {
        uint32_t block[16];
        for (int i = 0; i < 16; ++i) {
            block[i] = 0;
            for (int j = 0; j < 4; ++j) {
                block[i] |= static_cast<uint32_t>(msg[offset + i * 4 + j]) << (8 * j);
            }
        }
        uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
        for (int i = 0; i < 64; ++i) {
            uint32_t f;
            int g;
            if (i < 16) { f = (b & c) | (~b & d); g = i; }
            else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) % 16; }
            else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) % 16; }
            else { f = c ^ (b | ~d); g = (7 * i) % 16; }
            const uint32_t k = static_cast<uint32_t>(
                std::floor(std::abs(std::sin(static_cast<double>(i + 1))) * 4294967296.0));
            const uint32_t sum = a + f + k + block[g];
            const uint32_t rotated = (sum << shifts[i]) | (sum >> (32 - shifts[i]));
            a = d; d = c; c = b; b += rotated;
        }
        state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    }

    std::array<uint8_t, 16> digest{};
    for (size_t i = 0; i < 16; ++i) digest[i] = static_cast<uint8_t>(state[i / 4] >> (8 * (i % 4)));
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string base64;
    for (size_t i = 0; i < digest.size(); i += 3) {
        const unsigned value = (unsigned(digest[i]) << 16) |
                               (i + 1 < digest.size() ? unsigned(digest[i + 1]) << 8 : 0) |
                               (i + 2 < digest.size() ? unsigned(digest[i + 2]) : 0);
        base64.push_back(alphabet[(value >> 18) & 63]);
        base64.push_back(alphabet[(value >> 12) & 63]);
        base64.push_back(i + 1 < digest.size() ? alphabet[(value >> 6) & 63] : '=');
        base64.push_back(i + 2 < digest.size() ? alphabet[value & 63] : '=');
    }

    *header = sta::nes::FM2Header::Defaults();
    header->romFilename = std::filesystem::path(rom_path).filename().string();
    header->romChecksum = "base64:" + base64;
    return true;
}

#endif

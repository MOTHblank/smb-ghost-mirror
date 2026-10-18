#ifndef GHOST_MIRROR_ATOMIC_FILE_H
#define GHOST_MIRROR_ATOMIC_FILE_H

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif

// Write beside the destination, then replace it without ever truncating the
// previous valid file. A failure leaves the old file unchanged.
template <typename Write>
bool AtomicWriteFile(const std::string& filename, Write write) {
    namespace fs = std::filesystem;
    static std::atomic<unsigned long long> counter{0};
    const fs::path target(filename);
    fs::path temp = target;
    temp += ".tmp." + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
            "." + std::to_string(counter.fetch_add(1));

    try {
        std::ofstream os(temp, std::ios::binary | std::ios::trunc);
        if (!os) throw std::runtime_error("Cannot open temporary file");
        write(os);
        os.flush();
        if (!os) throw std::runtime_error("Write or flush failed");
        os.close();
        if (!os) throw std::runtime_error("Close failed");

#ifdef _WIN32
        // std::filesystem::rename is not consistently replace-existing on Windows.
        if (!MoveFileExW(temp.c_str(), target.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            throw std::runtime_error("Atomic replace failed (Windows error " +
                                     std::to_string(GetLastError()) + ")");
        }
#else
        fs::rename(temp, target);
#endif
        return true;
    } catch (const std::exception& e) {
        std::error_code ignored;
        fs::remove(temp, ignored);
        std::cerr << "ERROR: Cannot save " << filename << ": " << e.what() << std::endl;
        return false;
    }
}

#endif

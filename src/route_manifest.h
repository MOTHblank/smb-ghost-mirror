#ifndef GHOST_MIRROR_ROUTE_MANIFEST_H
#define GHOST_MIRROR_ROUTE_MANIFEST_H

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// Text manifest: id=..., name=..., stage=id,world,level[,state,tas,pb],
// shortcut=from,to. World and level are one-based in the manifest.
struct RouteStage {
    std::string id;
    int world = 0;
    int level = 0;
    std::string state_file;
    std::string tas_file;
    std::string pb_file;
};

struct RouteManifest {
    std::string id;
    std::string name;
    std::vector<RouteStage> stages;
    std::vector<std::pair<std::string, std::string>> shortcuts;

    static bool SafeName(const std::string& name) {
        return !name.empty() && name.size() <= 64 &&
            std::all_of(name.begin(), name.end(), [](unsigned char c) {
                return std::isalnum(c) || c == '-' || c == '_' || c == '.';
            }) && name != "." && name != "..";
    }

    static std::string Trim(const std::string& s) {
        const size_t begin = s.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos) return {};
        const size_t end = s.find_last_not_of(" \t\r\n");
        return s.substr(begin, end - begin + 1);
    }

    int Find(const std::string& stage_id) const {
        for (size_t i = 0; i < stages.size(); ++i)
            if (stages[i].id == stage_id) return static_cast<int>(i);
        return -1;
    }

    int Find(int world_zero_based, int level_zero_based) const {
        for (size_t i = 0; i < stages.size(); ++i)
            if (stages[i].world == world_zero_based && stages[i].level == level_zero_based)
                return static_cast<int>(i);
        return -1;
    }

    bool AllowsTransition(int from, int to) const {
        if (from < 0 || from >= static_cast<int>(stages.size()) ||
            to <= from || to >= static_cast<int>(stages.size())) return false;
        if (to == from + 1) return true;
        for (const auto& shortcut : shortcuts)
            if (shortcut.first == stages[from].id && shortcut.second == stages[to].id)
                return true;
        return false;
    }

    // A canonical representation makes edits to stage definitions change the profile
    // while harmless whitespace or comments do not.
    std::string IdentitySource() const {
        std::string result = id + "\n";
        for (const auto& s : stages)
            result += s.id + "," + std::to_string(s.world) + "," +
                      std::to_string(s.level) + "," + s.state_file + "," +
                      s.tas_file + "," + s.pb_file + "\n";
        for (const auto& e : shortcuts) result += e.first + ">" + e.second + "\n";
        return result;
    }

    static RouteManifest Load(const std::string& path) {
        std::ifstream input(path);
        if (!input) throw std::runtime_error("Cannot open route manifest: " + path);
        RouteManifest route;
        std::string line;
        int line_no = 0;
        while (std::getline(input, line)) {
            ++line_no;
            line = Trim(line);
            if (line.empty() || line[0] == '#') continue;
            const auto eq = line.find('=');
            if (eq == std::string::npos) throw std::runtime_error("Malformed route line " + std::to_string(line_no));
            const std::string key = Trim(line.substr(0, eq));
            const std::string value = Trim(line.substr(eq + 1));
            if (key == "id") {
                if (!route.id.empty() || !SafeName(value))
                    throw std::runtime_error("Invalid or duplicate route id");
                route.id = value;
            } else if (key == "name") {
                if (value.empty() || value.size() > 100 || !route.name.empty())
                    throw std::runtime_error("Invalid or duplicate route name");
                route.name = value;
            } else if (key == "stage") {
                std::vector<std::string> fields;
                std::istringstream parts(value);
                std::string part;
                while (std::getline(parts, part, ',')) fields.push_back(Trim(part));
                if (fields.size() < 3 || fields.size() > 6 || !SafeName(fields[0]))
                    throw std::runtime_error("Invalid stage on route line " + std::to_string(line_no));
                RouteStage stage;
                stage.id = fields[0];
                try {
                    size_t used_world = 0, used_level = 0;
                    stage.world = std::stoi(fields[1], &used_world) - 1;
                    stage.level = std::stoi(fields[2], &used_level) - 1;
                    if (used_world != fields[1].size() || used_level != fields[2].size())
                        throw std::invalid_argument("trailing coordinates");
                } catch (...) {
                    throw std::runtime_error("Invalid world/level on route line " + std::to_string(line_no));
                }
                if (stage.world < 0 || stage.world > 7 || stage.level < 0 || stage.level > 3 ||
                    route.Find(stage.id) >= 0 || route.Find(stage.world, stage.level) >= 0)
                    throw std::runtime_error("Duplicate or out-of-range stage");
                stage.state_file = fields.size() > 3 ? fields[3] : stage.id + ".state";
                stage.tas_file = fields.size() > 4 ? fields[4] : stage.id + ".tas.fm2";
                stage.pb_file = fields.size() > 5 ? fields[5] : stage.id + ".pb.fm2";
                if (!SafeName(stage.state_file) || !SafeName(stage.tas_file) || !SafeName(stage.pb_file))
                    throw std::runtime_error("Invalid reference filename");
                route.stages.push_back(stage);
                if (route.stages.size() > 64) throw std::runtime_error("Route exceeds 64 stages");
            } else if (key == "shortcut") {
                const auto comma = value.find(',');
                if (comma == std::string::npos || value.find(',', comma + 1) != std::string::npos)
                    throw std::runtime_error("Invalid shortcut line");
                route.shortcuts.emplace_back(Trim(value.substr(0, comma)), Trim(value.substr(comma + 1)));
            } else {
                throw std::runtime_error("Unknown route directive: " + key);
            }
        }
        if (!SafeName(route.id) || route.stages.empty())
            throw std::runtime_error("Route requires an id and at least one stage");
        if (route.name.empty()) route.name = route.id;
        for (const auto& link : route.shortcuts) {
            const int from = route.Find(link.first), to = route.Find(link.second);
            if (from < 0 || to <= from)
                throw std::runtime_error("Shortcut must skip forward to an existing stage");
        }
        return route;
    }
};

#endif

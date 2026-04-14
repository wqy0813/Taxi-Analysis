#include "appconfig.h"
#include "logger.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>

namespace fs = std::filesystem;

namespace {

std::string trim(const std::string& input) {
    std::size_t left = 0;
    while (left < input.size() && std::isspace(static_cast<unsigned char>(input[left]))) {
        ++left;
    }

    std::size_t right = input.size();
    while (right > left && std::isspace(static_cast<unsigned char>(input[right - 1]))) {
        --right;
    }

    return input.substr(left, right - left);
}

std::string stripInlineComment(const std::string& input) {
    bool in_quote = false;
    for (std::size_t i = 0; i < input.size(); ++i) {
        const char ch = input[i];
        if (ch == '"' || ch == '\'') {
            in_quote = !in_quote;
        }
        if (!in_quote && (ch == ';' || ch == '#')) {
            return trim(input.substr(0, i));
        }
    }
    return trim(input);
}

using IniMap = std::unordered_map<std::string, std::unordered_map<std::string, std::string>>;

IniMap parseIniFile(const std::string& configPath) {
    IniMap result;
    std::ifstream fin(configPath);
    if (!fin.is_open()) {
        std::cerr << "Failed to open config file: " << configPath << std::endl;
        return result;
    }

    std::string currentSection;
    std::string line;
    while (std::getline(fin, line)) {
        line = trim(line);
        if (line.empty()) {
            continue;
        }

        if (line[0] == ';' || line[0] == '#') {
            continue;
        }

        if (line.front() == '[' && line.back() == ']') {
            currentSection = trim(line.substr(1, line.size() - 2));
            continue;
        }

        const std::size_t pos = line.find('=');
        if (pos == std::string::npos) {
            continue;
        }

        const std::string key = trim(line.substr(0, pos));
        const std::string value = stripInlineComment(line.substr(pos + 1));
        result[currentSection][key] = value;
    }

    return result;
}

std::string getString(
    const IniMap& ini,
    const std::string& section,
    const std::string& key,
    const std::string& defaultValue) {
    const auto sectionIt = ini.find(section);
    if (sectionIt == ini.end()) {
        return defaultValue;
    }
    const auto keyIt = sectionIt->second.find(key);
    if (keyIt == sectionIt->second.end()) {
        return defaultValue;
    }
    return keyIt->second;
}

int getInt(const IniMap& ini, const std::string& section, const std::string& key, const int defaultValue) {
    try {
        return std::stoi(getString(ini, section, key, std::to_string(defaultValue)));
    } catch (...) {
        return defaultValue;
    }
}

double getDouble(const IniMap& ini, const std::string& section, const std::string& key, const double defaultValue) {
    try {
        return std::stod(getString(ini, section, key, std::to_string(defaultValue)));
    } catch (...) {
        return defaultValue;
    }
}

std::string resolvePath(const fs::path& configDir, const std::string& pathString) {
    if (pathString.empty()) {
        return pathString;
    }

    fs::path path(pathString);
    if (path.is_absolute()) {
        return path.lexically_normal().string();
    }

    return (configDir / path).lexically_normal().string();
}

} // namespace

AppConfig AppConfig::load(const std::string& configPath) {
    const IniMap ini = parseIniFile(configPath);
    const fs::path configDir = fs::absolute(fs::path(configPath)).parent_path();

    AppConfig cfg;

    cfg.dataDir = resolvePath(configDir, getString(ini, "Paths", "data_dir", "./data"));
    cfg.dbPath = resolvePath(configDir, getString(ini, "Paths", "db_path", "./taxi_data.db"));
    cfg.mapPath = resolvePath(configDir, getString(ini, "Paths", "map_path", "./map.html"));

    const std::string fallbackDataDir = (configDir / "data").lexically_normal().string();
    if (!fs::exists(cfg.dataDir) && fs::exists(fallbackDataDir)) {
        cfg.dataDir = fallbackDataDir;
    }

    if (!fs::exists(cfg.dbPath)) {
        std::cerr << "Configured db_path does not exist: " << cfg.dbPath << std::endl;
    } else if (fs::file_size(cfg.dbPath) <= 8192) {
        std::cerr << "Configured db_path looks too small (<= 8KB): " << cfg.dbPath << std::endl;
    }

    cfg.minLon = getDouble(ini, "Filter", "min_lon", 115.0);
    cfg.maxLon = getDouble(ini, "Filter", "max_lon", 118.0);
    cfg.minLat = getDouble(ini, "Filter", "min_lat", 39.0);
    cfg.maxLat = getDouble(ini, "Filter", "max_lat", 41.0);

    cfg.batchSize = getInt(ini, "Import", "batch_size", 500);

    {
        int portValue = getInt(ini, "Server", "port", 8080);
        if (portValue < 1 || portValue > 65535) {
            portValue = 8080;
        }
        cfg.serverPort = static_cast<std::uint16_t>(portValue);
    }

    cfg.mapCenterLon = getDouble(ini, "Map", "center_lon", 116.404);
    cfg.mapCenterLat = getDouble(ini, "Map", "center_lat", 39.915);
    cfg.mapInitialZoom = getInt(ini, "Map", "initial_zoom", 12);
    cfg.mapMinZoom = getInt(ini, "Map", "min_zoom", 8);
    cfg.mapMaxZoom = getInt(ini, "Map", "max_zoom", 18);
    cfg.baiduMapAk = getString(ini, "Map", "baidu_ak", cfg.baiduMapAk);

    cfg.rectCapacity = getInt(ini, "QuadTree", "rect_capacity", 500);
    cfg.maxQuadTreeDepth = getInt(ini, "QuadTree", "max_depth", 64);
    cfg.minQuadCellSize = getDouble(ini, "QuadTree", "min_cell_size", 1e-7);

    return cfg;
}

AppConfig AppConfigManager::config;

void AppConfigManager::init(const std::string& configPath) {
    config = AppConfig::load(configPath);
}

const AppConfig& AppConfigManager::get() {
    return config;
}

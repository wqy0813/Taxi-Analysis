#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include "appconfig.h"
#include "databasemanager.h"
#include "datamanager.h"
#include "httpserver.h"
#include "logger.h"

namespace fs = std::filesystem;

std::string findConfigPath(const char* argv0) {
    std::vector<fs::path> candidates;
    const fs::path exePath = argv0 ? fs::absolute(fs::path(argv0)) : fs::current_path();
    const fs::path exeDir = exePath.parent_path();

    candidates.push_back(exeDir / "../config.ini");
    candidates.push_back(exeDir / "../../config.ini");
    candidates.push_back(exeDir / "config.ini");
    candidates.push_back(fs::current_path() / "config.ini");

    for (const auto& path : candidates) {
        if (fs::exists(path)) {
            return path.lexically_normal().string();
        }
    }

    return fs::absolute(fs::current_path() / "config.ini").lexically_normal().string();
}

int main(int argc, char* argv[]) {
    std::string configPath;
    if (argc > 1) {
        configPath = fs::absolute(fs::path(argv[1])).lexically_normal().string();
    }
    if (configPath.empty() || !fs::exists(configPath)) {
        configPath = findConfigPath(argc > 0 ? argv[0] : nullptr);
    }

    AppConfigManager::init(configPath);
    const AppConfig& config = AppConfigManager::get();

    Debug() << "Config path: " << configPath << std::endl;
    Debug() << "Database path: " << config.dbPath << std::endl;

    DatabaseManager dbm(config.dbPath);
    if (!dbm.open()) {
        Error() << "Failed to open database." << std::endl;
        return -1;
    }

    DatabaseManager::checkAndImportData(dbm, config);

    if (!DataManager::loadFromDatabase(dbm)) {
        Error() << "Failed to load points from database." << std::endl;
        return -1;
    }

    Debug() << "Loaded point count: " << DataManager::getAllPoints().size() << std::endl;

    DataManager::buildQuadTree(config);
    Debug() << "Data loaded. Open the browser at: http://127.0.0.1:" << config.serverPort << "/" << std::endl;

    HttpServer server(config);
    if (!server.start(config.serverPort)) {
        Error() << "Failed to start HTTP server." << std::endl;
        return -1;
    }

    Debug() << "HTTP server listening at http://127.0.0.1:" << config.serverPort << std::endl;
    return 0;
}

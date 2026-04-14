#ifndef APPCONFIG_H
#define APPCONFIG_H

#include <cstdint>
#include <string>

struct AppConfig {
    std::string dataDir;
    std::string dbPath;
    std::string mapPath;

    double minLon = 115.0;
    double maxLon = 118.0;
    double minLat = 39.0;
    double maxLat = 41.0;

    int batchSize = 500;
    std::uint16_t serverPort = 8080;

    double mapCenterLon = 116.404;
    double mapCenterLat = 39.915;
    int mapInitialZoom = 12;
    int mapMinZoom = 8;
    int mapMaxZoom = 18;
    std::string baiduMapAk = "hf0NAP9ccSVWWMCH0gb0jrZqM0kfwclr";
    int rectCapacity = 500;

    int maxQuadTreeDepth = 64;
    double minQuadCellSize = 1e-7;

    static AppConfig load(const std::string& configPath);
};

class AppConfigManager {
public:
    static void init(const std::string& configPath);
    static const AppConfig& get();

private:
    static AppConfig config;
};

#endif

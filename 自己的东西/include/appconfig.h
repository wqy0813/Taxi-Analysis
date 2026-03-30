#ifndef APPCONFIG_H
#define APPCONFIG_H

#include <QString>

struct AppConfig {
    QString dataDir;
    QString dbPath;
    QString mapPath;

    double minLon;
    double maxLon;
    double minLat;
    double maxLat;

    int batchSize;

    double mapCenterLon;
    double mapCenterLat;
    int mapInitialZoom;
    int mapMinZoom;
    int mapMaxZoom;
    int rectCapacity;
    static AppConfig load(const QString& configPath);
};
class AppConfigManager {
public:
    static void init(const QString& configPath);
    static const AppConfig& get();

private:
    static AppConfig config;
};
#endif // APPCONFIG_H
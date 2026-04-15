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

    static AppConfig load(const QString& configPath);
};

#endif // APPCONFIG_H
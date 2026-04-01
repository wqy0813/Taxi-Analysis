#include "appconfig.h"
#include <QDir>
#include <QFileInfo>
#include <QSettings>

AppConfig AppConfig::load(const QString& configPath) {
    QSettings settings(configPath, QSettings::IniFormat);
    const QFileInfo configInfo(configPath);
    const QDir configDir = configInfo.dir();

    const auto resolvePath = [&configDir](const QString& path) {
        if (path.isEmpty()) {
            return path;
        }

        const QFileInfo fileInfo(path);
        if (fileInfo.isAbsolute()) {
            return QDir::cleanPath(fileInfo.absoluteFilePath());
        }

        return QDir::cleanPath(configDir.absoluteFilePath(path));
    };

    AppConfig cfg;

    cfg.dataDir = resolvePath(settings.value("Paths/data_dir", "./data").toString());
    cfg.dbPath = resolvePath(settings.value("Paths/db_path", "./taxi_data.db").toString());
    cfg.mapPath = resolvePath(settings.value("Paths/map_path", "./map.html").toString());

    cfg.minLon = settings.value("Filter/min_lon", 115.0).toDouble();
    cfg.maxLon = settings.value("Filter/max_lon", 118.0).toDouble();
    cfg.minLat = settings.value("Filter/min_lat", 39.0).toDouble();
    cfg.maxLat = settings.value("Filter/max_lat", 41.0).toDouble();

    cfg.batchSize = settings.value("Import/batch_size", 500).toInt();

    cfg.mapCenterLon = settings.value("Map/center_lon", 116.404).toDouble();
    cfg.mapCenterLat = settings.value("Map/center_lat", 39.915).toDouble();
    cfg.mapInitialZoom = settings.value("Map/initial_zoom", 12).toInt();
    cfg.mapMinZoom = settings.value("Map/min_zoom", 8).toInt();
    cfg.mapMaxZoom = settings.value("Map/max_zoom", 18).toInt();

    cfg.rectCapacity=settings.value("QuadTree/rect_capacity", 500).toInt();
    cfg.maxQuadTreeDepth = settings.value("QuadTree/max_depth", 64).toInt();
    cfg.minQuadCellSize = settings.value("QuadTree/min_cell_size", 1e-7).toDouble();
    return cfg;
}
AppConfig AppConfigManager::config;

void AppConfigManager::init(const QString& configPath) {
    config = AppConfig::load(configPath);
}

const AppConfig& AppConfigManager::get() {
    return config;
}

#include "appconfig.h"
#include <QSettings>

AppConfig AppConfig::load(const QString& configPath) {
    QSettings settings(configPath, QSettings::IniFormat);

    AppConfig cfg;

    cfg.dataDir = settings.value("Paths/data_dir", "./data").toString();
    cfg.dbPath = settings.value("Paths/db_path", "./taxi_data.db").toString();
    cfg.mapPath=settings.value("Paths/map_path", "./map.html").toString();

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

    cfg.rectCapacity=settings.value("Data/rect_capacity", 500).toInt();
    return cfg;
}
AppConfig AppConfigManager::config;

void AppConfigManager::init(const QString& configPath) {
    config = AppConfig::load(configPath);
}

const AppConfig& AppConfigManager::get() {
    return config;
}
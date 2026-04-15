#include "appconfig.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace {
QString resolvePathFromConfig(const QString& rawPath, const QString& configPath) {
    if (rawPath.isEmpty()) {
        return rawPath;
    }

    QFileInfo rawInfo(rawPath);
    if (rawInfo.isAbsolute()) {
        return rawInfo.absoluteFilePath();
    }

    const QFileInfo configInfo(configPath);
    const QDir configDir = configInfo.dir();
    const QString fromConfigDir = configDir.absoluteFilePath(rawPath);
    if (QFileInfo::exists(fromConfigDir) || !QFileInfo(rawPath).suffix().isEmpty()) {
        return QDir::cleanPath(fromConfigDir);
    }

    return QDir::cleanPath(fromConfigDir);
}
}

AppConfig AppConfig::load(const QString& configPath) {
    QSettings settings(configPath, QSettings::IniFormat);

    AppConfig cfg;

    cfg.dataDir = resolvePathFromConfig(settings.value("Paths/data_dir", "./data").toString(), configPath);
    cfg.dbPath = resolvePathFromConfig(settings.value("Paths/db_path", "./taxi_data.db").toString(), configPath);
    cfg.mapPath = resolvePathFromConfig(settings.value("Paths/map_path", "./map.html").toString(), configPath);

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

    return cfg;
}

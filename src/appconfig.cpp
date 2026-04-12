#include "appconfig.h"
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QDebug>

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

    const QString fallbackDataDir = QDir::cleanPath(configDir.absoluteFilePath("./data"));
    if (!QDir(cfg.dataDir).exists() && QDir(fallbackDataDir).exists()) {
        cfg.dataDir = fallbackDataDir;
    }

    // 注意：
    // db_path 为团队联调的显式配置，不再自动回退到其他数据库文件。
    // 之前的静默回退会导致“用户已替换数据库，但程序仍读取旧库”的问题，
    // 这里改为仅告警，不修改 cfg.dbPath，避免路径行为不可预期。
    const QFileInfo configuredDbInfo(cfg.dbPath);
    if (!configuredDbInfo.exists()) {
        qWarning().noquote() << QString("Configured db_path does not exist: %1").arg(cfg.dbPath);
    } else if (configuredDbInfo.size() <= 8192) {
        qWarning().noquote() << QString("Configured db_path looks too small (<= 8KB): %1").arg(cfg.dbPath);
    }

    cfg.minLon = settings.value("Filter/min_lon", 115.0).toDouble();
    cfg.maxLon = settings.value("Filter/max_lon", 118.0).toDouble();
    cfg.minLat = settings.value("Filter/min_lat", 39.0).toDouble();
    cfg.maxLat = settings.value("Filter/max_lat", 41.0).toDouble();

    cfg.batchSize = settings.value("Import/batch_size", 500).toInt();
    const uint portValue = settings.value("Server/port", 8080).toUInt();
    cfg.serverPort = static_cast<quint16>(portValue >= 1 && portValue <= 65535 ? portValue : 8080);

    cfg.mapCenterLon = settings.value("Map/center_lon", 116.404).toDouble();
    cfg.mapCenterLat = settings.value("Map/center_lat", 39.915).toDouble();
    cfg.mapInitialZoom = settings.value("Map/initial_zoom", 12).toInt();
    cfg.mapMinZoom = settings.value("Map/min_zoom", 8).toInt();
    cfg.mapMaxZoom = settings.value("Map/max_zoom", 18).toInt();
    cfg.baiduMapAk = settings.value(
        "Map/baidu_ak",
        "hf0NAP9ccSVWWMCH0gb0jrZqM0kfwclr"
    ).toString();

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

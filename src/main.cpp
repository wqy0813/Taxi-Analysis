#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>

#include "appconfig.h"
#include "databasemanager.h"
#include "datamanager.h"
#include "httpserver.h"

QString findConfigPath()
{
    // 配置文件查找优先级（高 -> 低）：
    // 1) 可执行文件上级目录（通常是项目根目录）；
    // 2) 可执行文件同级目录；
    // 3) 当前工作目录。
    // 这样可减少“构建目录里残留旧 config.ini 覆盖项目配置”的问题。
    const QStringList candidates = {
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../config.ini"),
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../../config.ini"),
        QCoreApplication::applicationDirPath() + "/config.ini",
        QDir::current().absoluteFilePath("config.ini")
    };

    for (const QString& path : candidates) {
        if (QFileInfo::exists(path)) {
            return QDir::cleanPath(path);
        }
    }

    return QDir::cleanPath(QDir::current().absoluteFilePath("config.ini"));
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    // 支持命令行显式传入配置路径：
    // TaxiAnalysisSystem.exe C:/xxx/config.ini
    // 未传参时按 findConfigPath() 自动查找。
    QString configPath;
    if (argc > 1) {
        configPath = QDir::cleanPath(QString::fromLocal8Bit(argv[1]));
    }
    if (configPath.isEmpty() || !QFileInfo::exists(configPath)) {
        configPath = findConfigPath();
    }

    AppConfigManager::init(configPath);
    const AppConfig& config = AppConfigManager::get();

    qDebug() << "Config path:" << configPath;
    qDebug() << "Database path:" << config.dbPath;

    DatabaseManager dbm(config.dbPath);
    if (!dbm.open()) {
        qCritical() << "Failed to open database.";
        return -1;
    }

    DatabaseManager::checkAndImportData(dbm, config);

    if (!DataManager::loadFromDatabase(dbm)) {
        qCritical() << "Failed to load points from database.";
        return -1;
    }

    qDebug() << "Loaded point count:" << DataManager::getAllPoints().size();

    DataManager::buildQuadTree(config);
    qInfo().noquote() << QString("Data loaded. Open the browser at: http://127.0.0.1:%1/").arg(config.serverPort);

    HttpServer server(config);
    if (!server.start(config.serverPort)) {
        qCritical() << "Failed to start HTTP server.";
        return -1;
    }

    qDebug() << "HTTP server listening at http://127.0.0.1:" << config.serverPort;
    return 0;
}

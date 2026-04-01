#include <QApplication>
#include <QDebug>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QCoreApplication>
#include "appconfig.h"
#include "databasemanager.h"
#include "TrafficAnalysisSystem.h"

QString findConfigPath() {
    const QStringList candidates = {
        QDir::current().absoluteFilePath("config.ini"),
        QCoreApplication::applicationDirPath() + "/config.ini",
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../config.ini"),
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../../config.ini")
    };

    for (const QString& path : candidates) {
        if (QFileInfo::exists(path)) {
            return QDir::cleanPath(path);
        }
    }

    return QDir::cleanPath(QDir::current().absoluteFilePath("config.ini"));
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    QString configPath = QDir::currentPath() + "/config.ini";
    if (!QFileInfo::exists(configPath)) {
        qDebug() << "未找到配置文件，将使用默认配置";
        configPath = findConfigPath();
    }
    AppConfigManager::init(configPath);
    const AppConfig& config = AppConfigManager::get();
    qDebug() << "四叉树节点容量限制:" << config.rectCapacity;
    qDebug() << "四叉树最大节点深度:" << config.maxQuadTreeDepth;
    qDebug() << "四叉树最小节点边长:" << config.minQuadCellSize;
    qDebug() << "配置文件路径:" << configPath;
    qDebug() << "数据目录:" << config.dataDir;
    qDebug() << "数据库路径:" << config.dbPath;

    DatabaseManager dbm(config.dbPath);
    if (!dbm.open()) {
        qDebug() << "数据库连接失败！请检查配置和 SQLite 驱动。";
        return -1;
    }

    DatabaseManager::checkAndImportData(dbm, config);

    if (!DataManager::loadFromDatabase(dbm)) {
        qDebug() << "从数据库加载点到内存失败";
        return -1;
    }

    qDebug() << "加载到内存的点数量:" << DataManager::getAllPoints().size();

    DataManager::buildQuadTree(config);
    TrafficAnalysisSystem window(&dbm);
    window.show();

    return app.exec();
}
#include <QApplication>
#include <QDebug>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QDateTime>
#include <QCoreApplication>

#include "appconfig.h"
#include "databasemanager.h"
#include "TrafficAnalysisSystem.h"

namespace {

}

void checkAndImportData(DatabaseManager &dbm, const AppConfig& config) {
    if ( dbm.getPointCount() > 0) {
        qDebug() << "检测到数据库已有数据，跳过导入。当前点数:" << dbm.getPointCount();
        return;
    }

    qDebug() << "--- 数据库为空，开始导入数据 ---";
    QElapsedTimer timer;
    timer.start();

    QDirIterator it(config.dataDir,
                    QStringList() << "*.txt",
                    QDir::Files,
                    QDirIterator::Subdirectories);

    std::vector<GPSPoint> buffer;
    int fileCount = 0;
    qint64 totalPoints = 0;

    while (it.hasNext()) {
        QString filePath = it.next();
        QFile file(filePath);

        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&file);

            while (!in.atEnd()) {
                QString line = in.readLine();
                QStringList parts = line.split(',');

                if (parts.size() < 4) {
                    continue;
                }

                GPSPoint p;
                p.id = parts[0].toInt();
                const QDateTime timestamp = QDateTime::fromString(parts[1], "yyyy-MM-dd HH:mm:ss");
                if (!timestamp.isValid()) {
                    continue;
                }
                p.timestamp = timestamp.toSecsSinceEpoch();
                p.lon = parts[2].toDouble();
                p.lat = parts[3].toDouble();

                if (p.lon > config.minLon && p.lon < config.maxLon &&
                    p.lat > config.minLat && p.lat < config.maxLat) {
                    buffer.push_back(p);
                }
            }

            file.close();
        } else {
            qDebug() << "文件打开失败:" << filePath;
        }

        ++fileCount;

        if (fileCount % config.batchSize == 0) {
            if (!dbm.batchInsert(buffer)) {
                qDebug() << "批量插入失败，程序中止导入";
                return;
            }

            totalPoints += static_cast<qint64>(buffer.size());
            qDebug() << "进度:" << fileCount << "个文件 | 已存入:" << totalPoints << "个点";
            buffer.clear();
        }
    }

    if (!buffer.empty()) {
        if (!dbm.batchInsert(buffer)) {
            qDebug() << "最后一批插入失败";
            return;
        }

        totalPoints += static_cast<qint64>(buffer.size());
        buffer.clear();
    }

    qDebug() << "导入完成！总点数:" << totalPoints
             << "耗时:" << timer.elapsed() / 1000.0 << "秒";
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    QString configPath = findConfigPath();
    AppConfig config = AppConfig::load(configPath);

    qDebug() << "配置文件路径:" << configPath;
    qDebug() << "数据目录:" << config.dataDir;
    qDebug() << "数据库路径:" << config.dbPath;

    DatabaseManager dbm(config.dbPath);
    if (!dbm.open()) {
        qDebug() << "数据库连接失败！请检查配置和 SQLite 驱动。";
        return -1;
    }

    checkAndImportData(dbm, config);

    TrafficAnalysisSystem window(&dbm);
    window.show();

    return app.exec();
}

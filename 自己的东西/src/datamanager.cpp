#include "datamanager.h"
#include <QDirIterator>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QDebug>

DataManager::DataManager() {}

void DataManager::loadTxtFiles(const AppConfig& config) {
    allPoints.clear();

    qDebug() << "正在扫描文件夹:" << config.dataDir << "(文件较多，请稍候...)";
    qDebug() << "开始解析文件内容...";
    qDebug() << "过滤范围:"
             << "lon(" << config.minLon << "," << config.maxLon << "),"
             << "lat(" << config.minLat << "," << config.maxLat << ")";

    QDirIterator it(config.dataDir,
                    QStringList() << "*.txt",
                    QDir::Files,
                    QDirIterator::Subdirectories);

    int fileCount = 0;
    int validCount = 0;
    int invalidTimeCount = 0;
    int invalidLineCount = 0;

    while (it.hasNext()) {
        QFile file(it.next());

        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&file);

            while (!in.atEnd()) {
                QString line = in.readLine().trimmed();
                if (line.isEmpty()) {
                    continue;
                }

                QStringList parts = line.split(',');
                if (parts.size() < 4) {
                    ++invalidLineCount;
                    continue;
                }

                GPSPoint p;
                p.id = parts[0].toInt();

                QDateTime dt = QDateTime::fromString(parts[1], "yyyy-MM-dd HH:mm:ss");
                if (!dt.isValid()) {
                    ++invalidTimeCount;
                    continue;
                }

                p.timestamp = dt.toSecsSinceEpoch();
                p.lon = parts[2].toDouble();
                p.lat = parts[3].toDouble();

                if (p.lon > config.minLon && p.lon < config.maxLon &&
                    p.lat > config.minLat && p.lat < config.maxLat) {
                    allPoints.push_back(p);
                    ++validCount;
                }
            }

            file.close();
        }

        ++fileCount;
        if (fileCount % config.batchSize == 0) {
            qDebug() << "已读取" << fileCount << "个文件..."
                     << "当前有效点数:" << validCount;
        }
    }

    qDebug() << "读取完成。";
    qDebug() << "总文件数:" << fileCount;
    qDebug() << "有效点数:" << allPoints.size();
    qDebug() << "无效行数:" << invalidLineCount;
    qDebug() << "无效时间数:" << invalidTimeCount;
}
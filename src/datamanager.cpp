#include "datamanager.h"
#include "databasemanager.h"

#include <QDirIterator>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QDebug>
#include <unordered_set>
std::vector<GPSPoint> DataManager::allPoints;
std::unique_ptr<QuadNode> DataManager::quadTreeRoot = nullptr;
std::set<const QuadNode*> DataManager::exceptionalNodes;
bool DataManager::loadFromDatabase(DatabaseManager& dbm) {
    allPoints.clear();
    exceptionalNodes.clear();
    quadTreeRoot.reset();   // 数据重载时，旧树作废
    return dbm.loadAllPoints(allPoints);
}

void DataManager::loadTxtFiles(const AppConfig& config) {
    allPoints.clear();
    quadTreeRoot.reset();   // 数据重载时，旧树作废
    exceptionalNodes.clear();
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

void DataManager::buildQuadTree(const AppConfig& config) {
    quadTreeRoot.reset();
    exceptionalNodes.clear();
    int capacity=config.rectCapacity;
    if (allPoints.empty()) {
        qDebug() << "buildQuadTree: allPoints 为空，无法建立四叉树";
        return;
    }

    Rect rootRect;
    rootRect.x = (config.minLon + config.maxLon) / 2.0;
    rootRect.y = (config.minLat + config.maxLat) / 2.0;
    rootRect.w = (config.maxLon - config.minLon) / 2.0;
    rootRect.h = (config.maxLat - config.minLat) / 2.0;

    quadTreeRoot = std::make_unique<QuadNode>(
        rootRect,
        capacity,
        0,
        config.maxQuadTreeDepth,
        config.minQuadCellSize
        );

    qDebug() << "开始建立四叉树，总点数:" << allPoints.size()
             << "节点容量:" << capacity;

    int insertedCount = 0;
    int failedCount = 0;

    for (int i = 0; i < static_cast<int>(allPoints.size()); ++i) {
        if (quadTreeRoot->insert(i, allPoints)) {
            ++insertedCount;
        } else {
            ++failedCount;
        }

        if ((i + 1) % 1000000 == 0) {
            qDebug() << "四叉树插入进度:" << (i + 1)
                     << "/" << allPoints.size();
        }
    }

    std::unordered_set<int> indexedDepths = {1,2, 3, 5};
    quadTreeRoot->buildSortedIndexForDepths(indexedDepths, allPoints);

    qDebug() << "四叉树建立完成，成功插入:" << insertedCount
             << "失败:" << failedCount;
    qDebug() << "异常节点数量:" << exceptionalNodes.size();
    for (const QuadNode* node : exceptionalNodes) {
        qDebug() << qSetRealNumberPrecision(15) <<"异常节点经纬度范围："<< node->boundary.x-node->boundary.w
            <<"-"
            << node->boundary.x+node->boundary.w
            <<","
            << node->boundary.y-node->boundary.h
            <<"-"
            << node->boundary.y+node->boundary.h
            ;
        qDebug()<<"异常节点内部点:"<<node->points.size();
        qDebug()<<"异常节点深度:"<<node->depth;
    }
}

bool DataManager::hasQuadTree() {
    return quadTreeRoot != nullptr;
}

std::vector<GPSPoint> DataManager::querySpatial(double minLon, double minLat,
                                              double maxLon, double maxLat) {
    std::vector<GPSPoint> result;

    if (!quadTreeRoot) {
        qDebug() << "queryRange: 四叉树尚未建立";
        return result;
    }

    Rect range;
    range.x = (minLon + maxLon) / 2.0;
    range.y = (minLat + maxLat) / 2.0;
    range.w = (maxLon - minLon) / 2.0;
    range.h = (maxLat - minLat) / 2.0;

    std::vector<int> foundIndexes;
    quadTreeRoot->querySpatial(range, foundIndexes, allPoints);

    result.reserve(foundIndexes.size());
    for (int idx : foundIndexes) {
        if (idx >= 0 && idx < static_cast<int>(allPoints.size())) {
            result.push_back(allPoints[idx]);
        }
    }

    qDebug() << "queryRange: 命中点数 =" << result.size();
    return result;
}
std::vector<GPSPoint> DataManager::querySpatialAndTime(double minLon, double minLat,
                                                       double maxLon, double maxLat,long long minTimeStamp,long long maxTimeStamp){

    std::vector<GPSPoint> result;

    if (!quadTreeRoot) {
        qDebug() << "queryRange: 四叉树尚未建立";
        return result;
    }

    Rect range;
    range.x = (minLon + maxLon) / 2.0;
    range.y = (minLat + maxLat) / 2.0;
    range.w = (maxLon - minLon) / 2.0;
    range.h = (maxLat - minLat) / 2.0;

    std::vector<int> foundIndexes;
    quadTreeRoot->querySpatioTemporal(range,minTimeStamp,maxTimeStamp, foundIndexes, allPoints);

    result.reserve(foundIndexes.size());
    for (int idx : foundIndexes) {
        if (idx >= 0 && idx < static_cast<int>(allPoints.size())) {
            result.push_back(allPoints[idx]);
        }
    }

    qDebug() << "queryRange: 命中点数 =" << result.size();
    return result;
}
std::unordered_set<int> DataManager::querySpatioTemporalUniqueIds(double minLon, double minLat,
                                                       double maxLon, double maxLat,long long minTimeStamp,long long maxTimeStamp){
    std::unordered_set<int>  foundIndexes;
    if (!quadTreeRoot) {
        qDebug() << "queryRange: 四叉树尚未建立";
        return foundIndexes;
    }

    Rect range;
    range.x = (minLon + maxLon) / 2.0;
    range.y = (minLat + maxLat) / 2.0;
    range.w = (maxLon - minLon) / 2.0;
    range.h = (maxLat - minLat) / 2.0;


    quadTreeRoot->querySpatioTemporalUniqueIds(range,minTimeStamp,maxTimeStamp, foundIndexes, allPoints);
    return foundIndexes;
}
int DataManager::getUniqueCountById(const std::vector<GPSPoint>& points)
{
    int maxId=11000;
    std::vector<bool> seen(maxId + 1, false);
    int count = 0;

    for (const auto& p : points) {
        if (!seen[p.id]) {
            seen[p.id] = true;
            ++count;
        }
    }

    return count;
}
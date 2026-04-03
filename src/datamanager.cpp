#include "datamanager.h"
#include "databasemanager.h"

#include <QDateTime>
#include <QDebug>
#include <QDirIterator>
#include <QFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

std::vector<GPSPoint> DataManager::allPoints;
std::unordered_map<int, VehicleRange> DataManager::idToRange;
std::unique_ptr<QuadNode> DataManager::quadTreeRoot = nullptr;
std::set<const QuadNode*> DataManager::exceptionalNodes;

namespace {

struct GridKey {
    int x;
    int y;

    bool operator==(const GridKey& other) const {
        return x == other.x && y == other.y;
    }
};

struct GridKeyHash {
    std::size_t operator()(const GridKey& key) const {
        std::size_t h1 = std::hash<int>()(key.x);
        std::size_t h2 = std::hash<int>()(key.y);
        return h1 ^ (h2 << 1);
    }
};

struct ClusterBucket {
    double sumLon = 0.0;
    double sumLat = 0.0;
    int count = 0;

    double minLon = std::numeric_limits<double>::max();
    double minLat = std::numeric_limits<double>::max();
    double maxLon = std::numeric_limits<double>::lowest();
    double maxLat = std::numeric_limits<double>::lowest();

    std::vector<GPSPoint> children;
};

double baseGridSizeByZoom(int zoom) {
    if (zoom <= 8)  return 0.100;
    if (zoom <= 10) return 0.050;
    if (zoom <= 12) return 0.025;
    if (zoom <= 13) return 0.015;
    if (zoom <= 14) return 0.008;
    if (zoom <= 15) return 0.004;
    if (zoom <= 16) return 0.002;
    return 0.001;
}

std::vector<ClusterPoint> buildClustersWithGrid(const std::vector<GPSPoint>& points,
                                                double minLon, double minLat,
                                                double maxLon, double maxLat,
                                                double gridSize)
{
    std::vector<ClusterPoint> result;
    if (points.empty()) {
        return result;
    }

    std::unordered_map<GridKey, ClusterBucket, GridKeyHash> buckets;
    buckets.reserve(points.size());

    for (const auto& p : points) {
        if (p.lon < minLon || p.lon > maxLon || p.lat < minLat || p.lat > maxLat) {
            continue;
        }

        int gx = static_cast<int>(std::floor((p.lon - minLon) / gridSize));
        int gy = static_cast<int>(std::floor((p.lat - minLat) / gridSize));

        GridKey key{gx, gy};
        auto& bucket = buckets[key];

        bucket.sumLon += p.lon;
        bucket.sumLat += p.lat;
        bucket.count++;

        bucket.minLon = std::min(bucket.minLon, p.lon);
        bucket.minLat = std::min(bucket.minLat, p.lat);
        bucket.maxLon = std::max(bucket.maxLon, p.lon);
        bucket.maxLat = std::max(bucket.maxLat, p.lat);

        if (bucket.count <= 100) {
            bucket.children.push_back(p);
        }
    }

    result.reserve(buckets.size());

    for (auto& [key, bucket] : buckets) {
        ClusterPoint cp{};
        cp.count = bucket.count;
        cp.isCluster = bucket.count > 1;

        if (bucket.count == 1 && !bucket.children.empty()) {
            cp.lon = bucket.children[0].lon;
            cp.lat = bucket.children[0].lat;
        } else {
            cp.lon = bucket.sumLon / static_cast<double>(bucket.count);
            cp.lat = bucket.sumLat / static_cast<double>(bucket.count);
        }

        cp.minLon = bucket.minLon;
        cp.minLat = bucket.minLat;
        cp.maxLon = bucket.maxLon;
        cp.maxLat = bucket.maxLat;

        if (bucket.count <= 100) {
            cp.children = std::move(bucket.children);
        }

        result.push_back(std::move(cp));
    }

    std::sort(result.begin(), result.end(),
              [](const ClusterPoint& a, const ClusterPoint& b) {
                  if (a.isCluster != b.isCluster) {
                      return a.isCluster > b.isCluster;
                  }
                  return a.count > b.count;
              });

    return result;
}

} // namespace

bool DataManager::loadAllPoints(DatabaseManager& dbm) {
    allPoints.clear();
    idToRange.clear();

    QSqlDatabase db = dbm.getQSqlDatabase();
    if (!db.isOpen()) {
        qDebug() << "数据库未打开，无法加载点数据";
        return false;
    }

    qint64 totalCount = dbm.getPointCount();
    if (totalCount <= 0) {
        qDebug() << "数据库中没有点数据";
        return true;
    }

    allPoints.reserve(static_cast<size_t>(totalCount));

    QSqlQuery query(db);
    query.setForwardOnly(true);

    const QString sql = R"(
        SELECT id, time, lon, lat
        FROM taxi_points
        ORDER BY id ASC, time ASC
    )";

    if (!query.exec(sql)) {
        qDebug() << "读取点数据失败:" << query.lastError().text();
        return false;
    }

    qint64 loadedCount = 0;

    int currentId = -1;
    int rangeStart = -1;

    while (query.next()) {
        GPSPoint p;
        p.id = query.value(0).toInt();
        p.timestamp = query.value(1).toLongLong();
        p.lon = query.value(2).toDouble();
        p.lat = query.value(3).toDouble();

        int currentIndex = static_cast<int>(allPoints.size());

        if (currentId != -1 && p.id != currentId) {
            idToRange[currentId] = {rangeStart, currentIndex - 1};
            rangeStart = currentIndex;
        }

        if (currentId == -1) {
            currentId = p.id;
            rangeStart = currentIndex;
        } else if (p.id != currentId) {
            currentId = p.id;
        }

        allPoints.push_back(p);
        ++loadedCount;

        if (loadedCount % 1000000 == 0) {
            qDebug() << "已加载" << loadedCount << "个点到内存...";
        }
    }

    if (!allPoints.empty() && currentId != -1) {
        idToRange[currentId] = {rangeStart, static_cast<int>(allPoints.size()) - 1};
    }

    qDebug() << "全部点加载完成，共" << loadedCount << "个点";
    qDebug() << "当前 allPoints 已按 id 递增、time 递增排列";
    qDebug() << "共建立" << idToRange.size() << "个车辆区间映射";

    return true;
}

bool DataManager::loadFromDatabase(DatabaseManager& dbm) {
    allPoints.clear();
    exceptionalNodes.clear();
    quadTreeRoot.reset();
    return loadAllPoints(dbm);
}

void DataManager::loadTxtFiles(const AppConfig& config) {
    allPoints.clear();
    quadTreeRoot.reset();
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

    int capacity = config.rectCapacity;
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

    std::unordered_set<int> indexedDepths = {1, 2, 3, 5};
    quadTreeRoot->buildSortedIndexForDepths(indexedDepths, allPoints);

    qDebug() << "四叉树建立完成，成功插入:" << insertedCount
             << "失败:" << failedCount;
    qDebug() << "异常节点数量:" << exceptionalNodes.size();

    for (const QuadNode* node : exceptionalNodes) {
        qDebug() << qSetRealNumberPrecision(15)
        << "异常节点经纬度范围："
        << node->boundary.x - node->boundary.w
        << "-"
        << node->boundary.x + node->boundary.w
        << ","
        << node->boundary.y - node->boundary.h
        << "-"
        << node->boundary.y + node->boundary.h;
        qDebug() << "异常节点内部点:" << node->points.size();
        qDebug() << "异常节点深度:" << node->depth;
    }
}

bool DataManager::hasQuadTree() {
    return quadTreeRoot != nullptr;
}

std::vector<GPSPoint> DataManager::getPointsRangeById(int id) {
    std::vector<GPSPoint> result;

    auto it = idToRange.find(id);
    if (it == idToRange.end()) {
        return result;
    }

    const VehicleRange& range = it->second;
    result.reserve(range.end - range.start + 1);

    for (int i = range.start; i <= range.end; ++i) {
        result.push_back(allPoints[i]);
    }

    return result;
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
                                                       double maxLon, double maxLat,
                                                       long long minTimeStamp, long long maxTimeStamp) {
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
    quadTreeRoot->querySpatioTemporal(range, minTimeStamp, maxTimeStamp, foundIndexes, allPoints);

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
                                                                  double maxLon, double maxLat,
                                                                  long long minTimeStamp, long long maxTimeStamp) {
    std::unordered_set<int> foundIndexes;

    if (!quadTreeRoot) {
        qDebug() << "queryRange: 四叉树尚未建立";
        return foundIndexes;
    }

    Rect range;
    range.x = (minLon + maxLon) / 2.0;
    range.y = (minLat + maxLat) / 2.0;
    range.w = (maxLon - minLon) / 2.0;
    range.h = (maxLat - minLat) / 2.0;

    quadTreeRoot->querySpatioTemporalUniqueIds(range, minTimeStamp, maxTimeStamp, foundIndexes, allPoints);
    return foundIndexes;
}

std::vector<ClusterPoint> DataManager::clusterPointsForView(const std::vector<GPSPoint>& points,
                                                            double minLon, double minLat,
                                                            double maxLon, double maxLat,
                                                            int zoom) {
    std::vector<ClusterPoint> result;
    if (points.empty()) {
        return result;
    }

    const double safeMinLon = std::min(minLon, maxLon);
    const double safeMaxLon = std::max(minLon, maxLon);
    const double safeMinLat = std::min(minLat, maxLat);
    const double safeMaxLat = std::max(minLat, maxLat);

    // 高 zoom 且点很少时，直接显示原始点
    if (zoom >= 17 && points.size() <= 100) {
        result.reserve(points.size());
        for (const auto& p : points) {
            ClusterPoint cp{};
            cp.lon = p.lon;
            cp.lat = p.lat;
            cp.count = 1;
            cp.isCluster = false;
            cp.minLon = p.lon;
            cp.minLat = p.lat;
            cp.maxLon = p.lon;
            cp.maxLat = p.lat;
            cp.children.push_back(p);
            result.push_back(std::move(cp));
        }
        return result;
    }

    double gridSize = baseGridSizeByZoom(zoom);

    // 控制地图上一屏不要出现太多聚类
    const int targetMaxClusters = 100;

    for (int attempt = 0; attempt < 8; ++attempt) {
        result = buildClustersWithGrid(points,
                                       safeMinLon, safeMinLat,
                                       safeMaxLon, safeMaxLat,
                                       gridSize);

        if (static_cast<int>(result.size()) <= targetMaxClusters) {
            break;
        }

        gridSize *= 1.6;
    }

    qDebug() << "clusterPointsForView: zoom =" << zoom
             << ", 原始点数 =" << points.size()
             << ", 聚合后对象数 =" << result.size();

    return result;
}

int DataManager::getUniqueCountById(const std::vector<GPSPoint>& points)
{
    int maxId = 11000;
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
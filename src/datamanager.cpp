#include "datamanager.h"
#include "databasemanager.h"

#include <sqlite3.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_set>
#include "logger.h"
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
        const std::size_t h1 = std::hash<int>()(key.x);
        const std::size_t h2 = std::hash<int>()(key.y);
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
                                                double gridSize,
                                                int zoom)
{
    std::vector<ClusterPoint> result;
    if (points.empty()) {
        return result;
    }

    const int childTransferThreshold = 100;
    const int childTransferZoom = 15;

    std::unordered_map<GridKey, ClusterBucket, GridKeyHash> buckets;
    buckets.reserve(points.size());

    for (const auto& p : points) {
        if (p.lon < minLon || p.lon > maxLon || p.lat < minLat || p.lat > maxLat) {
            continue;
        }

        const int gx = static_cast<int>(std::floor((p.lon - minLon) / gridSize));
        const int gy = static_cast<int>(std::floor((p.lat - minLat) / gridSize));

        GridKey key{gx, gy};
        auto& bucket = buckets[key];

        bucket.sumLon += p.lon;
        bucket.sumLat += p.lat;
        bucket.count++;

        bucket.minLon = std::min(bucket.minLon, p.lon);
        bucket.minLat = std::min(bucket.minLat, p.lat);
        bucket.maxLon = std::max(bucket.maxLon, p.lon);
        bucket.maxLat = std::max(bucket.maxLat, p.lat);

        if (zoom >= childTransferZoom && bucket.count <= childTransferThreshold) {
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

        if (zoom >= childTransferZoom && bucket.count <= childTransferThreshold) {
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

std::string trim(const std::string& input) {
    std::size_t left = 0;
    while (left < input.size() && std::isspace(static_cast<unsigned char>(input[left]))) {
        ++left;
    }

    std::size_t right = input.size();
    while (right > left && std::isspace(static_cast<unsigned char>(input[right - 1]))) {
        --right;
    }

    return input.substr(left, right - left);
}

long long parseTimestamp(const std::string& text) {
    std::tm tm{};
    std::istringstream iss(text);
    iss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    if (iss.fail()) {
        return 0;
    }

#if defined(_WIN32)
    return static_cast<long long>(_mkgmtime(&tm));
#else
    return static_cast<long long>(timegm(&tm));
#endif
}

} // namespace

bool DataManager::loadAllPoints(DatabaseManager& dbm) {
    allPoints.clear();
    idToRange.clear();

    sqlite3* db = dbm.getRawHandle();
    if (db == nullptr) {
        Debug() << "数据库未打开，无法加载点数据" ;
        return false;
    }

    const std::int64_t totalCount = dbm.getPointCount();
    if (totalCount <= 0) {
        Debug() << "数据库中没有点数据" ;
        return true;
    }

    allPoints.reserve(static_cast<std::size_t>(totalCount));

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, time, lon, lat FROM taxi_points ORDER BY id ASC, time ASC;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        Debug() << "读取点数据失败: " << sqlite3_errmsg(db) ;
        return false;
    }

    std::int64_t loadedCount = 0;
    int currentId = -1;
    int rangeStart = -1;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        GPSPoint p{};
        p.id = sqlite3_column_int(stmt, 0);
        p.timestamp = static_cast<long long>(sqlite3_column_int64(stmt, 1));
        p.lon = sqlite3_column_double(stmt, 2);
        p.lat = sqlite3_column_double(stmt, 3);

        const int currentIndex = static_cast<int>(allPoints.size());

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
            Debug() << "已加载 " << loadedCount << " 个点到内存..." ;
        }
    }

    sqlite3_finalize(stmt);

    if (!allPoints.empty() && currentId != -1) {
        idToRange[currentId] = {rangeStart, static_cast<int>(allPoints.size()) - 1};
    }

    Debug() << "全部点加载完成，共 " << loadedCount << " 个点" ;
    Debug() << "当前 allPoints 已按 id 递增、time 递增排列" ;
    Debug() << "共建立 " << idToRange.size() << " 个车辆区间映射" ;

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

    Debug() << "正在扫描文件夹: " << config.dataDir << " (文件较多，请稍候...)" ;
    Debug() << "开始解析文件内容..." ;
    Debug() << "过滤范围: "
              << "lon(" << config.minLon << "," << config.maxLon << "), "
              << "lat(" << config.minLat << "," << config.maxLat << ")" ;

    int fileCount = 0;
    int validCount = 0;
    int invalidTimeCount = 0;
    int invalidLineCount = 0;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(config.dataDir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".txt") {
            continue;
        }

        std::ifstream fin(entry.path());
        if (!fin.is_open()) {
            continue;
        }

        std::string line;
        while (std::getline(fin, line)) {
            line = trim(line);
            if (line.empty()) {
                continue;
            }

            std::stringstream ss(line);
            std::string idStr;
            std::string timeStr;
            std::string lonStr;
            std::string latStr;

            if (!std::getline(ss, idStr, ',')) {
                ++invalidLineCount;
                continue;
            }
            if (!std::getline(ss, timeStr, ',')) {
                ++invalidLineCount;
                continue;
            }
            if (!std::getline(ss, lonStr, ',')) {
                ++invalidLineCount;
                continue;
            }
            if (!std::getline(ss, latStr, ',')) {
                ++invalidLineCount;
                continue;
            }

            GPSPoint p{};
            try {
                p.id = std::stoi(idStr);
                p.timestamp = parseTimestamp(timeStr);
                p.lon = std::stod(lonStr);
                p.lat = std::stod(latStr);
            } catch (...) {
                ++invalidLineCount;
                continue;
            }

            if (p.timestamp == 0) {
                ++invalidTimeCount;
                continue;
            }

            if (p.lon > config.minLon && p.lon < config.maxLon &&
                p.lat > config.minLat && p.lat < config.maxLat) {
                allPoints.push_back(p);
                ++validCount;
            }
        }

        ++fileCount;
        if (fileCount % config.batchSize == 0) {
            Debug() << "已读取 " << fileCount << " 个文件... 当前有效点数: " << validCount ;
        }
    }

    Debug() << "读取完成。" ;
    Debug() << "总文件数: " << fileCount ;
    Debug() << "有效点数: " << allPoints.size() ;
    Debug() << "无效行数: " << invalidLineCount ;
    Debug() << "无效时间数: " << invalidTimeCount ;
}

void DataManager::buildQuadTree(const AppConfig& config) {
    quadTreeRoot.reset();
    exceptionalNodes.clear();

    const int capacity = config.rectCapacity;
    if (allPoints.empty()) {
        Debug() << "buildQuadTree: allPoints 为空，无法建立四叉树" ;
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
        config.minQuadCellSize);

    Debug() << "开始建立四叉树，总点数: " << allPoints.size()
              << " 节点容量: " << capacity ;

    int insertedCount = 0;
    int failedCount = 0;

    for (int i = 0; i < static_cast<int>(allPoints.size()); ++i) {
        if (quadTreeRoot->insert(i, allPoints)) {
            ++insertedCount;
        } else {
            ++failedCount;
        }

        if ((i + 1) % 1000000 == 0) {
            Debug() << "四叉树插入进度: " << (i + 1)
                      << "/" << allPoints.size() ;
        }
    }

    const std::unordered_set<int> indexedDepths = {1, 2, 3, 5};
    quadTreeRoot->buildSortedIndexForDepths(indexedDepths, allPoints);

    Debug() << "四叉树建立完成，成功插入: " << insertedCount
              << " 失败: " << failedCount ;
    Debug() << "异常节点数量: " << exceptionalNodes.size() ;

    for (const QuadNode* node : exceptionalNodes) {
        Debug() << std::setprecision(15)
        << "异常节点经纬度范围："
        << node->boundary.x - node->boundary.w
        << "-"
        << node->boundary.x + node->boundary.w
        << ","
        << node->boundary.y - node->boundary.h
        << "-"
        << node->boundary.y + node->boundary.h
        ;
        Debug() << "异常节点内部点: " << node->points.size() ;
        Debug() << "异常节点深度: " << node->depth ;
    }
}

bool DataManager::hasQuadTree() {
    return quadTreeRoot != nullptr;
}

std::vector<GPSPoint> DataManager::getPointsRangeById(int id) {
    std::vector<GPSPoint> result;

    const auto it = idToRange.find(id);
    if (it == idToRange.end()) {
        return result;
    }

    const VehicleRange& range = it->second;
    result.reserve(static_cast<std::size_t>(range.end - range.start + 1));

    for (int i = range.start; i <= range.end; ++i) {
        result.push_back(allPoints[static_cast<std::size_t>(i)]);
    }

    return result;
}

std::vector<GPSPoint> DataManager::querySpatial(double minLon, double minLat,
                                                double maxLon, double maxLat) {
    std::vector<GPSPoint> result;

    if (!quadTreeRoot) {
        Debug() << "queryRange: 四叉树尚未建立" ;
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
    for (const int idx : foundIndexes) {
        if (idx >= 0 && idx < static_cast<int>(allPoints.size())) {
            result.push_back(allPoints[static_cast<std::size_t>(idx)]);
        }
    }

    Debug() << "queryRange: 命中点数 = " << result.size() ;
    return result;
}

std::vector<GPSPoint> DataManager::querySpatialAndTime(double minLon, double minLat,
                                                       double maxLon, double maxLat,
                                                       long long minTimeStamp, long long maxTimeStamp) {
    std::vector<GPSPoint> result;

    if (!quadTreeRoot) {
        Debug() << "queryRange: 四叉树尚未建立" ;
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
    for (const int idx : foundIndexes) {
        if (idx >= 0 && idx < static_cast<int>(allPoints.size())) {
            result.push_back(allPoints[static_cast<std::size_t>(idx)]);
        }
    }

    Debug() << "queryRange: 命中点数 = " << result.size() ;
    return result;
}

std::unordered_set<int> DataManager::querySpatioTemporalUniqueIds(double minLon, double minLat,
                                                                  double maxLon, double maxLat,
                                                                  long long minTimeStamp, long long maxTimeStamp) {
    std::unordered_set<int> foundIndexes;

    if (!quadTreeRoot) {
        Debug() << "queryRange: 四叉树尚未建立" ;
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

    double gridSize = baseGridSizeByZoom(zoom);
    const int targetMaxClusters = 225;

    for (int attempt = 0; attempt < 8; ++attempt) {
        result = buildClustersWithGrid(
            points, safeMinLon, safeMinLat, safeMaxLon, safeMaxLat, gridSize, zoom);

        if (static_cast<int>(result.size()) <= targetMaxClusters) {
            break;
        }

        gridSize *= 1.6;
    }

    Debug() << "clusterPointsForView: zoom = " << zoom
              << ", 原始点数 = " << points.size()
              << ", 聚合后对象数 = " << result.size() ;

    return result;
}

int DataManager::getUniqueCountById(const std::vector<GPSPoint>& points) {
    int maxId = 11000;
    for (const auto& p : points) {
        maxId = std::max(maxId, p.id);
    }

    std::vector<bool> seen(static_cast<std::size_t>(maxId + 1), false);
    int count = 0;

    for (const auto& p : points) {
        if (p.id < 0) {
            continue;
        }
        if (!seen[static_cast<std::size_t>(p.id)]) {
            seen[static_cast<std::size_t>(p.id)] = true;
            ++count;
        }
    }

    return count;
}

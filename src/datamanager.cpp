#include "datamanager.h"
#include "databasemanager.h"
#include <atomic>
#include <mutex>
#include <thread>
#include <sqlite3.h>
#include <chrono>
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
    bool readChunkByRowIdRange(const std::string& dbPath,
                           std::int64_t beginRowId,
                           std::int64_t endRowId,
                           std::vector<GPSPoint>& outPoints,
                           std::string& errorMessage,
                           long long& elapsedMs)
{
    outPoints.clear();
    elapsedMs = 0;

    if (beginRowId > endRowId) {
        return true;
    }

    auto start = std::chrono::steady_clock::now();

    sqlite3* db = nullptr;
    const int rc = sqlite3_open_v2(
        dbPath.c_str(),
        &db,
        SQLITE_OPEN_READONLY,
        nullptr);

    if (rc != SQLITE_OK || db == nullptr) {
        errorMessage = "线程打开数据库失败";
        if (db != nullptr) {
            errorMessage += ": ";
            errorMessage += sqlite3_errmsg(db);
            sqlite3_close(db);
        }
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT id, time, lon, lat "
        "FROM taxi_points "
        "WHERE rowid >= ? AND rowid <= ? "
        "ORDER BY rowid;";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        errorMessage = "按 rowid 分块读取预编译失败: ";
        errorMessage += sqlite3_errmsg(db);
        sqlite3_close(db);
        return false;
    }

    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(beginRowId));
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(endRowId));

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        GPSPoint p{};
        p.id = sqlite3_column_int(stmt, 0);
        p.timestamp = static_cast<long long>(sqlite3_column_int64(stmt, 1));
        p.lon = sqlite3_column_double(stmt, 2);
        p.lat = sqlite3_column_double(stmt, 3);
        outPoints.push_back(p);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    return true;
}

bool isSortedByIdAndTime(const std::vector<GPSPoint>& points, int& badIndex)
{
    badIndex = -1;
    if (points.size() < 2) {
        return true;
    }

    for (std::size_t i = 1; i < points.size(); ++i) {
        const GPSPoint& prev = points[i - 1];
        const GPSPoint& curr = points[i];

        if (curr.id < prev.id) {
            badIndex = static_cast<int>(i);
            return false;
        }
        if (curr.id == prev.id && curr.timestamp < prev.timestamp) {
            badIndex = static_cast<int>(i);
            return false;
        }
    }
    return true;
}

bool buildIdRangesFromAllPoints(const std::vector<GPSPoint>& points,
                                std::unordered_map<int, VehicleRange>& idToRange)
{
    idToRange.clear();
    if (points.empty()) {
        return true;
    }

    int currentId = points[0].id;
    int rangeStart = 0;

    for (int i = 1; i < static_cast<int>(points.size()); ++i) {
        if (points[static_cast<std::size_t>(i)].id != currentId) {
            idToRange[currentId] = {rangeStart, i - 1};
            currentId = points[static_cast<std::size_t>(i)].id;
            rangeStart = i;
        }
    }

    idToRange[currentId] = {rangeStart, static_cast<int>(points.size()) - 1};
    return true;
}

bool loadAllPointsOrderedSingleThread(DatabaseManager& dbm,
                                      std::vector<GPSPoint>& outPoints,
                                      std::unordered_map<int, VehicleRange>& outRanges,
                                      long long& elapsedMs)
{
    outPoints.clear();
    outRanges.clear();
    elapsedMs = 0;

    sqlite3* db = dbm.getRawHandle();
    if (db == nullptr) {
        Debug() << "数据库未打开，无法加载点数据";
        return false;
    }

    const std::int64_t totalCount = dbm.getPointCount();
    if (totalCount <= 0) {
        return true;
    }

    auto start = std::chrono::steady_clock::now();

    outPoints.reserve(static_cast<std::size_t>(totalCount));

    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT id, time, lon, lat "
        "FROM taxi_points "
        "ORDER BY id ASC, time ASC;";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        Debug() << "读取点数据失败: " << sqlite3_errmsg(db);
        return false;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        GPSPoint p{};
        p.id = sqlite3_column_int(stmt, 0);
        p.timestamp = static_cast<long long>(sqlite3_column_int64(stmt, 1));
        p.lon = sqlite3_column_double(stmt, 2);
        p.lat = sqlite3_column_double(stmt, 3);
        outPoints.push_back(p);
    }

    sqlite3_finalize(stmt);

    int badIndex = -1;
    if (!isSortedByIdAndTime(outPoints, badIndex)) {
        Debug() << "单线程 ORDER BY 结果竟然未通过有序校验，异常位置: " << badIndex;
        return false;
    }

    buildIdRangesFromAllPoints(outPoints, outRanges);

    elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    return true;
}
bool readChunkFromDatabase(const std::string& dbPath,
                           std::int64_t offset,
                           std::int64_t limit,
                           std::vector<GPSPoint>& outPoints,
                           std::string& errorMessage)
{
    outPoints.clear();
    if (limit <= 0) {
        return true;
    }

    sqlite3* db = nullptr;
    const int openRc = sqlite3_open_v2(
        dbPath.c_str(),
        &db,
        SQLITE_OPEN_READONLY,
        nullptr);

    if (openRc != SQLITE_OK || db == nullptr) {
        errorMessage = "线程打开数据库失败";
        if (db != nullptr) {
            errorMessage += ": ";
            errorMessage += sqlite3_errmsg(db);
            sqlite3_close(db);
        }
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT id, time, lon, lat "
        "FROM taxi_points "
        "ORDER BY id ASC, time ASC "
        "LIMIT ? OFFSET ?;";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        errorMessage = "线程预编译查询失败: ";
        errorMessage += sqlite3_errmsg(db);
        sqlite3_close(db);
        return false;
    }

    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(limit));
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(offset));

    outPoints.reserve(static_cast<std::size_t>(limit));

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        GPSPoint p{};
        p.id = sqlite3_column_int(stmt, 0);
        p.timestamp = static_cast<long long>(sqlite3_column_int64(stmt, 1));
        p.lon = sqlite3_column_double(stmt, 2);
        p.lat = sqlite3_column_double(stmt, 3);
        outPoints.push_back(p);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return true;
}
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
void distributeFlowToBuckets(std::vector<FlowBucket>& buckets,
                             long long analysisStart,
                             long long bucketSize,
                             long long segStart,
                             long long segEnd,
                             bool isAToB) {
    if (segEnd <= segStart || bucketSize <= 0 || buckets.empty()) {
        return;
    }

    const long long totalDuration = segEnd - segStart;
    const long long analysisEnd =
        analysisStart + bucketSize * static_cast<long long>(buckets.size());

    // 先裁剪到分析窗口内
    const long long clippedStart = std::max(segStart, analysisStart);
    const long long clippedEnd = std::min(segEnd, analysisEnd);

    if (clippedEnd <= clippedStart) {
        return;
    }

    int startBucket = static_cast<int>((clippedStart - analysisStart) / bucketSize);
    int endBucket = static_cast<int>((clippedEnd - 1 - analysisStart) / bucketSize);

    if (startBucket < 0) startBucket = 0;
    if (endBucket >= static_cast<int>(buckets.size())) {
        endBucket = static_cast<int>(buckets.size()) - 1;
    }

    for (int i = startBucket; i <= endBucket; ++i) {
        const long long bucketStart = analysisStart + static_cast<long long>(i) * bucketSize;
        const long long bucketEnd = bucketStart + bucketSize;

        const long long overlapStart = std::max(segStart, bucketStart);
        const long long overlapEnd = std::min(segEnd, bucketEnd);

        if (overlapEnd <= overlapStart) {
            continue;
        }

        const double contribution =
            static_cast<double>(overlapEnd - overlapStart) /
            static_cast<double>(bucketSize);

        if (isAToB) {
            buckets[static_cast<std::size_t>(i)].aToB += contribution;
        } else {
            buckets[static_cast<std::size_t>(i)].bToA += contribution;
        }
    }
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
inline Rect makeRect(double minLon, double minLat,
                     double maxLon, double maxLat) {
    Rect r;
    r.x = (minLon + maxLon) / 2.0;
    r.y = (minLat + maxLat) / 2.0;
    r.w = (maxLon - minLon) / 2.0;
    r.h = (maxLat - minLat) / 2.0;
    return r;
}

inline bool pointInRect(const GPSPoint& p, const Rect& r) {
    return r.contains(p.lon, p.lat);
}

// 在 [startIdx, endIdx] 这个闭区间里找第一个 timestamp >= target 的索引
// 如果都小于 target，返回 endIdx + 1
int lowerBoundPointIndex(const std::vector<GPSPoint>& points,
                         int startIdx,
                         int endIdx,
                         long long target) {
    int left = startIdx;
    int right = endIdx + 1; // 半开区间 [left, right)

    while (left < right) {
        int mid = left + (right - left) / 2;
        if (points[mid].timestamp < target) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }
    return left;
}

// 在 [startIdx, endIdx] 这个闭区间里找第一个 timestamp > target 的索引
// 如果都 <= target，返回 endIdx + 1
int upperBoundPointIndex(const std::vector<GPSPoint>& points,
                         int startIdx,
                         int endIdx,
                         long long target) {
    int left = startIdx;
    int right = endIdx + 1; // 半开区间 [left, right)

    while (left < right) {
        int mid = left + (right - left) / 2;
        if (points[mid].timestamp <= target) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }
    return left;
}

// 求线段 p1->p2 与矩形边界的交点比例 t（0~1）
// enterMode = true  : 用于 外->内，取“最早进入”
// enterMode = false : 用于 内->外，取“最早离开”
// 返回 true 表示找到有效交点
bool intersectSegmentWithRectBoundary(const GPSPoint& p1,
                                      const GPSPoint& p2,
                                      const Rect& rect,
                                      bool enterMode,
                                      double& outT) {
    const double x1 = p1.lon;
    const double y1 = p1.lat;
    const double x2 = p2.lon;
    const double y2 = p2.lat;
    const double dx = x2 - x1;
    const double dy = y2 - y1;

    const double left   = rect.x - rect.w;
    const double right  = rect.x + rect.w;
    const double bottom = rect.y - rect.h;
    const double top    = rect.y + rect.h;

    std::vector<double> candidates;
    candidates.reserve(8);

    auto tryPush = [&](double t) {
        if (t < 0.0 || t > 1.0) return;
        double x = x1 + t * dx;
        double y = y1 + t * dy;

        const double eps = 1e-9;
        bool onVertical = (std::abs(x - left) <= eps || std::abs(x - right) <= eps) &&
                          (y >= bottom - eps && y <= top + eps);
        bool onHorizontal = (std::abs(y - bottom) <= eps || std::abs(y - top) <= eps) &&
                            (x >= left - eps && x <= right + eps);

        if (onVertical || onHorizontal) {
            candidates.push_back(t);
        }
    };

    if (std::abs(dx) > 1e-12) {
        tryPush((left  - x1) / dx);
        tryPush((right - x1) / dx);
    }

    if (std::abs(dy) > 1e-12) {
        tryPush((bottom - y1) / dy);
        tryPush((top    - y1) / dy);
    }

    if (candidates.empty()) {
        return false;
    }

    std::sort(candidates.begin(), candidates.end());

    // 去重
    std::vector<double> uniqueTs;
    uniqueTs.reserve(candidates.size());
    for (double t : candidates) {
        if (uniqueTs.empty() || std::abs(t - uniqueTs.back()) > 1e-9) {
            uniqueTs.push_back(t);
        }
    }

    const bool p1In = rect.contains(p1.lon, p1.lat);
    const bool p2In = rect.contains(p2.lon, p2.lat);

    // 外->内：找最早使“稍后一点在内”的交点
    if (enterMode && !p1In && p2In) {
        for (double t : uniqueTs) {
            double tt = std::min(1.0, t + 1e-7);
            double x = x1 + tt * dx;
            double y = y1 + tt * dy;
            if (rect.contains(x, y)) {
                outT = t;
                return true;
            }
        }
    }

    // 内->外：找最早使“稍后一点在外”的交点
    if (!enterMode && p1In && !p2In) {
        for (double t : uniqueTs) {
            double tt = std::min(1.0, t + 1e-7);
            double x = x1 + tt * dx;
            double y = y1 + tt * dy;
            if (!rect.contains(x, y)) {
                outT = t;
                return true;
            }
        }
    }

    // 兜底：取第一个交点
    outT = uniqueTs.front();
    return true;
}

long long interpolateTimestamp(const GPSPoint& p1,
                               const GPSPoint& p2,
                               double t) {
    if (p2.timestamp <= p1.timestamp) {
        return p2.timestamp;
    }
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;

    return p1.timestamp +
           static_cast<long long>(t * static_cast<double>(p2.timestamp - p1.timestamp));
}

// 估计进入目标矩形的时间：外 -> 内
long long estimateEnterTime(const GPSPoint& prev,
                            const GPSPoint& curr,
                            const Rect& targetRect) {
    double t = 0.5;
    if (intersectSegmentWithRectBoundary(prev, curr, targetRect, true, t)) {
        return interpolateTimestamp(prev, curr, t);
    }
    return prev.timestamp + (curr.timestamp - prev.timestamp) / 2;
}

// 估计离开目标矩形的时间：内 -> 外
long long estimateLeaveTime(const GPSPoint& prev,
                            const GPSPoint& curr,
                            const Rect& targetRect) {
    double t = 0.5;
    if (intersectSegmentWithRectBoundary(prev, curr, targetRect, false, t)) {
        return interpolateTimestamp(prev, curr, t);
    }
    return prev.timestamp + (curr.timestamp - prev.timestamp) / 2;
}

// 新增：单区域关联流量统计（F6功能）
bool isValidRegionRect(const RegionRect& rect) {
    return rect.minLon < rect.maxLon && rect.minLat < rect.maxLat;
}
bool rectsOverlap(const RegionRect& a, const RegionRect& b) {
    return !(a.maxLon <= b.minLon ||
             a.minLon >= b.maxLon ||
             a.maxLat <= b.minLat ||
             a.minLat >= b.maxLat);
}
std::vector<RegionRect> buildOuterRegions(
    const RegionRect& target,
    const RegionRect& globalBounds) {

    std::vector<RegionRect> regions;

    if (!isValidRegionRect(target) || !isValidRegionRect(globalBounds)) {
        return regions;
    }

    // 先把目标区域裁剪到全局边界内，避免越界输入导致外部区域计算异常
    RegionRect clippedTarget{
        std::max(target.minLon, globalBounds.minLon),
        std::max(target.minLat, globalBounds.minLat),
        std::min(target.maxLon, globalBounds.maxLon),
        std::min(target.maxLat, globalBounds.maxLat)
    };

    if (!isValidRegionRect(clippedTarget)) {
        return regions;
    }

    // 上方
    RegionRect top{
        globalBounds.minLon,
        clippedTarget.maxLat,
        globalBounds.maxLon,
        globalBounds.maxLat
    };

    // 下方
    RegionRect bottom{
        globalBounds.minLon,
        globalBounds.minLat,
        globalBounds.maxLon,
        clippedTarget.minLat
    };

    // 左侧
    RegionRect left{
        globalBounds.minLon,
        clippedTarget.minLat,
        clippedTarget.minLon,
        clippedTarget.maxLat
    };

    // 右侧
    RegionRect right{
        clippedTarget.maxLon,
        clippedTarget.minLat,
        globalBounds.maxLon,
        clippedTarget.maxLat
    };

    if (isValidRegionRect(top)) {
        regions.push_back(top);
    }
    if (isValidRegionRect(bottom)) {
        regions.push_back(bottom);
    }
    if (isValidRegionRect(left)) {
        regions.push_back(left);
    }
    if (isValidRegionRect(right)) {
        regions.push_back(right);
    }

    return regions;
}
} // namespace


bool DataManager::loadAllPoints(DatabaseManager& dbm) {
    allPoints.clear();
    idToRange.clear();

    sqlite3* db = dbm.getRawHandle();
    if (db == nullptr) {
        Debug() << "数据库未打开，无法加载点数据";
        return false;
    }

    const std::int64_t totalCount = dbm.getPointCount();
    if (totalCount <= 0) {
        Debug() << "数据库中没有点数据";
        return true;
    }

    std::int64_t minRowId = 0;
    std::int64_t maxRowId = 0;
    {
        sqlite3_stmt* stmt = nullptr;
        const char* sql = "SELECT MIN(rowid), MAX(rowid) FROM taxi_points;";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            Debug() << "读取 rowid 范围失败: " << sqlite3_errmsg(db);
            return false;
        }

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            minRowId = static_cast<std::int64_t>(sqlite3_column_int64(stmt, 0));
            maxRowId = static_cast<std::int64_t>(sqlite3_column_int64(stmt, 1));
        }
        sqlite3_finalize(stmt);
    }

    if (minRowId <= 0 || maxRowId < minRowId) {
        Debug() << "rowid 范围异常，回退单线程 ORDER BY 读取";
        long long fallbackMs = 0;
        if (!loadAllPointsOrderedSingleThread(dbm, allPoints, idToRange, fallbackMs)) {
            return false;
        }
        Debug() << "单线程 ORDER BY 读取完成，耗时(ms): " << fallbackMs;
        return true;
    }

    unsigned int threadCount = std::thread::hardware_concurrency();
    if (threadCount == 0) {
        threadCount = 4;
    }
    threadCount = std::min<unsigned int>(threadCount, 8);

    const std::int64_t rowidSpan = maxRowId - minRowId + 1;
    const std::int64_t chunkSpan =
        (rowidSpan + static_cast<std::int64_t>(threadCount) - 1) /
        static_cast<std::int64_t>(threadCount);

    const std::string dbPath = dbm.getDbPath();
    if (dbPath.empty()) {
        Debug() << "数据库路径为空，回退单线程 ORDER BY 读取";
        long long fallbackMs = 0;
        if (!loadAllPointsOrderedSingleThread(dbm, allPoints, idToRange, fallbackMs)) {
            return false;
        }
        Debug() << "单线程 ORDER BY 读取完成，耗时(ms): " << fallbackMs;
        return true;
    }

    auto totalStart = std::chrono::steady_clock::now();

    std::vector<std::vector<GPSPoint>> chunkResults(threadCount);
    std::vector<long long> chunkElapsed(threadCount, 0);
    std::vector<std::thread> workers;
    workers.reserve(threadCount);

    std::atomic<bool> hasError(false);
    std::mutex errorMutex;
    std::string firstErrorMessage;

    Debug() << "开始按 rowid 多线程读取，总点数: " << totalCount
            << "，rowid范围: [" << minRowId << ", " << maxRowId << "]"
            << "，线程数: " << threadCount
            << "，每块 rowid 跨度: " << chunkSpan;

    for (unsigned int i = 0; i < threadCount; ++i) {
        const std::int64_t beginRowId =
            minRowId + static_cast<std::int64_t>(i) * chunkSpan;
        if (beginRowId > maxRowId) {
            break;
        }

        const std::int64_t endRowId =
            std::min(maxRowId, beginRowId + chunkSpan - 1);

        workers.emplace_back([&, i, beginRowId, endRowId]() {
            std::string errorMessage;
            long long elapsedMs = 0;

            if (!readChunkByRowIdRange(
                    dbPath,
                    beginRowId,
                    endRowId,
                    chunkResults[i],
                    errorMessage,
                    elapsedMs)) {
                hasError.store(true);
                std::lock_guard<std::mutex> lock(errorMutex);
                if (firstErrorMessage.empty()) {
                    firstErrorMessage = errorMessage;
                }
                return;
            }

            chunkElapsed[i] = elapsedMs;
            Debug() << "分块 " << i
                    << " 完成，rowid=[" << beginRowId << "," << endRowId << "]"
                    << "，读取点数=" << chunkResults[i].size()
                    << "，耗时(ms)=" << elapsedMs;
        });
    }

    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    if (hasError.load()) {
        Debug() << "按 rowid 多线程读取失败: " << firstErrorMessage
                << "，回退单线程 ORDER BY 读取";
        long long fallbackMs = 0;
        if (!loadAllPointsOrderedSingleThread(dbm, allPoints, idToRange, fallbackMs)) {
            return false;
        }
        Debug() << "单线程 ORDER BY 回退完成，耗时(ms): " << fallbackMs;
        return true;
    }

    allPoints.reserve(static_cast<std::size_t>(totalCount));
    for (unsigned int i = 0; i < threadCount; ++i) {
        allPoints.insert(
            allPoints.end(),
            std::make_move_iterator(chunkResults[i].begin()),
            std::make_move_iterator(chunkResults[i].end()));
    }

    const long long mergeDoneMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - totalStart).count();

    Debug() << "多线程 rowid 读取+合并完成，当前点数=" << allPoints.size()
            << "，总耗时(ms)=" << mergeDoneMs;

    if (static_cast<std::int64_t>(allPoints.size()) != totalCount) {
        Debug() << "点数不一致，期望=" << totalCount
                << "，实际=" << allPoints.size()
                << "，回退单线程 ORDER BY";
        allPoints.clear();
        idToRange.clear();

        long long fallbackMs = 0;
        if (!loadAllPointsOrderedSingleThread(dbm, allPoints, idToRange, fallbackMs)) {
            return false;
        }
        Debug() << "单线程 ORDER BY 回退完成，耗时(ms): " << fallbackMs;
        return true;
    }

    int badIndex = -1;
    const bool sortedOk = isSortedByIdAndTime(allPoints, badIndex);

    if (!sortedOk) {
        Debug() << "rowid 快路径未通过 id/time 有序校验，异常位置=" << badIndex
                << "，回退单线程 ORDER BY 读取";

        std::vector<GPSPoint> fallbackPoints;
        std::unordered_map<int, VehicleRange> fallbackRanges;
        long long fallbackMs = 0;

        if (!loadAllPointsOrderedSingleThread(dbm, fallbackPoints, fallbackRanges, fallbackMs)) {
            return false;
        }

        allPoints = std::move(fallbackPoints);
        idToRange = std::move(fallbackRanges);

        Debug() << "单线程 ORDER BY 回退完成，耗时(ms): " << fallbackMs;
        Debug() << "最终 allPoints 已按 id 递增、time 递增排列";
        Debug() << "共建立 " << idToRange.size() << " 个车辆区间映射";
        return true;
    }

    buildIdRangesFromAllPoints(allPoints, idToRange);

    const long long totalElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - totalStart).count();

    Debug() << "rowid 快路径通过有序校验";
    Debug() << "全部点加载完成，共 " << allPoints.size() << " 个点";
    Debug() << "当前 allPoints 已按 id 递增、time 递增排列";
    Debug() << "共建立 " << idToRange.size() << " 个车辆区间映射";
    Debug() << "loadAllPoints 总耗时(ms): " << totalElapsedMs;

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
std::vector<FlowBucket> DataManager::queryBidirectionalFlow(
    double minLonA, double minLatA,
    double maxLonA, double maxLatA,
    double minLonB, double minLatB,
    double maxLonB, double maxLatB,
    long long tStart,
    long long bucketSize,
    int bucketCount,
    long long deltaT) {

    std::vector<FlowBucket> result;

    if (bucketCount <= 0 || bucketSize <= 0 || deltaT < 0) {
        return result;
    }

    result.resize(static_cast<std::size_t>(bucketCount));
    for (int i = 0; i < bucketCount; ++i) {
        result[static_cast<std::size_t>(i)].bucketStart = tStart + i * bucketSize;
        result[static_cast<std::size_t>(i)].aToB = 0;
        result[static_cast<std::size_t>(i)].bToA = 0;
    }

    if (!quadTreeRoot || allPoints.empty() || idToRange.empty()) {
        return result;
    }

    const long long tEnd = tStart + bucketSize * static_cast<long long>(bucketCount);
    const long long queryStart = tStart - deltaT;
    const long long queryEnd = tEnd + deltaT;

    const Rect rectA = makeRect(minLonA, minLatA, maxLonA, maxLatA);
    const Rect rectB = makeRect(minLonB, minLatB, maxLonB, maxLatB);

    // 1. 分别查询 A、B 区域在扩展时间窗内出现过的车辆，再取交集
    std::unordered_set<int> idsInA = querySpatioTemporalUniqueIds(
        minLonA, minLatA, maxLonA, maxLatA, queryStart, queryEnd);

    std::unordered_set<int> idsInB = querySpatioTemporalUniqueIds(
        minLonB, minLatB, maxLonB, maxLatB, queryStart, queryEnd);

    if (idsInA.empty() || idsInB.empty()) {
        return result;
    }

    std::vector<int> candidateIds;
    candidateIds.reserve(std::min(idsInA.size(), idsInB.size()));

    if (idsInA.size() <= idsInB.size()) {
        for (int id : idsInA) {
            if (idsInB.find(id) != idsInB.end()) {
                candidateIds.push_back(id);
            }
        }
    } else {
        for (int id : idsInB) {
            if (idsInA.find(id) != idsInA.end()) {
                candidateIds.push_back(id);
            }
        }
    }

    // 2. 逐车分析
    for (int vid : candidateIds) {
        auto it = idToRange.find(vid);
        if (it == idToRange.end()) {
            continue;
        }

        const VehicleRange& vr = it->second;
        if (vr.start < 0 || vr.end < vr.start) {
            continue;
        }

        // 利用每辆车时间有序，先二分定位到扩展时间窗口内
        int leftIdx = lowerBoundPointIndex(allPoints, vr.start, vr.end, queryStart);
        int rightExclusive = upperBoundPointIndex(allPoints, vr.start, vr.end, queryEnd);

        if (leftIdx >= rightExclusive) {
            continue;
        }

        // 为了检测跨边界，左边尽量多带一个点
        if (leftIdx > vr.start) {
            --leftIdx;
        }

        // 状态变量
        bool pendingA2B = false;     // 是否已从 A 离开，等待进入 B
        bool pendingB2A = false;     // 是否已从 B 离开，等待进入 A
        long long leaveATime = -1;   // 最近一次离开 A 的时间
        long long leaveBTime = -1;   // 最近一次离开 B 的时间

        for (int i = leftIdx + 1; i < rightExclusive; ++i) {
            const GPSPoint& prev = allPoints[i - 1];
            const GPSPoint& curr = allPoints[i];

            const bool prevInA = pointInRect(prev, rectA);
            const bool currInA = pointInRect(curr, rectA);
            const bool prevInB = pointInRect(prev, rectB);
            const bool currInB = pointInRect(curr, rectB);

            // -----------------------------
            // 1) 先检测“离开 A”
            // -----------------------------
            if (prevInA && !currInA) {
                leaveATime = estimateLeaveTime(prev, curr, rectA);
                pendingA2B = true;

                // 新的 A->B 开始后，之前等待中的 B->A 逻辑作废
                pendingB2A = false;
                leaveBTime = -1;
            }

            // -----------------------------
            // 2) 检测“离开 B”
            // -----------------------------
            if (prevInB && !currInB) {
                leaveBTime = estimateLeaveTime(prev, curr, rectB);
                pendingB2A = true;

                // 新的 B->A 开始后，之前等待中的 A->B 逻辑作废
                pendingA2B = false;
                leaveATime = -1;
            }

            // -----------------------------
            // 3) 检测“进入 B”，完成 A->B
            // -----------------------------
            if (!prevInB && currInB) {
                long long enterBTime = estimateEnterTime(prev, curr, rectB);

                if (pendingA2B && leaveATime >= 0) {
    const long long travelTime = enterBTime - leaveATime;

    if (travelTime > 0 && travelTime <= deltaT) {
        distributeFlowToBuckets(
            result,
            tStart,
            bucketSize,
            leaveATime,
            enterBTime,
            true
        );
    }
}

                // 无论是否成功统计，进入 B 后 A->B 这次流程结束
                pendingA2B = false;
                leaveATime = -1;
            }

            // -----------------------------
            // 4) 检测“进入 A”，完成 B->A
            // -----------------------------
            if (!prevInA && currInA) {
                long long enterATime = estimateEnterTime(prev, curr, rectA);

                if (pendingB2A && leaveBTime >= 0) {
    const long long travelTime = enterATime - leaveBTime;

    if (travelTime > 0 && travelTime <= deltaT) {
        distributeFlowToBuckets(
            result,
            tStart,
            bucketSize,
            leaveBTime,
            enterATime,
            false
        );
    }
}

                // 无论是否成功统计，进入 A 后 B->A 这次流程结束
                pendingB2A = false;
                leaveBTime = -1;
            }
        }
    }

    return result;
}

// 3. 基于双向流量查询，统计单区域流入/流出流量
std::vector<SingleRegionFlowBucket> DataManager::querySingleRegionFlow(
    double targetMinLon, double targetMinLat,
    double targetMaxLon, double targetMaxLat,
    double globalMinLon, double globalMinLat,
    double globalMaxLon, double globalMaxLat,
    long long tStart,
    long long bucketSize,
    int bucketCount,
    long long deltaT) {

    std::vector<SingleRegionFlowBucket> result;

    if (bucketCount <= 0 || bucketSize <= 0 || deltaT < 0) {
        return result;
    }

    RegionRect target{
        targetMinLon, targetMinLat,
        targetMaxLon, targetMaxLat
    };

    RegionRect globalBounds{
        globalMinLon, globalMinLat,
        globalMaxLon, globalMaxLat
    };

    if (!isValidRegionRect(target) || !isValidRegionRect(globalBounds)) {
        return result;
    }

    // 目标区域必须与全局范围有交集；否则这个分析没有意义
    if (!rectsOverlap(target, globalBounds) &&
        !(target.minLon >= globalBounds.minLon &&
          target.maxLon <= globalBounds.maxLon &&
          target.minLat >= globalBounds.minLat &&
          target.maxLat <= globalBounds.maxLat)) {
        return result;
    }

    result.resize(static_cast<std::size_t>(bucketCount));
    for (int i = 0; i < bucketCount; ++i) {
        result[static_cast<std::size_t>(i)].bucketStart = tStart + i * bucketSize;
        result[static_cast<std::size_t>(i)].incoming = 0.0;
        result[static_cast<std::size_t>(i)].outgoing = 0.0;
    }

    const std::vector<RegionRect> outerRegions = buildOuterRegions(target, globalBounds);
    if (outerRegions.empty()) {
        return result;
    }

    for (const auto& outer : outerRegions) {
        const std::vector<FlowBucket> partial = queryBidirectionalFlow(
            target.minLon, target.minLat,
            target.maxLon, target.maxLat,
            outer.minLon, outer.minLat,
            outer.maxLon, outer.maxLat,
            tStart,
            bucketSize,
            bucketCount,
            deltaT
        );

        if (partial.size() != result.size()) {
            continue;
        }

        for (std::size_t i = 0; i < result.size(); ++i) {
            // F5 里 A=target, B=outer
            // 所以：
            // A->B = 目标区域流出
            // B->A = 目标区域流入
            result[i].outgoing += partial[i].aToB;
            result[i].incoming += partial[i].bToA;
        }
    }

    return result;
}

std::vector<FastestPathBucket> DataManager::queryFastestPathsBetweenRegions(
    double minLonA, double minLatA,
    double maxLonA, double maxLatA,
    double minLonB, double minLatB,
    double maxLonB, double maxLatB,
    long long tStart,
    long long bucketSize,
    int bucketCount,
    long long deltaT) {

    std::vector<FastestPathBucket> result;
    if (bucketCount <= 0 || bucketSize <= 0 || deltaT <= 0) {
        return result;
    }

    result.resize(static_cast<std::size_t>(bucketCount));
    for (int i = 0; i < bucketCount; ++i) {
        result[static_cast<std::size_t>(i)].bucketStart = tStart + static_cast<long long>(i) * bucketSize;
    }

    if (!quadTreeRoot || allPoints.empty() || idToRange.empty()) {
        return result;
    }

    const long long tEnd = tStart + bucketSize * static_cast<long long>(bucketCount);
    const long long queryStart = tStart - deltaT;
    const long long queryEnd = tEnd + deltaT;

    const Rect rectA = makeRect(minLonA, minLatA, maxLonA, maxLatA);
    const Rect rectB = makeRect(minLonB, minLatB, maxLonB, maxLatB);

    std::unordered_set<int> idsInA = querySpatioTemporalUniqueIds(
        minLonA, minLatA, maxLonA, maxLatA, queryStart, queryEnd);
    std::unordered_set<int> idsInB = querySpatioTemporalUniqueIds(
        minLonB, minLatB, maxLonB, maxLatB, queryStart, queryEnd);

    if (idsInA.empty() || idsInB.empty()) {
        return result;
    }

    std::vector<int> candidateIds;
    candidateIds.reserve(std::min(idsInA.size(), idsInB.size()));
    if (idsInA.size() <= idsInB.size()) {
        for (int id : idsInA) {
            if (idsInB.find(id) != idsInB.end()) {
                candidateIds.push_back(id);
            }
        }
    } else {
        for (int id : idsInB) {
            if (idsInA.find(id) != idsInA.end()) {
                candidateIds.push_back(id);
            }
        }
    }

    for (int vid : candidateIds) {
        auto it = idToRange.find(vid);
        if (it == idToRange.end()) {
            continue;
        }

        const VehicleRange& vr = it->second;
        if (vr.start < 0 || vr.end < vr.start) {
            continue;
        }

        int leftIdx = lowerBoundPointIndex(allPoints, vr.start, vr.end, queryStart);
        int rightExclusive = upperBoundPointIndex(allPoints, vr.start, vr.end, queryEnd);
        if (leftIdx >= rightExclusive) {
            continue;
        }
        if (leftIdx > vr.start) {
            --leftIdx;
        }

        bool pendingA2B = false;
        long long leaveATime = -1;
        int leaveIndex = -1;

        for (int i = leftIdx + 1; i < rightExclusive; ++i) {
            const GPSPoint& prev = allPoints[static_cast<std::size_t>(i - 1)];
            const GPSPoint& curr = allPoints[static_cast<std::size_t>(i)];

            const bool prevInA = pointInRect(prev, rectA);
            const bool currInA = pointInRect(curr, rectA);
            const bool prevInB = pointInRect(prev, rectB);
            const bool currInB = pointInRect(curr, rectB);

            if (prevInA && !currInA) {
                leaveATime = estimateLeaveTime(prev, curr, rectA);
                leaveIndex = i - 1;
                pendingA2B = true;
            }

            if (!prevInB && currInB) {
                const long long enterBTime = estimateEnterTime(prev, curr, rectB);
                if (!pendingA2B || leaveATime < 0 || leaveIndex < 0) {
                    continue;
                }

                const long long travelTime = enterBTime - leaveATime;
                if (travelTime <= 0 || travelTime > deltaT) {
                    pendingA2B = false;
                    leaveATime = -1;
                    leaveIndex = -1;
                    continue;
                }

                if (leaveATime < tStart || leaveATime >= tEnd) {
                    pendingA2B = false;
                    leaveATime = -1;
                    leaveIndex = -1;
                    continue;
                }

                int bucketIndex = static_cast<int>((leaveATime - tStart) / bucketSize);
                if (bucketIndex < 0 || bucketIndex >= bucketCount) {
                    pendingA2B = false;
                    leaveATime = -1;
                    leaveIndex = -1;
                    continue;
                }

                FastestPathBucket& bucket = result[static_cast<std::size_t>(bucketIndex)];
                if (!bucket.found || travelTime < bucket.travelTime) {
                    bucket.found = true;
                    bucket.taxiId = vid;
                    bucket.leaveTime = leaveATime;
                    bucket.enterTime = enterBTime;
                    bucket.travelTime = travelTime;
                    bucket.points.clear();

                    const int pathStart = std::max(leaveIndex, vr.start);
                    const int pathEnd = std::min(i, vr.end);
                    for (int idx = pathStart; idx <= pathEnd; ++idx) {
                        bucket.points.push_back(allPoints[static_cast<std::size_t>(idx)]);
                    }
                }

                pendingA2B = false;
                leaveATime = -1;
                leaveIndex = -1;
            }
        }
    }

    return result;
}

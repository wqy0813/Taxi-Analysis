#include "densityanalysis.h"

#include <QElapsedTimer>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "datamanager.h"

namespace {

constexpr double kMetersPerDegreeLat = 111000.0;
constexpr double kPi = 3.14159265358979323846;
constexpr double kEpsilon = 1e-9;

struct GridKey {
    int x = 0;
    int y = 0;

    bool operator==(const GridKey& other) const
    {
        return x == other.x && y == other.y;
    }
};

struct GridKeyHash {
    std::size_t operator()(const GridKey& key) const
    {
        const std::size_t hx = std::hash<int>()(key.x);
        const std::size_t hy = std::hash<int>()(key.y);
        return hx ^ (hy << 1);
    }
};

// 轻量聚合记录：
// 仅保留排序和分组统计需要的最小字段，避免旧方案“每桶每格维护一个 set”带来的内存放大。
struct PointRecord {
    int bucketIndex = 0;
    int gx = 0;
    int gy = 0;
    int taxiId = 0;
};

double metersToLatDegrees(const double meters)
{
    return meters / kMetersPerDegreeLat;
}

double metersToLonDegrees(const double meters, const double latitudeDegrees)
{
    const double cosValue = std::cos(latitudeDegrees * kPi / 180.0);
    const double metersPerDegreeLon = std::max(1.0, kMetersPerDegreeLat * std::abs(cosValue));
    return meters / metersPerDegreeLon;
}

double estimateCellAreaKm2(const double lonStep, const double latStep, const double latitudeDegrees)
{
    const double latMeters = latStep * kMetersPerDegreeLat;
    const double lonMeters = lonStep * kMetersPerDegreeLat * std::abs(std::cos(latitudeDegrees * kPi / 180.0));
    const double areaM2 = std::max(1.0, latMeters * lonMeters);
    return areaM2 / 1'000'000.0;
}

int clampGridIndex(const double value, const int maxIndex)
{
    if (maxIndex <= 0) {
        return 0;
    }
    if (value <= 0.0) {
        return 0;
    }

    const int index = static_cast<int>(std::floor(value));
    if (index < 0) {
        return 0;
    }
    if (index > maxIndex) {
        return maxIndex;
    }
    return index;
}

double calcDeltaRate(const double current, const double previous)
{
    if (std::abs(previous) <= kEpsilon) {
        return current > 0.0 ? 1.0 : 0.0;
    }
    return (current - previous) / previous;
}

bool validateRequest(const DensityAnalysisRequest& request, QString& errorMessage)
{
    if (request.minLon >= request.maxLon || request.minLat >= request.maxLat) {
        errorMessage = QStringLiteral("map bounds invalid");
        return false;
    }
    if (request.startTime > request.endTime) {
        errorMessage = QStringLiteral("start time must be <= end time");
        return false;
    }
    if (request.intervalMinutes <= 0) {
        errorMessage = QStringLiteral("intervalMinutes must be > 0");
        return false;
    }
    if (request.cellSizeMeters <= 0.0) {
        errorMessage = QStringLiteral("cellSizeMeters must be > 0");
        return false;
    }
    return true;
}

void initBuckets(
    std::vector<DensityTimeBucket>& buckets,
    const long long startTime,
    const long long endTime,
    const int bucketSeconds)
{
    for (std::size_t i = 0; i < buckets.size(); ++i) {
        DensityTimeBucket& bucket = buckets[i];
        const long long bucketStart = startTime + static_cast<long long>(i) * bucketSeconds;
        bucket.startTime = bucketStart;
        bucket.endTime = std::min(bucketStart + bucketSeconds - 1LL, endTime);
    }
}

} // namespace

DensityAnalysisResult DensityAnalyzer::analyze(const DensityAnalysisRequest& request)
{
    DensityAnalysisResult result;

    // 参数合法性校验：只做输入正确性检查，不对“正常查询”做硬阈值拒绝。
    if (!validateRequest(request, result.errorMessage)) {
        return result;
    }

    QElapsedTimer timer;
    timer.start();

    const double centerLat = (request.minLat + request.maxLat) * 0.5;
    const double latStep = metersToLatDegrees(request.cellSizeMeters);
    const double lonStep = metersToLonDegrees(request.cellSizeMeters, centerLat);
    if (latStep <= 0.0 || lonStep <= 0.0) {
        result.errorMessage = QStringLiteral("grid step conversion failed");
        return result;
    }

    const int bucketSeconds = request.intervalMinutes * 60;
    const long long totalSeconds = std::max<long long>(0, request.endTime - request.startTime);
    const int bucketCount = static_cast<int>(totalSeconds / bucketSeconds) + 1;
    if (bucketCount <= 0) {
        result.errorMessage = QStringLiteral("bucket count invalid");
        return result;
    }

    const int columnCount = std::max(1, static_cast<int>(std::ceil((request.maxLon - request.minLon) / lonStep)));
    const int rowCount = std::max(1, static_cast<int>(std::ceil((request.maxLat - request.minLat) / latStep)));
    const long long gridCount = static_cast<long long>(columnCount) * static_cast<long long>(rowCount);
    const long long analysisScale = static_cast<long long>(bucketCount) * gridCount;

    result.lonStep = lonStep;
    result.latStep = latStep;
    result.cellAreaKm2 = estimateCellAreaKm2(lonStep, latStep, centerLat);
    result.columnCount = columnCount;
    result.rowCount = rowCount;
    result.bucketCount = bucketCount;
    result.gridCount = gridCount;
    result.analysisScale = analysisScale;

    // 候选点输入仍然严格使用现有接口。
    const std::vector<GPSPoint> candidates = DataManager::querySpatialAndTime(
        request.minLon,
        request.minLat,
        request.maxLon,
        request.maxLat,
        request.startTime,
        request.endTime);

    // 旧方案问题：
    // 为每个(时间桶, 网格)维护 unordered_set<int>，在大范围细网格下会产生大量小哈希表，
    // 内存碎片和哈希开销都较重。
    //
    // 新方案：
    // 1) 先压缩成轻量记录 (bucketIndex, gx, gy, taxiId)；
    // 2) 按 (bucket, gx, gy, taxiId) 排序；
    // 3) 线性扫描一次，精确得到 pointCount 和 vehicleCount。
    // 该方案把“去重”转换为“相邻比较”，显著降低临时内存占用。
    std::vector<PointRecord> records;
    records.reserve(candidates.size());

    std::unordered_set<int> totalVehicleIds;
    totalVehicleIds.reserve(candidates.size() / 4 + 1);

    for (const auto& point : candidates) {
        const int bucketIndex = std::clamp(
            static_cast<int>((point.timestamp - request.startTime) / bucketSeconds),
            0,
            bucketCount - 1);

        const int gx = clampGridIndex((point.lon - request.minLon) / lonStep, columnCount - 1);
        const int gy = clampGridIndex((point.lat - request.minLat) / latStep, rowCount - 1);

        records.push_back(PointRecord{bucketIndex, gx, gy, point.id});
        totalVehicleIds.insert(point.id);
    }

    result.totalPointCount = static_cast<long long>(records.size());
    result.totalVehicleCount = static_cast<int>(totalVehicleIds.size());

    std::sort(records.begin(), records.end(),
              [](const PointRecord& a, const PointRecord& b) {
                  if (a.bucketIndex != b.bucketIndex) {
                      return a.bucketIndex < b.bucketIndex;
                  }
                  if (a.gy != b.gy) {
                      return a.gy < b.gy;
                  }
                  if (a.gx != b.gx) {
                      return a.gx < b.gx;
                  }
                  return a.taxiId < b.taxiId;
              });

    std::vector<DensityTimeBucket> buckets(static_cast<std::size_t>(bucketCount));
    initBuckets(buckets, request.startTime, request.endTime, bucketSeconds);

    // 线性扫描聚合：
    // 每次处理一个 (bucketIndex, gx, gy) 分组，分组内：
    // - pointCount = 记录条数
    // - vehicleCount = taxiId 去重后数量（通过有序相邻比较实现）
    std::size_t i = 0;
    while (i < records.size()) {
        const int bucketIndex = records[i].bucketIndex;
        const int gx = records[i].gx;
        const int gy = records[i].gy;

        int pointCount = 0;
        int vehicleCount = 0;
        int lastTaxiId = 0;
        bool hasLastTaxi = false;

        while (i < records.size() &&
               records[i].bucketIndex == bucketIndex &&
               records[i].gx == gx &&
               records[i].gy == gy) {
            ++pointCount;
            if (!hasLastTaxi || records[i].taxiId != lastTaxiId) {
                ++vehicleCount;
                lastTaxiId = records[i].taxiId;
                hasLastTaxi = true;
            }
            ++i;
        }

        DensityGridCell cell;
        cell.gx = gx;
        cell.gy = gy;
        cell.pointCount = pointCount;
        cell.vehicleCount = vehicleCount;
        cell.vehicleDensity = vehicleCount / std::max(kEpsilon, result.cellAreaKm2);
        cell.flowIntensity = pointCount / std::max(kEpsilon, result.cellAreaKm2);

        DensityTimeBucket& bucket = buckets[static_cast<std::size_t>(bucketIndex)];
        bucket.maxVehicleCount = std::max(bucket.maxVehicleCount, vehicleCount);
        bucket.maxVehicleDensity = std::max(bucket.maxVehicleDensity, cell.vehicleDensity);
        bucket.totalPointCount += pointCount;
        bucket.totalVehicleCount += vehicleCount;
        bucket.totalFlowDensity += cell.vehicleDensity;
        bucket.cells.push_back(std::move(cell));
    }

    // 计算跨桶变化：
    // 对同一网格，和“上一时间桶”做 delta 统计。
    std::unordered_map<GridKey, double, GridKeyHash> prevDensityByCell;
    std::unordered_map<GridKey, int, GridKeyHash> prevVehicleByCell;
    double prevTotalFlowDensity = 0.0;

    for (auto& bucket : buckets) {
        if (!bucket.cells.empty()) {
            bucket.avgVehicleDensity = bucket.totalFlowDensity / static_cast<double>(bucket.cells.size());
        }
        bucket.deltaRate = calcDeltaRate(bucket.totalFlowDensity, prevTotalFlowDensity);
        prevTotalFlowDensity = bucket.totalFlowDensity;

        std::unordered_map<GridKey, double, GridKeyHash> currentDensityByCell;
        std::unordered_map<GridKey, int, GridKeyHash> currentVehicleByCell;
        currentDensityByCell.reserve(bucket.cells.size());
        currentVehicleByCell.reserve(bucket.cells.size());

        for (auto& cell : bucket.cells) {
            const GridKey key{cell.gx, cell.gy};
            const auto prevDensityIt = prevDensityByCell.find(key);
            const auto prevVehicleIt = prevVehicleByCell.find(key);
            const double prevDensity = prevDensityIt != prevDensityByCell.end() ? prevDensityIt->second : 0.0;
            const int prevVehicle = prevVehicleIt != prevVehicleByCell.end() ? prevVehicleIt->second : 0;

            cell.deltaVehicleDensity = cell.vehicleDensity - prevDensity;
            cell.deltaVehicleCount = cell.vehicleCount - prevVehicle;
            cell.deltaRate = calcDeltaRate(cell.vehicleDensity, prevDensity);

            currentDensityByCell[key] = cell.vehicleDensity;
            currentVehicleByCell[key] = cell.vehicleCount;
        }

        prevDensityByCell.swap(currentDensityByCell);
        prevVehicleByCell.swap(currentVehicleByCell);

        // 保持与现有前端一致：同桶内按密度降序输出。
        std::sort(bucket.cells.begin(), bucket.cells.end(),
                  [](const DensityGridCell& a, const DensityGridCell& b) {
                      if (a.vehicleDensity != b.vehicleDensity) {
                          return a.vehicleDensity > b.vehicleDensity;
                      }
                      if (a.vehicleCount != b.vehicleCount) {
                          return a.vehicleCount > b.vehicleCount;
                      }
                      if (a.gy != b.gy) {
                          return a.gy < b.gy;
                      }
                      return a.gx < b.gx;
                  });

        result.maxVehicleDensity = std::max(result.maxVehicleDensity, bucket.maxVehicleDensity);
    }

    result.buckets = std::move(buckets);
    result.elapsedSeconds = static_cast<double>(timer.elapsed()) / 1000.0;
    result.success = true;
    return result;
}

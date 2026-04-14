#include "densityanalysis.h"

#include <algorithm>
#include <chrono>
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

    bool operator==(const GridKey& other) const {
        return x == other.x && y == other.y;
    }
};

struct GridKeyHash {
    std::size_t operator()(const GridKey& key) const {
        const std::size_t hx = std::hash<int>()(key.x);
        const std::size_t hy = std::hash<int>()(key.y);
        return hx ^ (hy << 1);
    }
};

struct PointRecord {
    int bucketIndex = 0;
    int gx = 0;
    int gy = 0;
    int taxiId = 0;
};

double metersToLatDegrees(const double meters) {
    return meters / kMetersPerDegreeLat;
}

double metersToLonDegrees(const double meters, const double latitudeDegrees) {
    const double cosValue = std::cos(latitudeDegrees * kPi / 180.0);
    const double metersPerDegreeLon = std::max(1.0, kMetersPerDegreeLat * std::abs(cosValue));
    return meters / metersPerDegreeLon;
}

double estimateCellAreaKm2(const double lonStep, const double latStep, const double latitudeDegrees) {
    const double latMeters = latStep * kMetersPerDegreeLat;
    const double lonMeters = lonStep * kMetersPerDegreeLat * std::abs(std::cos(latitudeDegrees * kPi / 180.0));
    const double areaM2 = std::max(1.0, latMeters * lonMeters);
    return areaM2 / 1'000'000.0;
}

int clampGridIndex(const double value, const int maxIndex) {
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

double calcDeltaRate(const double current, const double previous) {
    if (std::abs(previous) <= kEpsilon) {
        return current > 0.0 ? 1.0 : 0.0;
    }
    return (current - previous) / previous;
}

bool validateRequest(const DensityAnalysisRequest& request, std::string& errorMessage) {
    if (request.minLon >= request.maxLon || request.minLat >= request.maxLat) {
        errorMessage = "map bounds invalid";
        return false;
    }
    if (request.startTime > request.endTime) {
        errorMessage = "start time must be <= end time";
        return false;
    }
    if (request.intervalMinutes <= 0) {
        errorMessage = "intervalMinutes must be > 0";
        return false;
    }
    if (request.cellSizeMeters <= 0.0) {
        errorMessage = "cellSizeMeters must be > 0";
        return false;
    }
    return true;
}

void initBuckets(std::vector<DensityTimeBucket>& buckets,
                 const long long startTime,
                 const long long endTime,
                 const int bucketSeconds) {
    for (std::size_t i = 0; i < buckets.size(); ++i) {
        DensityTimeBucket& bucket = buckets[i];
        const long long bucketStart = startTime + static_cast<long long>(i) * bucketSeconds;
        bucket.startTime = bucketStart;
        bucket.endTime = std::min(bucketStart + bucketSeconds - 1LL, endTime);
    }
}

} // namespace

DensityAnalysisResult DensityAnalyzer::analyze(const DensityAnalysisRequest& request) {
    DensityAnalysisResult result;

    if (!validateRequest(request, result.errorMessage)) {
        return result;
    }

    const auto start = std::chrono::steady_clock::now();

    const double centerLat = (request.minLat + request.maxLat) * 0.5;
    const double latStep = metersToLatDegrees(request.cellSizeMeters);
    const double lonStep = metersToLonDegrees(request.cellSizeMeters, centerLat);
    if (latStep <= 0.0 || lonStep <= 0.0) {
        result.errorMessage = "grid step conversion failed";
        return result;
    }

    const int bucketSeconds = request.intervalMinutes * 60;
    const long long totalSeconds = std::max<long long>(0, request.endTime - request.startTime);
    const int bucketCount = static_cast<int>(totalSeconds / bucketSeconds) + 1;
    if (bucketCount <= 0) {
        result.errorMessage = "bucket count invalid";
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

    const std::vector<GPSPoint> candidates = DataManager::querySpatialAndTime(
        request.minLon, request.minLat, request.maxLon, request.maxLat, request.startTime, request.endTime);

    std::vector<PointRecord> records;
    records.reserve(candidates.size());

    std::unordered_set<int> totalVehicleIds;
    totalVehicleIds.reserve(candidates.size() / 4 + 1);

    for (const auto& point : candidates) {
        const int bucketIndex = std::clamp(
            static_cast<int>((point.timestamp - request.startTime) / bucketSeconds), 0, bucketCount - 1);

        const int gx = clampGridIndex((point.lon - request.minLon) / lonStep, columnCount - 1);
        const int gy = clampGridIndex((point.lat - request.minLat) / latStep, rowCount - 1);

        records.push_back(PointRecord{bucketIndex, gx, gy, point.id});
        totalVehicleIds.insert(point.id);
    }

    result.totalPointCount = static_cast<long long>(records.size());
    result.totalVehicleCount = static_cast<int>(totalVehicleIds.size());

    std::sort(records.begin(), records.end(),
              [](const PointRecord& a, const PointRecord& b) {
                  if (a.bucketIndex != b.bucketIndex) return a.bucketIndex < b.bucketIndex;
                  if (a.gy != b.gy) return a.gy < b.gy;
                  if (a.gx != b.gx) return a.gx < b.gx;
                  return a.taxiId < b.taxiId;
              });

    std::vector<DensityTimeBucket> buckets(static_cast<std::size_t>(bucketCount));
    initBuckets(buckets, request.startTime, request.endTime, bucketSeconds);

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

        std::sort(bucket.cells.begin(), bucket.cells.end(),
                  [](const DensityGridCell& a, const DensityGridCell& b) {
                      if (a.vehicleDensity != b.vehicleDensity) return a.vehicleDensity > b.vehicleDensity;
                      if (a.vehicleCount != b.vehicleCount) return a.vehicleCount > b.vehicleCount;
                      if (a.gy != b.gy) return a.gy < b.gy;
                      return a.gx < b.gx;
                  });

        result.maxVehicleDensity = std::max(result.maxVehicleDensity, bucket.maxVehicleDensity);
    }

    result.buckets = std::move(buckets);
    result.elapsedSeconds = static_cast<double>(
                                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count()) / 1000.0;
    result.success = true;
    return result;
}

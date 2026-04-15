#include "densityanalysis.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <vector>
#include "logger.h"
#include "datamanager.h"

namespace {

constexpr double kMetersPerDegreeLat = 111000.0;
constexpr double kPi = 3.14159265358979323846;
constexpr double kEpsilon = 1e-9;

// 轨迹段插值控制：过长时间间隔不插值，避免乱连
constexpr long long kMaxInterpolationGapSeconds = 30 * 60; // 30分钟
// 速度阈值：超过这个值认为异常段，不参与统计
constexpr double kMaxSpeedMetersPerSecond = 45.0; // 约 162 km/h

double metersToLatDegrees(const double meters) {
    return meters / kMetersPerDegreeLat;
}

double metersToLonDegrees(const double meters, const double latitudeDegrees) {
    const double cosValue = std::cos(latitudeDegrees * kPi / 180.0);
    const double metersPerDegreeLon =
        std::max(1.0, kMetersPerDegreeLat * std::abs(cosValue));
    return meters / metersPerDegreeLon;
}

double estimateCellAreaKm2(const double lonStep,
                           const double latStep,
                           const double latitudeDegrees) {
    const double latMeters = latStep * kMetersPerDegreeLat;
    const double lonMeters =
        lonStep * kMetersPerDegreeLat *
        std::abs(std::cos(latitudeDegrees * kPi / 180.0));
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

bool validateRequest(const DensityAnalysisRequest& request,
                     std::string& errorMessage) {
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

double lerp(const double a, const double b, const double ratio) {
    return a + (b - a) * ratio;
}

double clamp01(const double value) {
    if (value < 0.0) return 0.0;
    if (value > 1.0) return 1.0;
    return value;
}

double distanceMeters(const GPSPoint& a, const GPSPoint& b) {
    const double avgLatRad = ((a.lat + b.lat) * 0.5) * kPi / 180.0;
    const double dx =
        (b.lon - a.lon) * kMetersPerDegreeLat * std::cos(avgLatRad);
    const double dy =
        (b.lat - a.lat) * kMetersPerDegreeLat;
    return std::sqrt(dx * dx + dy * dy);
}

// Liang-Barsky 线段裁剪（二维经纬度平面）
// 输入输出都是参数 t ∈ [0,1]
bool clipSegmentToRect(const double x0, const double y0,
                       const double x1, const double y1,
                       const double minX, const double maxX,
                       const double minY, const double maxY,
                       double& tEnter, double& tExit) {
    double t0 = 0.0;
    double t1 = 1.0;

    const double dx = x1 - x0;
    const double dy = y1 - y0;

    auto update = [&](double p, double q) -> bool {
        if (std::abs(p) <= kEpsilon) {
            return q >= 0.0;
        }

        const double r = q / p;
        if (p < 0.0) {
            if (r > t1) return false;
            if (r > t0) t0 = r;
        } else {
            if (r < t0) return false;
            if (r < t1) t1 = r;
        }
        return true;
    };

    if (!update(-dx, x0 - minX)) return false;
    if (!update(dx, maxX - x0)) return false;
    if (!update(-dy, y0 - minY)) return false;
    if (!update(dy, maxY - y0)) return false;

    tEnter = clamp01(t0);
    tExit = clamp01(t1);
    return tEnter <= tExit + kEpsilon;
}

int findVehicleSegmentStartIndex(const std::vector<GPSPoint>& points,
                                 const VehicleRange& range,
                                 const long long requestStartTime) {
    auto begin = points.begin() + range.start;
    auto end = points.begin() + range.end + 1;

    auto it = std::lower_bound(
        begin,
        end,
        requestStartTime,
        [](const GPSPoint& p, const long long t) {
            return p.timestamp < t;
        });

    int index = static_cast<int>(it - points.begin());
    if (index > range.start) {
        --index;
    }
    return std::max(index, range.start);
}

int findVehicleSegmentEndIndex(const std::vector<GPSPoint>& points,
                               const VehicleRange& range,
                               const long long requestEndTime) {
    auto begin = points.begin() + range.start;
    auto end = points.begin() + range.end + 1;

    auto it = std::upper_bound(
        begin,
        end,
        requestEndTime,
        [](const long long t, const GPSPoint& p) {
            return t < p.timestamp;
        });

    int index = static_cast<int>(it - points.begin()) - 1;
    return std::min(index, range.end);
}

inline std::size_t flatIndex(const int bucketIndex,
                             const int gx,
                             const int gy,
                             const int columnCount,
                             const int rowCount) {
    return static_cast<std::size_t>(bucketIndex) *
               static_cast<std::size_t>(columnCount) *
               static_cast<std::size_t>(rowCount) +
           static_cast<std::size_t>(gy) * static_cast<std::size_t>(columnCount) +
           static_cast<std::size_t>(gx);
}

struct TraversalState {
    int gx = 0;
    int gy = 0;
    int bucketIndex = 0;

    int stepX = 0;
    int stepY = 0;

    double currentT = 0.0;
    double nextGridXT = std::numeric_limits<double>::infinity();
    double nextGridYT = std::numeric_limits<double>::infinity();
    double nextBucketT = std::numeric_limits<double>::infinity();

    double deltaGridXT = std::numeric_limits<double>::infinity();
    double deltaGridYT = std::numeric_limits<double>::infinity();
    double deltaBucketT = std::numeric_limits<double>::infinity();
};

bool nearlyEqual(const double a, const double b) {
    return std::abs(a - b) <= 1e-10;
}

void initTraversalState(
    TraversalState& state,
    const double startLon,
    const double startLat,
    const long long startTime,
    const double endLon,
    const double endLat,
    const long long endTime,
    const DensityAnalysisRequest& request,
    const int bucketSeconds,
    const int bucketCount,
    const int columnCount,
    const int rowCount,
    const double lonStep,
    const double latStep) {

    state.gx = clampGridIndex(
        (startLon - request.minLon) / std::max(lonStep, kEpsilon),
        columnCount - 1);
    state.gy = clampGridIndex(
        (startLat - request.minLat) / std::max(latStep, kEpsilon),
        rowCount - 1);

    state.bucketIndex = std::clamp(
        static_cast<int>((startTime - request.startTime) / bucketSeconds),
        0,
        bucketCount - 1);

    const double dx = endLon - startLon;
    const double dy = endLat - startLat;
    const double duration = static_cast<double>(endTime - startTime);

    if (dx > kEpsilon) {
        state.stepX = 1;
        const double nextBoundaryLon =
            request.minLon + static_cast<double>(state.gx + 1) * lonStep;
        state.nextGridXT = clamp01((nextBoundaryLon - startLon) / dx);
        state.deltaGridXT = lonStep / dx;
    } else if (dx < -kEpsilon) {
        state.stepX = -1;
        const double nextBoundaryLon =
            request.minLon + static_cast<double>(state.gx) * lonStep;
        state.nextGridXT = clamp01((nextBoundaryLon - startLon) / dx);
        state.deltaGridXT = -lonStep / dx;
    }

    if (dy > kEpsilon) {
        state.stepY = 1;
        const double nextBoundaryLat =
            request.minLat + static_cast<double>(state.gy + 1) * latStep;
        state.nextGridYT = clamp01((nextBoundaryLat - startLat) / dy);
        state.deltaGridYT = latStep / dy;
    } else if (dy < -kEpsilon) {
        state.stepY = -1;
        const double nextBoundaryLat =
            request.minLat + static_cast<double>(state.gy) * latStep;
        state.nextGridYT = clamp01((nextBoundaryLat - startLat) / dy);
        state.deltaGridYT = -latStep / dy;
    }

    if (duration > kEpsilon && state.bucketIndex < bucketCount - 1) {
        const long long nextBucketBoundaryTime =
            request.startTime + static_cast<long long>(state.bucketIndex + 1) * bucketSeconds;
        state.nextBucketT =
            clamp01(static_cast<double>(nextBucketBoundaryTime - startTime) / duration);
        state.deltaBucketT = static_cast<double>(bucketSeconds) / duration;
    }
}

void advanceTraversalState(TraversalState& state,
                           const double eventT,
                           const int bucketCount,
                           const int columnCount,
                           const int rowCount) {
    if (nearlyEqual(eventT, state.nextGridXT)) {
        state.gx = std::clamp(state.gx + state.stepX, 0, columnCount - 1);

        if ((state.stepX > 0 && state.gx >= columnCount - 1) ||
            (state.stepX < 0 && state.gx <= 0)) {
            state.nextGridXT = std::numeric_limits<double>::infinity();
        } else {
            state.nextGridXT += state.deltaGridXT;
        }
    }

    if (nearlyEqual(eventT, state.nextGridYT)) {
        state.gy = std::clamp(state.gy + state.stepY, 0, rowCount - 1);

        if ((state.stepY > 0 && state.gy >= rowCount - 1) ||
            (state.stepY < 0 && state.gy <= 0)) {
            state.nextGridYT = std::numeric_limits<double>::infinity();
        } else {
            state.nextGridYT += state.deltaGridYT;
        }
    }

    if (nearlyEqual(eventT, state.nextBucketT)) {
        if (state.bucketIndex < bucketCount - 1) {
            ++state.bucketIndex;
            if (state.bucketIndex < bucketCount - 1) {
                state.nextBucketT += state.deltaBucketT;
            } else {
                state.nextBucketT = std::numeric_limits<double>::infinity();
            }
        } else {
            state.nextBucketT = std::numeric_limits<double>::infinity();
        }
    }

    state.currentT = eventT;
}

void accumulateSegmentToDenseArray(
    const long long clippedStartTime,
    const long long clippedEndTime,
    const double clippedStartLon,
    const double clippedStartLat,
    const double clippedEndLon,
    const double clippedEndLat,
    const DensityAnalysisRequest& request,
    const int bucketSeconds,
    const int bucketCount,
    const int columnCount,
    const int rowCount,
    const double lonStep,
    const double latStep,
    std::vector<float>& vehicleSeconds) {

    if (clippedEndTime <= clippedStartTime) {
        return;
    }

    const double totalDuration =
        static_cast<double>(clippedEndTime - clippedStartTime);
    if (totalDuration <= kEpsilon) {
        return;
    }

    TraversalState state;
    initTraversalState(
        state,
        clippedStartLon,
        clippedStartLat,
        clippedStartTime,
        clippedEndLon,
        clippedEndLat,
        clippedEndTime,
        request,
        bucketSeconds,
        bucketCount,
        columnCount,
        rowCount,
        lonStep,
        latStep);

    while (state.currentT < 1.0 - 1e-12) {
        double nextEventT = 1.0;
        nextEventT = std::min(nextEventT, state.nextGridXT);
        nextEventT = std::min(nextEventT, state.nextGridYT);
        nextEventT = std::min(nextEventT, state.nextBucketT);
        nextEventT = clamp01(nextEventT);

        if (nextEventT <= state.currentT + 1e-12) {
            advanceTraversalState(state, nextEventT, bucketCount, columnCount, rowCount);
            if (state.currentT >= 1.0 - 1e-12) {
                break;
            }
            continue;
        }

        if (state.gx >= 0 && state.gx < columnCount &&
            state.gy >= 0 && state.gy < rowCount &&
            state.bucketIndex >= 0 && state.bucketIndex < bucketCount) {

            const double subDuration = totalDuration * (nextEventT - state.currentT);
            if (subDuration > kEpsilon) {
                const std::size_t idx =
                    flatIndex(state.bucketIndex, state.gx, state.gy, columnCount, rowCount);
                vehicleSeconds[idx] += static_cast<float>(subDuration);
            }
        }

        advanceTraversalState(state, nextEventT, bucketCount, columnCount, rowCount);
    }
}

} // namespace

DensityAnalysisResult DensityAnalyzer::analyze(const DensityAnalysisRequest& request) {
    DensityAnalysisResult result;

    if (!validateRequest(request, result.errorMessage)) {
        return result;
    }

    const auto startClock = std::chrono::steady_clock::now();

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

    const int columnCount = std::max(
        1, static_cast<int>(std::ceil((request.maxLon - request.minLon) / lonStep)));
    const int rowCount = std::max(
        1, static_cast<int>(std::ceil((request.maxLat - request.minLat) / latStep)));

    const long long gridCount =
        static_cast<long long>(columnCount) * static_cast<long long>(rowCount);
    const long long analysisScale =
        static_cast<long long>(bucketCount) * gridCount;

    if (gridCount <= 0 || analysisScale <= 0) {
        result.errorMessage = "analysis scale invalid";
        return result;
    }

    result.lonStep = lonStep;
    result.latStep = latStep;
    result.cellAreaKm2 = estimateCellAreaKm2(lonStep, latStep, centerLat);
    result.columnCount = columnCount;
    result.rowCount = rowCount;
    result.bucketCount = bucketCount;
    result.gridCount = gridCount;
    result.analysisScale = analysisScale;

    const std::vector<GPSPoint>& allPoints = DataManager::getAllPoints();
    const std::unordered_map<int, VehicleRange>& idToRange = DataManager::getIdToRange();

    const std::size_t totalVehicles = idToRange.size();
    std::size_t processedVehicles = 0;
    const std::size_t printStep = std::max<std::size_t>(1, totalVehicles / 100);
    std::size_t nextPrintThreshold = printStep;
    const auto progressStartTime = std::chrono::steady_clock::now();

    std::vector<float> vehicleSeconds(
        static_cast<std::size_t>(analysisScale), 0.0f);

    long long totalContributingSegments = 0;
    int totalTouchedVehicles = 0;

    for (const auto& [taxiId, range] : idToRange) {
        (void)taxiId;

        if (range.start < 0 || range.end <= range.start ||
            range.end >= static_cast<int>(allPoints.size())) {
            ++processedVehicles;
            goto maybe_print_progress;
        }

        if (allPoints[range.end].timestamp < request.startTime ||
            allPoints[range.start].timestamp > request.endTime) {
            ++processedVehicles;
            goto maybe_print_progress;
        }

        {
            const int segStart = findVehicleSegmentStartIndex(allPoints, range, request.startTime);
            const int segEnd = findVehicleSegmentEndIndex(allPoints, range, request.endTime);

            if (segStart < segEnd) {
                bool vehicleTouched = false;

                for (int i = segStart; i < segEnd; ++i) {
                    const GPSPoint& p0 = allPoints[static_cast<std::size_t>(i)];
                    const GPSPoint& p1 = allPoints[static_cast<std::size_t>(i + 1)];

                    if (p0.id != p1.id) {
                        continue;
                    }

                    if (p1.timestamp <= p0.timestamp) {
                        continue;
                    }

                    if (p0.timestamp > request.endTime) {
                        break;
                    }
                    if (p1.timestamp < request.startTime) {
                        continue;
                    }

                    const long long rawStartTime = p0.timestamp;
                    const long long rawEndTime = p1.timestamp;
                    const long long rawDuration = rawEndTime - rawStartTime;

                    if (rawDuration > kMaxInterpolationGapSeconds) {
                        continue;
                    }

                    const double distMeters = distanceMeters(p0, p1);
                    const double speed = distMeters / std::max(1.0, static_cast<double>(rawDuration));
                    if (speed > kMaxSpeedMetersPerSecond) {
                        continue;
                    }

                    const long long clippedTimeStart = std::max(rawStartTime, request.startTime);
                    const long long clippedTimeEnd = std::min(rawEndTime, request.endTime);
                    if (clippedTimeEnd <= clippedTimeStart) {
                        continue;
                    }

                    const double fullDuration = static_cast<double>(rawDuration);
                    const double timeT0 =
                        static_cast<double>(clippedTimeStart - rawStartTime) / fullDuration;
                    const double timeT1 =
                        static_cast<double>(clippedTimeEnd - rawStartTime) / fullDuration;

                    const double timeStartLon = lerp(p0.lon, p1.lon, timeT0);
                    const double timeStartLat = lerp(p0.lat, p1.lat, timeT0);
                    const double timeEndLon = lerp(p0.lon, p1.lon, timeT1);
                    const double timeEndLat = lerp(p0.lat, p1.lat, timeT1);

                    double rectEnter = 0.0;
                    double rectExit = 1.0;
                    if (!clipSegmentToRect(timeStartLon, timeStartLat,
                                           timeEndLon, timeEndLat,
                                           request.minLon, request.maxLon,
                                           request.minLat, request.maxLat,
                                           rectEnter, rectExit)) {
                        continue;
                    }

                    const long long segDuration = clippedTimeEnd - clippedTimeStart;
                    if (segDuration <= 0) {
                        continue;
                    }

                    const long long finalStartTime =
                        clippedTimeStart +
                        static_cast<long long>(std::llround(rectEnter * static_cast<double>(segDuration)));
                    const long long finalEndTime =
                        clippedTimeStart +
                        static_cast<long long>(std::llround(rectExit * static_cast<double>(segDuration)));

                    if (finalEndTime <= finalStartTime) {
                        continue;
                    }

                    const double finalStartLon = lerp(timeStartLon, timeEndLon, rectEnter);
                    const double finalStartLat = lerp(timeStartLat, timeEndLat, rectEnter);
                    const double finalEndLon = lerp(timeStartLon, timeEndLon, rectExit);
                    const double finalEndLat = lerp(timeStartLat, timeEndLat, rectExit);

                    accumulateSegmentToDenseArray(
                        finalStartTime,
                        finalEndTime,
                        finalStartLon,
                        finalStartLat,
                        finalEndLon,
                        finalEndLat,
                        request,
                        bucketSeconds,
                        bucketCount,
                        columnCount,
                        rowCount,
                        lonStep,
                        latStep,
                        vehicleSeconds);

                    vehicleTouched = true;
                    ++totalContributingSegments;
                }

                if (vehicleTouched) {
                    ++totalTouchedVehicles;
                }
            }
        }

        ++processedVehicles;

maybe_print_progress:
        if (processedVehicles >= nextPrintThreshold || processedVehicles == totalVehicles) {
            nextPrintThreshold += printStep;

            const double progress = totalVehicles == 0 ? 1.0 :
                static_cast<double>(processedVehicles) / static_cast<double>(totalVehicles);

            const auto now = std::chrono::steady_clock::now();
            const double elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - progressStartTime).count() / 1000.0;

            const double speed = processedVehicles / std::max(1e-6, elapsed);
            const double eta = (totalVehicles - processedVehicles) / std::max(1e-6, speed);

            Debug() << "[进度] "
                      << processedVehicles << "/" << totalVehicles
                      << " (" << static_cast<int>(progress * 100.0) << "%)"
                      << " | 已用: " << elapsed << "s"
                      << " | 速度: " << static_cast<int>(speed) << " 辆/s"
                      << " | 剩余: " << static_cast<int>(eta) << "s";
        }
    }

    result.totalVehicleCount = totalTouchedVehicles;
    result.totalPointCount = totalContributingSegments;
    result.maxVehicleDensity = 0.0;

    const double bucketSecondsDouble = std::max(1.0, static_cast<double>(bucketSeconds));
    const double invArea = 1.0 / std::max(kEpsilon, result.cellAreaKm2);

    for (std::size_t i = 0; i < vehicleSeconds.size(); ++i) {
        if (vehicleSeconds[i] <= 0.0f) {
            continue;
        }
        const double density =
            (static_cast<double>(vehicleSeconds[i]) / bucketSecondsDouble) * invArea;
        if (density > result.maxVehicleDensity) {
            result.maxVehicleDensity = density;
        }
    }

    result.vehicleSeconds = std::move(vehicleSeconds);
    result.elapsedSeconds =
        static_cast<double>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - startClock).count()) / 1000.0;
    result.success = true;
    return result;
}
#ifndef DENSITYANALYSIS_H
#define DENSITYANALYSIS_H

#include <QString>
#include <vector>

// 密度分析请求参数。
// 约定：
// 1) 空间范围使用经纬度矩形（min <= value <= max）。
// 2) 时间范围使用 Unix 秒级时间戳（start <= timestamp <= end）。
// 3) intervalMinutes 为时间分桶粒度（分钟）。
// 4) cellSizeMeters 为网格边长（米）。
struct DensityAnalysisRequest {
    double minLon = 0.0;
    double minLat = 0.0;
    double maxLon = 0.0;
    double maxLat = 0.0;
    long long startTime = 0;
    long long endTime = 0;
    int intervalMinutes = 30;
    double cellSizeMeters = 500.0;
};

// 单个网格在某个时间桶中的统计结果。
// 注意：仅返回网格索引(gx, gy)与统计指标。
// 网格边界由前端根据 minLon/minLat/lonStep/latStep 反推，
// 这样可以减少接口返回体积。
struct DensityGridCell {
    int gx = 0;
    int gy = 0;
    int pointCount = 0;
    int vehicleCount = 0;

    // 车辆密度：去重车辆数 / km^2。
    double vehicleDensity = 0.0;
    // 流强度：GPS 点数 / km^2。
    double flowIntensity = 0.0;

    // 相对上一时间桶同一网格的变化量。
    int deltaVehicleCount = 0;
    double deltaVehicleDensity = 0.0;
    double deltaRate = 0.0;
};

// 单个时间桶的汇总统计。
struct DensityTimeBucket {
    long long startTime = 0;
    long long endTime = 0;
    int maxVehicleCount = 0;
    double maxVehicleDensity = 0.0;
    double avgVehicleDensity = 0.0;
    long long totalPointCount = 0;
    int totalVehicleCount = 0;
    double totalFlowDensity = 0.0;
    double deltaRate = 0.0;
    std::vector<DensityGridCell> cells;
};

// 整体分析结果。
struct DensityAnalysisResult {
    bool success = false;
    QString errorMessage;

    long long totalPointCount = 0;
    int totalVehicleCount = 0;
    double elapsedSeconds = 0.0;

    // 网格基础信息（前后端字段约定）。
    double lonStep = 0.0;
    double latStep = 0.0;
    double cellAreaKm2 = 0.0;
    int columnCount = 0;
    int rowCount = 0;
    double maxVehicleDensity = 0.0;

    // 规模信息（联调与性能排查使用）。
    int bucketCount = 0;
    long long gridCount = 0;
    long long analysisScale = 0;

    std::vector<DensityTimeBucket> buckets;
};

class DensityAnalyzer {
public:
    static DensityAnalysisResult analyze(const DensityAnalysisRequest& request);
};

#endif // DENSITYANALYSIS_H

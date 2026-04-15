#ifndef DENSITYANALYSIS_H
#define DENSITYANALYSIS_H

#include <string>
#include <vector>

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

struct DensityAnalysisResult {
    bool success = false;
    std::string errorMessage;

    double lonStep = 0.0;
    double latStep = 0.0;
    double cellAreaKm2 = 0.0;

    int columnCount = 0;
    int rowCount = 0;
    int bucketCount = 0;

    long long gridCount = 0;
    long long analysisScale = 0;

    double maxVehicleDensity = 0.0;

    long long totalPointCount = 0;
    int totalVehicleCount = 0;

    double elapsedSeconds = 0.0;

    // 压平三维数组: [bucket][gy][gx]，每个值是该 bucket-cell 的累计停留秒数
    std::vector<float> vehicleSeconds;
};

class DensityAnalyzer {
public:
    static DensityAnalysisResult analyze(const DensityAnalysisRequest& request);
};

#endif // DENSITYANALYSIS_H
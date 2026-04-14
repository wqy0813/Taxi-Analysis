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

struct DensityGridCell {
    int gx = 0;
    int gy = 0;
    int pointCount = 0;
    int vehicleCount = 0;
    double vehicleDensity = 0.0;
    double flowIntensity = 0.0;
    int deltaVehicleCount = 0;
    double deltaVehicleDensity = 0.0;
    double deltaRate = 0.0;
};

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

struct DensityAnalysisResult {
    bool success = false;
    std::string errorMessage;

    long long totalPointCount = 0;
    int totalVehicleCount = 0;
    double elapsedSeconds = 0.0;

    double lonStep = 0.0;
    double latStep = 0.0;
    double cellAreaKm2 = 0.0;
    int columnCount = 0;
    int rowCount = 0;
    double maxVehicleDensity = 0.0;

    int bucketCount = 0;
    long long gridCount = 0;
    long long analysisScale = 0;

    std::vector<DensityTimeBucket> buckets;
};

class DensityAnalyzer {
public:
    static DensityAnalysisResult analyze(const DensityAnalysisRequest& request);
};

#endif

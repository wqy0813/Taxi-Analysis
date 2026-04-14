#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <cstdint>
#include <string>
#include <vector>

#include "datamanager.h"

struct sqlite3;

class DatabaseManager {
public:
    explicit DatabaseManager(const std::string& dbName = "taxi_data.db");
    ~DatabaseManager();

    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;

    bool open();
    bool batchInsert(const std::vector<GPSPoint>& points);
    std::int64_t getPointCount();
    std::vector<GPSPoint> getTrajectoryByTaxiId(int taxiId);
    std::vector<GPSPoint> getAllPointsForDisplay(int maxPoints);
    std::int64_t countUniqueTaxisInBoundsAndTime(std::int64_t startTime,
                                                 std::int64_t endTime,
                                                 double minLon,
                                                 double minLat,
                                                 double maxLon,
                                                 double maxLat);
    bool getDatasetBounds(std::int64_t& minTime,
                          std::int64_t& maxTime,
                          double& minLon,
                          double& minLat,
                          double& maxLon,
                          double& maxLat);
    static void checkAndImportData(DatabaseManager& dbm, const AppConfig& config);

    sqlite3* getRawHandle() const;

private:
    sqlite3* db = nullptr;
    std::string dbPath;
};

#endif

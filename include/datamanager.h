#ifndef DATAMANAGER_H
#define DATAMANAGER_H

#include <memory>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "appconfig.h"
#include "quadtree.h"

class DatabaseManager;

struct GPSPoint {
    int id = 0;
    long long timestamp = 0;
    double lon = 0.0;
    double lat = 0.0;
};

struct VehicleRange {
    int start = 0;
    int end = -1;
};

struct ClusterPoint {
    double lon = 0.0;
    double lat = 0.0;
    int count = 0;
    bool isCluster = false;

    double minLon = 0.0;
    double minLat = 0.0;
    double maxLon = 0.0;
    double maxLat = 0.0;

    std::vector<GPSPoint> children;
};

class DataManager {
public:
    static void loadTxtFiles(const AppConfig& config);
    static bool loadFromDatabase(DatabaseManager& dbm);
    static bool loadAllPoints(DatabaseManager& dbm);
    static const std::vector<GPSPoint>& getAllPoints() { return allPoints; }

    static void buildQuadTree(const AppConfig& config);
    static bool hasQuadTree();

    static std::vector<GPSPoint> querySpatial(double minLon, double minLat,
                                              double maxLon, double maxLat);

    static std::vector<GPSPoint> querySpatialAndTime(double minLon, double minLat,
                                                     double maxLon, double maxLat,
                                                     long long minTimeStamp, long long maxTimeStamp);

    static std::unordered_set<int> querySpatioTemporalUniqueIds(double minLon, double minLat,
                                                                double maxLon, double maxLat,
                                                                long long minTimeStamp, long long maxTimeStamp);

    static std::vector<ClusterPoint> clusterPointsForView(const std::vector<GPSPoint>& points,
                                                          double minLon, double minLat,
                                                          double maxLon, double maxLat,
                                                          int zoom);

    static std::set<const QuadNode*> exceptionalNodes;

    static int getUniqueCountById(const std::vector<GPSPoint>& points);
    static std::vector<GPSPoint> getPointsRangeById(int id);

private:
    static std::unordered_map<int, VehicleRange> idToRange;
    static std::vector<GPSPoint> allPoints;
    static std::unique_ptr<QuadNode> quadTreeRoot;
};

#endif

#ifndef DATAMANAGER_H
#define DATAMANAGER_H

#include <QString>
#include <vector>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include "appconfig.h"
#include "quadtree.h"
#include <set>

class DatabaseManager;

struct GPSPoint {
    int id;
    long long timestamp;
    double lon;
    double lat;
};

struct VehicleRange {
    int start;
    int end;
};

struct ClusterPoint {
    double lon;
    double lat;
    int count;
    bool isCluster;

    double minLon;
    double minLat;
    double maxLon;
    double maxLat;

    std::vector<GPSPoint> children; // 只有 count <= 100 时才填充
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
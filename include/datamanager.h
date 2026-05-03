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

// 新增：双向流量桶
struct FlowBucket {
    long long bucketStart = 0;
    double aToB = 0.0;
    double bToA = 0.0;
};

//新增：区域矩形（用于F6功能）
struct RegionRect {
    double minLon = 0.0;
    double minLat = 0.0;
    double maxLon = 0.0;
    double maxLat = 0.0;
};

struct SingleRegionFlowBucket {
    long long bucketStart = 0;
    double incoming = 0.0; // 其他区域 -> 目标区域
    double outgoing = 0.0; // 目标区域 -> 其他区域
};

class DataManager {
public:
    static void loadTxtFiles(const AppConfig& config);
    static bool loadFromDatabase(DatabaseManager& dbm);
    static bool loadAllPoints(DatabaseManager& dbm);
    static const std::vector<GPSPoint>& getAllPoints() { return allPoints; }
    static const std::unordered_map<int, VehicleRange>& getIdToRange() { return idToRange; }
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

    // 新增：双向区域关联流量统计
    // tStart: 起始时间
    // bucketSize: 桶大小（例如 3600 秒）
    // bucketCount: 桶数量（例如 48）
    // deltaT: 最大允许通行时间（秒）
    static std::vector<FlowBucket> queryBidirectionalFlow(
        double minLonA, double minLatA,
        double maxLonA, double maxLatA,
        double minLonB, double minLatB,
        double maxLonB, double maxLatB,
        long long tStart,
        long long bucketSize,
        int bucketCount,
        long long deltaT);

    //新增：单区域关联流量统计（F6功能）
        static std::vector<SingleRegionFlowBucket> querySingleRegionFlow(
        double targetMinLon, double targetMinLat,
        double targetMaxLon, double targetMaxLat,
        double globalMinLon, double globalMinLat,
        double globalMaxLon, double globalMaxLat,
        long long tStart,
        long long bucketSize,
        int bucketCount,
        long long deltaT);


private:
    static std::unordered_map<int, VehicleRange> idToRange;
    static std::vector<GPSPoint> allPoints;
    static std::unique_ptr<QuadNode> quadTreeRoot;
};

#endif
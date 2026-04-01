#ifndef DATAMANAGER_H
#define DATAMANAGER_H

#include <QString>
#include <vector>
#include <memory>
#include "appconfig.h"
#include "quadtree.h"
#include<set>

class DatabaseManager;
struct GPSPoint {
    int id;
    long long timestamp;
    double lon;
    double lat;
};

class DataManager {
public:
    static void loadTxtFiles(const AppConfig& config);
    static bool loadFromDatabase(DatabaseManager& dbm);

    static const std::vector<GPSPoint>& getAllPoints() { return allPoints; }
    // 新增：建立四叉树
    static void buildQuadTree(const AppConfig& config);

    // 新增：判断四叉树是否已建立
    static bool hasQuadTree();

    // 新增：区域查询，返回命中的点
    static std::vector<GPSPoint> querySpatial(double minLon, double minLat,
                                            double maxLon, double maxLat);
    static std::vector<GPSPoint> querySpatialAndTime(double minLon, double minLat,
                                              double maxLon, double maxLat,long long minTimeStamp,long long maxTimeStamp);
    static std::unordered_set<int> querySpatioTemporalUniqueIds(double minLon, double minLat,
                                                     double maxLon, double maxLat,long long minTimeStamp,long long maxTimeStamp);
    static std::set<const QuadNode*> exceptionalNodes;
    static int getUniqueCountById(const std::vector<GPSPoint>& points);
private:

    static std::vector<GPSPoint> allPoints;
    static std::unique_ptr<QuadNode> quadTreeRoot;
};

#endif
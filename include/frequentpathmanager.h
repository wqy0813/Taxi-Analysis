#ifndef FREQUENTPATHMANAGER_H
#define FREQUENTPATHMANAGER_H

#include <string>
#include <vector>

struct FrequentPathPoint {
    double lon = 0.0;
    double lat = 0.0;
};

struct FrequentPathRecord {
    int rank = 0;
    int frequency = 0;
    double lengthMeters = 0.0;
    int cellCount = 0;
    std::vector<FrequentPathPoint> points;
};

struct FrequentPathQuery {
    int k = 10;
    double minLengthMeters = 0.0;
    std::string dbPath;
};

struct FrequentPathRegionQuery {
    int k = 10;
    double minLengthMeters = 0.0;
    std::string dbPath;

    double minLonA = 0.0;
    double minLatA = 0.0;
    double maxLonA = 0.0;
    double maxLatA = 0.0;

    double minLonB = 0.0;
    double minLatB = 0.0;
    double maxLonB = 0.0;
    double maxLatB = 0.0;
};

class FrequentPathManager {
public:
    static std::vector<FrequentPathRecord> queryTopK(const FrequentPathQuery& query);
    static std::vector<FrequentPathRecord> queryTopKBetweenRegions(const FrequentPathRegionQuery& query);
};

#endif

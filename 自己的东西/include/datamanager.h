#ifndef DATAMANAGER_H
#define DATAMANAGER_H

#include <QString>
#include <vector>
#include "appconfig.h"

struct GPSPoint {
    int id;
    long long timestamp;
    double lon;
    double lat;
};

class DataManager {
public:
    DataManager();

    // 改成直接吃配置
    void loadTxtFiles(const AppConfig& config);

    const std::vector<GPSPoint>& getAllPoints() const { return allPoints; }

private:
    std::vector<GPSPoint> allPoints;
};

#endif
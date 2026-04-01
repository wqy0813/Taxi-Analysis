#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QSqlDatabase>
#include <QString>
#include <QtGlobal>
#include <vector>
#include "datamanager.h"

class DatabaseManager {
public:
    explicit DatabaseManager(const QString &dbName = "taxi_data.db");
    ~DatabaseManager();

    bool open();
    bool batchInsert(const std::vector<GPSPoint>& points);
    qint64 getPointCount();
    bool loadAllPoints(std::vector<GPSPoint>& points);
    std::vector<GPSPoint> getTrajectoryByTaxiId(int taxiId);
    std::vector<GPSPoint> getAllPointsForDisplay(int maxPoints);
    qint64 countUniqueTaxisInBoundsAndTime(qint64 startTime,
                                           qint64 endTime,
                                           double minLon,
                                           double minLat,
                                           double maxLon,
                                           double maxLat);
    bool getDatasetBounds(qint64 &minTime,
                          qint64 &maxTime,
                          double &minLon,
                          double &minLat,
                          double &maxLon,
                          double &maxLat);
    static void checkAndImportData(DatabaseManager &dbm, const AppConfig& config);

private:
    QSqlDatabase db;
    QString connectionName;
};

#endif // DATABASEMANAGER_H
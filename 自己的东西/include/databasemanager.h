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
private:
    QSqlDatabase db;
};

#endif // DATABASEMANAGER_H
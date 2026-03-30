#include "databasemanager.h"
#include <QDir>
#include <QFileInfo>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>

DatabaseManager::DatabaseManager(const QString &dbName) {
    db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName(dbName);
}

DatabaseManager::~DatabaseManager() {
    if (db.isOpen()) {
        db.close();
    }
}

bool DatabaseManager::open() {
    const QFileInfo dbInfo(db.databaseName());
    const QString dbDirPath = dbInfo.absolutePath();
    if (!dbDirPath.isEmpty() && !QDir(dbDirPath).exists() && !QDir().mkpath(dbDirPath)) {
        qDebug() << "Database directory creation failed:" << dbDirPath;
        return false;
    }

    if (!db.open()) {
        qDebug() << "数据库打开失败:" << db.lastError().text();
        return false;
    }

    QSqlQuery query;

    // 导入阶段的性能优化
    query.exec("PRAGMA synchronous = OFF");
    query.exec("PRAGMA journal_mode = MEMORY");

    if (!query.exec("CREATE TABLE IF NOT EXISTS taxi_points ("
                    "id INTEGER, "
                    "time INTEGER, "
                    "lon REAL, "
                    "lat REAL)")) {
        qDebug() << "建表失败:" << query.lastError().text();
        return false;
    }

    return true;
}

bool DatabaseManager::batchInsert(const std::vector<GPSPoint>& points) {
    if (points.empty()) {
        return true;
    }

    if (!db.transaction()) {
        qDebug() << "开启事务失败:" << db.lastError().text();
        return false;
    }

    QSqlQuery query;
    query.prepare("INSERT INTO taxi_points (id, time, lon, lat) VALUES (?, ?, ?, ?)");

    for (const auto& p : points) {
        query.addBindValue(p.id);
        query.addBindValue(static_cast<qlonglong>(p.timestamp));
        query.addBindValue(p.lon);
        query.addBindValue(p.lat);

        if (!query.exec()) {
            qDebug() << "插入失败:" << query.lastError().text();
            db.rollback();
            return false;
        }
    }

    if (!db.commit()) {
        qDebug() << "提交事务失败:" << db.lastError().text();
        return false;
    }

    return true;
}

qint64 DatabaseManager::getPointCount() {
    if (!db.isOpen()) {
        qDebug() << "数据库未打开，无法统计点数";
        return 0;
    }

    QSqlQuery query;
    if (!query.exec("SELECT COUNT(*) FROM taxi_points")) {
        qDebug() << "统计点数失败:" << query.lastError().text();
        return 0;
    }

    if (query.next()) {
        return query.value(0).toLongLong();
    }

    return 0;
}
bool DatabaseManager::loadAllPoints(std::vector<GPSPoint>& points) {
    points.clear();

    if (!db.isOpen()) {
        qDebug() << "数据库未打开，无法加载点数据";
        return false;
    }

    qint64 totalCount = getPointCount();
    if (totalCount <= 0) {
        qDebug() << "数据库中没有点数据";
        return true;
    }

    // 预分配，减少 vector 扩容次数
    points.reserve(static_cast<size_t>(totalCount));

    QSqlQuery query;
    query.setForwardOnly(true);  // 大数据量时更省内存

    if (!query.exec("SELECT id, time, lon, lat FROM taxi_points")) {
        qDebug() << "读取点数据失败:" << query.lastError().text();
        return false;
    }

    qint64 loadedCount = 0;

    while (query.next()) {
        GPSPoint p;
        p.id = query.value(0).toInt();
        p.timestamp = query.value(1).toLongLong();
        p.lon = query.value(2).toDouble();
        p.lat = query.value(3).toDouble();

        points.push_back(p);
        ++loadedCount;

        if (loadedCount % 1000000 == 0) {
            qDebug() << "已加载" << loadedCount << "个点到内存...";
        }
    }

    qDebug() << "全部点加载完成，共" << loadedCount << "个点";
    return true;
}
std::vector<GPSPoint> DatabaseManager::getTrajectoryByTaxiId(int taxiId) {
    std::vector<GPSPoint> points;

    if (!db.isOpen()) {
        qDebug() << "Database is not open, cannot query trajectory.";
        return points;
    }

    QSqlQuery query(db);
    query.prepare("SELECT id, time, lon, lat "
                  "FROM taxi_points "
                  "WHERE id = ? "
                  "ORDER BY time ASC");
    query.addBindValue(taxiId);

    if (!query.exec()) {
        qDebug() << "Failed to query trajectory:" << query.lastError().text();
        return points;
    }

    while (query.next()) {
        GPSPoint point;
        point.id = query.value(0).toInt();
        point.timestamp = query.value(1).toLongLong();
        point.lon = query.value(2).toDouble();
        point.lat = query.value(3).toDouble();
        points.push_back(point);
    }

    return points;
}

std::vector<GPSPoint> DatabaseManager::getAllPointsForDisplay(int maxPoints) {
    std::vector<GPSPoint> points;

    if (!db.isOpen()) {
        qDebug() << "Database is not open, cannot query all points.";
        return points;
    }

    if (maxPoints <= 0) {
        return points;
    }

    const qint64 totalCount = getPointCount();
    if (totalCount <= 0) {
        return points;
    }

    const qint64 step = std::max<qint64>(1, (totalCount + maxPoints - 1) / maxPoints);

    QSqlQuery query(db);
    query.prepare("SELECT id, time, lon, lat "
                  "FROM taxi_points "
                  "WHERE (? = 1 OR rowid % ? = 0) "
                  "LIMIT ?");
    query.addBindValue(step);
    query.addBindValue(step);
    query.addBindValue(maxPoints);

    if (!query.exec()) {
        qDebug() << "Failed to query all points for display:" << query.lastError().text();
        return points;
    }

    points.reserve(maxPoints);
    while (query.next()) {
        GPSPoint point;
        point.id = query.value(0).toInt();
        point.timestamp = query.value(1).toLongLong();
        point.lon = query.value(2).toDouble();
        point.lat = query.value(3).toDouble();
        points.push_back(point);
    }

    return points;
}

qint64 DatabaseManager::countUniqueTaxisInBoundsAndTime(qint64 startTime,
                                                        qint64 endTime,
                                                        double minLon,
                                                        double minLat,
                                                        double maxLon,
                                                        double maxLat) {
    if (!db.isOpen()) {
        qDebug() << "Database is not open, cannot count taxis in bounds.";
        return -1;
    }

    qint64 datasetMinTime = 0;
    qint64 datasetMaxTime = 0;
    double datasetMinLon = 0.0;
    double datasetMinLat = 0.0;
    double datasetMaxLon = 0.0;
    double datasetMaxLat = 0.0;
    const bool hasValidBounds = getDatasetBounds(datasetMinTime, datasetMaxTime,
                                                 datasetMinLon, datasetMinLat,
                                                 datasetMaxLon, datasetMaxLat);
    const qint64 reasonableEpochStart = 946684800; // 2000-01-01 00:00:00
    const bool timeRangeLooksInvalid =
        !hasValidBounds || datasetMaxTime < reasonableEpochStart || datasetMinTime > datasetMaxTime;

    QSqlQuery query(db);
    if (timeRangeLooksInvalid) {
        query.prepare("SELECT COUNT(DISTINCT id) "
                      "FROM taxi_points "
                      "WHERE lon >= ? AND lon <= ? "
                      "AND lat >= ? AND lat <= ?");
        query.addBindValue(minLon);
        query.addBindValue(maxLon);
        query.addBindValue(minLat);
        query.addBindValue(maxLat);
    } else {
        query.prepare("SELECT COUNT(DISTINCT id) "
                      "FROM taxi_points "
                      "WHERE time >= ? AND time <= ? "
                      "AND lon >= ? AND lon <= ? "
                      "AND lat >= ? AND lat <= ?");
        query.addBindValue(startTime);
        query.addBindValue(endTime);
        query.addBindValue(minLon);
        query.addBindValue(maxLon);
        query.addBindValue(minLat);
        query.addBindValue(maxLat);
    }

    if (!query.exec()) {
        qDebug() << "Failed to count taxis in bounds:" << query.lastError().text();
        return -1;
    }

    if (query.next()) {
        return query.value(0).toLongLong();
    }

    return 0;
}

bool DatabaseManager::getDatasetBounds(qint64 &minTime,
                                       qint64 &maxTime,
                                       double &minLon,
                                       double &minLat,
                                       double &maxLon,
                                       double &maxLat) {
    if (!db.isOpen()) {
        qDebug() << "Database is not open, cannot get dataset bounds.";
        return false;
    }

    QSqlQuery query(db);
    if (!query.exec("SELECT MIN(time), MAX(time), MIN(lon), MIN(lat), MAX(lon), MAX(lat) "
                    "FROM taxi_points")) {
        qDebug() << "Failed to query dataset bounds:" << query.lastError().text();
        return false;
    }

    if (!query.next() || query.isNull(0) || query.isNull(1)) {
        return false;
    }

    minTime = query.value(0).toLongLong();
    maxTime = query.value(1).toLongLong();
    minLon = query.value(2).toDouble();
    minLat = query.value(3).toDouble();
    maxLon = query.value(4).toDouble();
    maxLat = query.value(5).toDouble();
    return true;
}

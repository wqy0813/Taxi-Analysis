#include "databasemanager.h"
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
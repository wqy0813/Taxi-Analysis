#include "databasemanager.h"

#include <algorithm>
#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

DatabaseManager::DatabaseManager(const QString &dbName, const QString &connectionName)
    : connectionName(connectionName.isEmpty() ? QStringLiteral("TaxiDataAnalysisConnection")
                                              : connectionName) {
    if (QSqlDatabase::contains(this->connectionName)) {
        db = QSqlDatabase::database(this->connectionName);
    } else {
        db = QSqlDatabase::addDatabase("QSQLITE", this->connectionName);
    }
    db.setDatabaseName(dbName);
}

DatabaseManager::~DatabaseManager() {
    if (db.isOpen()) {
        db.close();
    }

    const QString name = connectionName;
    db = QSqlDatabase();
    if (!name.isEmpty() && QSqlDatabase::contains(name)) {
        QSqlDatabase::removeDatabase(name);
    }
}

bool DatabaseManager::open() {
    if (!db.open()) {
        qDebug() << "Failed to open database:" << db.lastError().text();
        return false;
    }

    QSqlQuery query(db);

    query.exec("PRAGMA synchronous = OFF");
    query.exec("PRAGMA journal_mode = MEMORY");

    if (!query.exec("CREATE TABLE IF NOT EXISTS taxi_points ("
                    "id INTEGER, "
                    "time INTEGER, "
                    "lon REAL, "
                    "lat REAL)")) {
        qDebug() << "Failed to create taxi_points table:" << query.lastError().text();
        return false;
    }

    if (!query.exec("CREATE INDEX IF NOT EXISTS idx_taxi_points_id_time "
                    "ON taxi_points(id, time)")) {
        qDebug() << "Failed to create idx_taxi_points_id_time:" << query.lastError().text();
    }

    return true;
}

bool DatabaseManager::batchInsert(const std::vector<GPSPoint>& points) {
    if (points.empty()) {
        return true;
    }

    if (!db.transaction()) {
        qDebug() << "Failed to start transaction:" << db.lastError().text();
        return false;
    }

    QSqlQuery query(db);
    query.prepare("INSERT INTO taxi_points (id, time, lon, lat) VALUES (?, ?, ?, ?)");

    for (const auto& p : points) {
        query.addBindValue(p.id);
        query.addBindValue(static_cast<qlonglong>(p.timestamp));
        query.addBindValue(p.lon);
        query.addBindValue(p.lat);

        if (!query.exec()) {
            qDebug() << "Failed to insert point:" << query.lastError().text();
            db.rollback();
            return false;
        }
    }

    if (!db.commit()) {
        qDebug() << "Failed to commit transaction:" << db.lastError().text();
        return false;
    }

    return true;
}

qint64 DatabaseManager::getPointCount() {
    if (!db.isOpen()) {
        qDebug() << "Database is not open, cannot count points.";
        return 0;
    }

    QSqlQuery query(db);
    if (!query.exec("SELECT COUNT(*) FROM taxi_points")) {
        qDebug() << "Failed to count points:" << query.lastError().text();
        return 0;
    }

    if (query.next()) {
        return query.value(0).toLongLong();
    }

    return 0;
}



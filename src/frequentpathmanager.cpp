#include "frequentpathmanager.h"

#include <algorithm>
#include <filesystem>
#include <stdexcept>

#include <sqlite3.h>

#include "json.hpp"

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

std::vector<FrequentPathPoint> parsePointsJson(const std::string& text) {
    std::vector<FrequentPathPoint> points;

    const json arr = json::parse(text);
    if (!arr.is_array()) {
        return points;
    }

    points.reserve(arr.size());
    for (const auto& item : arr) {
        if (!item.is_object()) {
            continue;
        }
        FrequentPathPoint point{};
        point.lon = item.value("lon", 0.0);
        point.lat = item.value("lat", 0.0);
        points.push_back(point);
    }

    return points;
}

void throwSqliteError(sqlite3* db, const std::string& prefix) {
    throw std::runtime_error(prefix + ": " + (db ? sqlite3_errmsg(db) : "unknown sqlite error"));
}

void validateDbPath(const std::string& dbPath) {
    if (dbPath.empty()) {
        throw std::runtime_error("frequent path database path is empty");
    }
    if (!fs::exists(dbPath)) {
        throw std::runtime_error("frequent path database not found: " + dbPath);
    }
}

sqlite3* openReadOnlyDatabase(const std::string& dbPath) {
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(dbPath.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        std::string message = "failed to open frequent path database";
        if (db != nullptr) {
            message += ": ";
            message += sqlite3_errmsg(db);
            sqlite3_close(db);
        }
        throw std::runtime_error(message);
    }
    return db;
}

FrequentPathRecord readRecord(sqlite3_stmt* stmt, int rank) {
    FrequentPathRecord record{};
    record.rank = rank;
    record.frequency = sqlite3_column_int(stmt, 0);
    record.lengthMeters = sqlite3_column_double(stmt, 1);
    record.cellCount = sqlite3_column_int(stmt, 2);

    const unsigned char* text = sqlite3_column_text(stmt, 3);
    if (text != nullptr) {
        record.points = parsePointsJson(reinterpret_cast<const char*>(text));
    }

    return record;
}

} // namespace

std::vector<FrequentPathRecord> FrequentPathManager::queryTopK(const FrequentPathQuery& query) {
    if (query.k <= 0) {
        return {};
    }
    validateDbPath(query.dbPath);
    sqlite3* db = openReadOnlyDatabase(query.dbPath);

    const char* sql =
        "SELECT frequency, length_meters, cell_count, points_json "
        "FROM frequent_paths "
        "WHERE length_meters > ? "
        "ORDER BY frequency DESC, length_meters DESC "
        "LIMIT ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throwSqliteError(db, "failed to prepare frequent path query");
    }

    sqlite3_bind_double(stmt, 1, std::max(0.0, query.minLengthMeters));
    sqlite3_bind_int(stmt, 2, query.k);

    std::vector<FrequentPathRecord> records;
    int rank = 1;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        records.push_back(readRecord(stmt, rank++));
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    return records;
}

std::vector<FrequentPathRecord> FrequentPathManager::queryTopKBetweenRegions(
    const FrequentPathRegionQuery& query) {
    if (query.k <= 0) {
        return {};
    }
    validateDbPath(query.dbPath);
    sqlite3* db = openReadOnlyDatabase(query.dbPath);

    const char* sql =
        "SELECT frequency, length_meters, cell_count, points_json "
        "FROM frequent_paths "
        "WHERE length_meters > ? "
        "AND start_lon >= ? AND start_lon <= ? "
        "AND start_lat >= ? AND start_lat <= ? "
        "AND end_lon >= ? AND end_lon <= ? "
        "AND end_lat >= ? AND end_lat <= ? "
        "ORDER BY frequency DESC, length_meters DESC "
        "LIMIT ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throwSqliteError(db, "failed to prepare frequent path region query");
    }

    sqlite3_bind_double(stmt, 1, std::max(0.0, query.minLengthMeters));
    sqlite3_bind_double(stmt, 2, query.minLonA);
    sqlite3_bind_double(stmt, 3, query.maxLonA);
    sqlite3_bind_double(stmt, 4, query.minLatA);
    sqlite3_bind_double(stmt, 5, query.maxLatA);
    sqlite3_bind_double(stmt, 6, query.minLonB);
    sqlite3_bind_double(stmt, 7, query.maxLonB);
    sqlite3_bind_double(stmt, 8, query.minLatB);
    sqlite3_bind_double(stmt, 9, query.maxLatB);
    sqlite3_bind_int(stmt, 10, query.k);

    std::vector<FrequentPathRecord> records;
    int rank = 1;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        records.push_back(readRecord(stmt, rank++));
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    return records;
}

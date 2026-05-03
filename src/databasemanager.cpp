#include "databasemanager.h"
#include <algorithm>
#include <sqlite3.h>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include "logger.h"
namespace fs = std::filesystem;

namespace {

bool execSql(sqlite3* db, const char* sql) {
    char* errMsg = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        Error() << "SQLite exec failed: " << (errMsg ? errMsg : "unknown error") << std::endl;
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

std::int64_t parseTimestamp(const std::string& text) {
    std::tm tm{};
    std::istringstream iss(text);
    iss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    if (iss.fail()) {
        return 0;
    }

#if defined(_WIN32)
    return static_cast<std::int64_t>(_mkgmtime(&tm));
#else
    return static_cast<std::int64_t>(timegm(&tm));
#endif
}

bool readPointFromStmt(sqlite3_stmt* stmt, GPSPoint& point) {
    point.id = sqlite3_column_int(stmt, 0);
    point.timestamp = static_cast<long long>(sqlite3_column_int64(stmt, 1));
    point.lon = sqlite3_column_double(stmt, 2);
    point.lat = sqlite3_column_double(stmt, 3);
    return true;
}

} // namespace

DatabaseManager::DatabaseManager(const std::string& dbName)
    : dbPath(dbName) {}

DatabaseManager::~DatabaseManager() {
    if (db != nullptr) {
        sqlite3_close(db);
        db = nullptr;
    }
}

bool DatabaseManager::open() {
    Debug() << "DatabaseManager::open begin: " << dbPath << std::endl;

    const fs::path dbFilePath(dbPath);
    const fs::path dbDirPath = dbFilePath.parent_path();
    if (!dbDirPath.empty()) {
        Debug() << "Database directory: " << dbDirPath.string() << std::endl;
        std::error_code ec;
        fs::create_directories(dbDirPath, ec);
        if (ec) {
            Error() << "Database directory creation failed: " << dbDirPath << std::endl;
            return false;
        }
    }

    Debug() << "Opening sqlite database..." << std::endl;
    if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK) {
        Error() << "数据库打开失败: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    Debug() << "sqlite3_open ok" << std::endl;

    Debug() << "SQLite PRAGMA journal_mode" << std::endl;
    if (!execSql(db, "PRAGMA journal_mode = MEMORY;")) {
        return false;
    }
    Debug() << "SQLite PRAGMA synchronous" << std::endl;
    if (!execSql(db, "PRAGMA synchronous = OFF;")) {
        return false;
    }
    Debug() << "SQLite PRAGMA temp_store" << std::endl;
    if (!execSql(db, "PRAGMA temp_store = MEMORY;")) {
        return false;
    }
    Debug() << "SQLite PRAGMA cache_size" << std::endl;
    if (!execSql(db, "PRAGMA cache_size = -262144;")) {   // 约 256MB 页缓存
        return false;
    }
    Debug() << "SQLite PRAGMA mmap_size" << std::endl;
    if (!execSql(db, "PRAGMA mmap_size = 1073741824;")) { // 1GB mmap，上限由系统决定
        return false;
    }

    Debug() << "Ensuring taxi_points table" << std::endl;
    if (!execSql(db, "CREATE TABLE IF NOT EXISTS taxi_points (id INTEGER, time INTEGER, lon REAL, lat REAL);")) {
        Error() << "建表失败" << std::endl;
        return false;
    }

    Debug() << "DatabaseManager::open ok" << std::endl;
    return true;
}
bool DatabaseManager::batchInsert(const std::vector<GPSPoint>& points) {
    if (points.empty()) {
        return true;
    }
    if (db == nullptr) {
        Error() << "batchInsert failed: database not open" << std::endl;
        return false;
    }

    if (!execSql(db, "BEGIN TRANSACTION;")) {
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO taxi_points (id, time, lon, lat) VALUES (?, ?, ?, ?);";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        Error() << "插入预编译失败: " << sqlite3_errmsg(db) << std::endl;
        execSql(db, "ROLLBACK;");
        return false;
    }

    for (const auto& p : points) {
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);

        sqlite3_bind_int(stmt, 1, p.id);
        sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(p.timestamp));
        sqlite3_bind_double(stmt, 3, p.lon);
        sqlite3_bind_double(stmt, 4, p.lat);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            Error() << "插入失败: " << sqlite3_errmsg(db) << std::endl;
            sqlite3_finalize(stmt);
            execSql(db, "ROLLBACK;");
            return false;
        }
    }

    sqlite3_finalize(stmt);

    if (!execSql(db, "COMMIT;")) {
        execSql(db, "ROLLBACK;");
        return false;
    }

    return true;
}

std::int64_t DatabaseManager::getPointCount() {
    if (db == nullptr) {
        Error() << "数据库未打开，无法统计点数" << std::endl;
        return 0;
    }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM taxi_points;", -1, &stmt, nullptr) != SQLITE_OK) {
        Error() << "统计点数失败: " << sqlite3_errmsg(db) << std::endl;
        return 0;
    }

    std::int64_t count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = static_cast<std::int64_t>(sqlite3_column_int64(stmt, 0));
    }
    sqlite3_finalize(stmt);
    return count;
}

std::vector<GPSPoint> DatabaseManager::getTrajectoryByTaxiId(int taxiId) {
    std::vector<GPSPoint> points;
    if (db == nullptr) {
        Error() << "Database is not open, cannot query trajectory." << std::endl;
        return points;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, time, lon, lat FROM taxi_points WHERE id = ? ORDER BY time ASC;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        Error() << "Failed to query trajectory: " << sqlite3_errmsg(db) << std::endl;
        return points;
    }

    sqlite3_bind_int(stmt, 1, taxiId);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        GPSPoint point{};
        readPointFromStmt(stmt, point);
        points.push_back(point);
    }

    sqlite3_finalize(stmt);
    return points;
}

std::vector<GPSPoint> DatabaseManager::getAllPointsForDisplay(int maxPoints) {
    std::vector<GPSPoint> points;
    if (db == nullptr) {
        Error() << "Database is not open, cannot query all points." << std::endl;
        return points;
    }
    if (maxPoints <= 0) {
        return points;
    }

    const std::int64_t totalCount = getPointCount();
    if (totalCount <= 0) {
        return points;
    }

    const std::int64_t step = std::max<std::int64_t>(1, (totalCount + maxPoints - 1) / maxPoints);

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, time, lon, lat FROM taxi_points WHERE (? = 1 OR rowid % ? = 0) LIMIT ?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        Error() << "Failed to query all points for display: " << sqlite3_errmsg(db) << std::endl;
        return points;
    }

    sqlite3_bind_int64(stmt, 1, step);
    sqlite3_bind_int64(stmt, 2, step);
    sqlite3_bind_int(stmt, 3, maxPoints);

    points.reserve(static_cast<std::size_t>(maxPoints));
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        GPSPoint point{};
        readPointFromStmt(stmt, point);
        points.push_back(point);
    }

    sqlite3_finalize(stmt);
    return points;
}

std::int64_t DatabaseManager::countUniqueTaxisInBoundsAndTime(std::int64_t startTime,
                                                              std::int64_t endTime,
                                                              double minLon,
                                                              double minLat,
                                                              double maxLon,
                                                              double maxLat) {
    if (db == nullptr) {
        Error() << "Database is not open, cannot count taxis in bounds." << std::endl;
        return -1;
    }

    std::int64_t datasetMinTime = 0;
    std::int64_t datasetMaxTime = 0;
    double datasetMinLon = 0.0;
    double datasetMinLat = 0.0;
    double datasetMaxLon = 0.0;
    double datasetMaxLat = 0.0;
    const bool hasValidBounds = getDatasetBounds(
        datasetMinTime, datasetMaxTime, datasetMinLon, datasetMinLat, datasetMaxLon, datasetMaxLat);
    const std::int64_t reasonableEpochStart = 946684800;
    const bool timeRangeLooksInvalid =
        !hasValidBounds || datasetMaxTime < reasonableEpochStart || datasetMinTime > datasetMaxTime;

    sqlite3_stmt* stmt = nullptr;
    if (timeRangeLooksInvalid) {
        const char* sql =
            "SELECT COUNT(DISTINCT id) FROM taxi_points WHERE lon >= ? AND lon <= ? AND lat >= ? AND lat <= ?;";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            Error() << "Failed to count taxis in bounds: " << sqlite3_errmsg(db) << std::endl;
            return -1;
        }
        sqlite3_bind_double(stmt, 1, minLon);
        sqlite3_bind_double(stmt, 2, maxLon);
        sqlite3_bind_double(stmt, 3, minLat);
        sqlite3_bind_double(stmt, 4, maxLat);
    } else {
        const char* sql =
            "SELECT COUNT(DISTINCT id) FROM taxi_points "
            "WHERE time >= ? AND time <= ? AND lon >= ? AND lon <= ? AND lat >= ? AND lat <= ?;";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            Error() << "Failed to count taxis in bounds: " << sqlite3_errmsg(db) << std::endl;
            return -1;
        }
        sqlite3_bind_int64(stmt, 1, startTime);
        sqlite3_bind_int64(stmt, 2, endTime);
        sqlite3_bind_double(stmt, 3, minLon);
        sqlite3_bind_double(stmt, 4, maxLon);
        sqlite3_bind_double(stmt, 5, minLat);
        sqlite3_bind_double(stmt, 6, maxLat);
    }

    std::int64_t count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = static_cast<std::int64_t>(sqlite3_column_int64(stmt, 0));
    }
    sqlite3_finalize(stmt);
    return count;
}

bool DatabaseManager::getDatasetBounds(std::int64_t& minTime,
                                       std::int64_t& maxTime,
                                       double& minLon,
                                       double& minLat,
                                       double& maxLon,
                                       double& maxLat) {
    if (db == nullptr) {
        Error() << "Database is not open, cannot get dataset bounds." << std::endl;
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT MIN(time), MAX(time), MIN(lon), MIN(lat), MAX(lon), MAX(lat) FROM taxi_points;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        Error() << "Failed to query dataset bounds: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }

    const int rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW || sqlite3_column_type(stmt, 0) == SQLITE_NULL || sqlite3_column_type(stmt, 1) == SQLITE_NULL) {
        sqlite3_finalize(stmt);
        return false;
    }

    minTime = static_cast<std::int64_t>(sqlite3_column_int64(stmt, 0));
    maxTime = static_cast<std::int64_t>(sqlite3_column_int64(stmt, 1));
    minLon = sqlite3_column_double(stmt, 2);
    minLat = sqlite3_column_double(stmt, 3);
    maxLon = sqlite3_column_double(stmt, 4);
    maxLat = sqlite3_column_double(stmt, 5);

    sqlite3_finalize(stmt);
    return true;
}

sqlite3* DatabaseManager::getRawHandle() const {
    return db;
}

void DatabaseManager::checkAndImportData(DatabaseManager& dbm, const AppConfig& config) {
    if (dbm.getPointCount() > 0) {
        Debug() << "检测到数据库已有数据，跳过导入。当前点数: " << dbm.getPointCount() << std::endl;
        return;
    }

    Debug() << "--- 数据库为空，开始导入数据（排序建库模式） ---" << std::endl;
    if (!fs::exists(config.dataDir) || !fs::is_directory(config.dataDir)) {
        Debug() << "Data directory does not exist, cannot import: " << config.dataDir << std::endl;
        return;
    }

    bool hasTxtFile = false;
    for (const auto& entry : fs::recursive_directory_iterator(config.dataDir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".txt") {
            hasTxtFile = true;
            break;
        }
    }

    if (!hasTxtFile) {
        Debug() << "No .txt files found under data directory, cannot import: " << config.dataDir << std::endl;
        return;
    }

    const auto totalStart = std::chrono::steady_clock::now();

    std::vector<GPSPoint> allImported;
    int fileCount = 0;
    std::int64_t invalidLineCount = 0;
    std::int64_t invalidTimeCount = 0;

    for (const auto& entry : fs::recursive_directory_iterator(config.dataDir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".txt") {
            continue;
        }

        std::ifstream fin(entry.path());
        if (!fin.is_open()) {
            Debug() << "文件打开失败: " << entry.path().string() << std::endl;
            continue;
        }

        std::string line;
        while (std::getline(fin, line)) {
            std::stringstream ss(line);
            std::string idStr;
            std::string timeStr;
            std::string lonStr;
            std::string latStr;

            if (!std::getline(ss, idStr, ',')) { ++invalidLineCount; continue; }
            if (!std::getline(ss, timeStr, ',')) { ++invalidLineCount; continue; }
            if (!std::getline(ss, lonStr, ',')) { ++invalidLineCount; continue; }
            if (!std::getline(ss, latStr, ',')) { ++invalidLineCount; continue; }

            GPSPoint p{};
            try {
                p.id = std::stoi(idStr);
                p.timestamp = parseTimestamp(timeStr);
                p.lon = std::stod(lonStr);
                p.lat = std::stod(latStr);
            } catch (...) {
                ++invalidLineCount;
                continue;
            }

            if (p.timestamp == 0) {
                ++invalidTimeCount;
                continue;
            }

            if (p.lon > config.minLon && p.lon < config.maxLon &&
                p.lat > config.minLat && p.lat < config.maxLat) {
                allImported.push_back(p);
            }
        }

        ++fileCount;
        if (fileCount % config.batchSize == 0) {
            Debug() << "已扫描 " << fileCount << " 个文件，当前累计有效点数: "
                    << allImported.size() << std::endl;
        }
    }

    Debug() << "文件扫描完成，开始内存排序。总点数: " << allImported.size()
            << "，无效行数: " << invalidLineCount
            << "，无效时间数: " << invalidTimeCount << std::endl;

    const auto sortStart = std::chrono::steady_clock::now();

    std::sort(allImported.begin(), allImported.end(),
              [](const GPSPoint& a, const GPSPoint& b) {
                  if (a.id != b.id) return a.id < b.id;
                  if (a.timestamp != b.timestamp) return a.timestamp < b.timestamp;
                  if (a.lon != b.lon) return a.lon < b.lon;
                  return a.lat < b.lat;
              });

    const auto sortMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - sortStart).count();
    Debug() << "内存排序完成，耗时(ms): " << sortMs << std::endl;

    const std::size_t insertChunkSize = 500000;
    std::size_t inserted = 0;

    for (std::size_t begin = 0; begin < allImported.size(); begin += insertChunkSize) {
        const std::size_t end = std::min(begin + insertChunkSize, allImported.size());
        std::vector<GPSPoint> chunk(allImported.begin() + static_cast<std::ptrdiff_t>(begin),
                                    allImported.begin() + static_cast<std::ptrdiff_t>(end));

        if (!dbm.batchInsert(chunk)) {
            Debug() << "排序后分批插入失败，程序中止导入" << std::endl;
            return;
        }

        inserted += chunk.size();
        Debug() << "已按序写入: " << inserted << "/" << allImported.size() << std::endl;
    }

    sqlite3* db = dbm.getRawHandle();
    if (db != nullptr) {
        execSql(db, "ANALYZE;");
    }

    const auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - totalStart).count();

    Debug() << "排序建库完成！总点数: " << allImported.size()
            << "，总耗时(ms): " << totalMs << std::endl;
}

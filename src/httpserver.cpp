#include "httpserver.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <iomanip>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include "datamanager.h"
#include "densityanalysis.h"
#include "httplib.h"
#include "json.hpp"
#include "logger.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {
struct DensityBucketSummary {
    long long startTime = 0;
    long long endTime = 0;
    int nonZeroCount = 0;
    double maxDensity = 0.0;
};

struct DensityCacheEntry {
    std::string queryId;
    DensityAnalysisRequest request;
    DensityAnalysisResult result;
    std::vector<DensityBucketSummary> bucketSummaries;
    std::chrono::steady_clock::time_point createdAt;
    std::chrono::steady_clock::time_point lastAccessAt;
};

std::mutex g_densityCacheMutex;
std::unordered_map<std::string, std::shared_ptr<DensityCacheEntry>> g_densityCacheById;

// 为了简单起见，先只保留很少的缓存结果，避免本地内存继续炸
constexpr std::size_t kDensityCacheMaxEntries = 2;
constexpr auto kDensityCacheTtl = std::chrono::minutes(15);

std::string makeDensityRequestKey(const DensityAnalysisRequest& request) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(8)
        << request.minLon << '|'
        << request.minLat << '|'
        << request.maxLon << '|'
        << request.maxLat << '|'
        << request.startTime << '|'
        << request.endTime << '|'
        << request.intervalMinutes << '|'
        << request.cellSizeMeters;
    return oss.str();
}

std::string makeDensityQueryId() {
    const auto nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    static std::atomic<unsigned long long> seq{0};
    const auto id = ++seq;

    std::ostringstream oss;
    oss << "density_" << nowNs << "_" << id;
    return oss.str();
}

void cleanupDensityCacheLocked() {
    const auto now = std::chrono::steady_clock::now();

    for (auto it = g_densityCacheById.begin(); it != g_densityCacheById.end(); ) {
        const auto& entry = it->second;
        if (!entry || (now - entry->lastAccessAt) > kDensityCacheTtl) {
            it = g_densityCacheById.erase(it);
        } else {
            ++it;
        }
    }

    if (g_densityCacheById.size() <= kDensityCacheMaxEntries) {
        return;
    }

    std::vector<std::pair<std::string, std::shared_ptr<DensityCacheEntry>>> entries;
    entries.reserve(g_densityCacheById.size());
    for (const auto& kv : g_densityCacheById) {
        entries.push_back(kv);
    }

    std::sort(entries.begin(), entries.end(),
              [](const auto& a, const auto& b) {
                  return a.second->lastAccessAt < b.second->lastAccessAt;
              });

    while (g_densityCacheById.size() > kDensityCacheMaxEntries && !entries.empty()) {
        g_densityCacheById.erase(entries.front().first);
        entries.erase(entries.begin());
    }
}

std::shared_ptr<DensityCacheEntry> findDensityCacheBySameRequestLocked(const DensityAnalysisRequest& request) {
    const std::string targetKey = makeDensityRequestKey(request);

    for (auto& kv : g_densityCacheById) {
        auto& entry = kv.second;
        if (!entry) {
            continue;
        }
        if (makeDensityRequestKey(entry->request) == targetKey) {
            entry->lastAccessAt = std::chrono::steady_clock::now();
            return entry;
        }
    }
    return nullptr;
}

std::vector<DensityBucketSummary> buildDensityBucketSummaries(
    const DensityAnalysisRequest& request,
    const DensityAnalysisResult& result) {

    std::vector<DensityBucketSummary> summaries;
    summaries.resize(static_cast<std::size_t>(result.bucketCount));

    const int bucketCount = result.bucketCount;
    const int columnCount = result.columnCount;
    const int rowCount = result.rowCount;
    const int gridSize = columnCount * rowCount;
    const int bucketSeconds = request.intervalMinutes * 60;
    const double bucketSecondsDouble = std::max(1.0, static_cast<double>(bucketSeconds));
    const double invArea = 1.0 / std::max(1e-9, result.cellAreaKm2);

    for (int b = 0; b < bucketCount; ++b) {
        auto& summary = summaries[static_cast<std::size_t>(b)];
        summary.startTime = request.startTime + static_cast<long long>(b) * bucketSeconds;
        summary.endTime = std::min(summary.startTime + bucketSeconds - 1LL, request.endTime);

        const int base = b * gridSize;
        int nonZeroCount = 0;
        double maxDensity = 0.0;

        for (int i = 0; i < gridSize; ++i) {
            const float seconds = result.vehicleSeconds[base + i];
            if (seconds <= 0.0f) {
                continue;
            }

            ++nonZeroCount;
            const double density = (static_cast<double>(seconds) / bucketSecondsDouble) * invArea;
            if (density > maxDensity) {
                maxDensity = density;
            }
        }

        summary.nonZeroCount = nonZeroCount;
        summary.maxDensity = maxDensity;
    }

    return summaries;
}

std::shared_ptr<DensityCacheEntry> getOrCreateDensityCacheEntry(const DensityAnalysisRequest& request) {
    {
        std::lock_guard<std::mutex> lock(g_densityCacheMutex);
        cleanupDensityCacheLocked();
        if (auto existing = findDensityCacheBySameRequestLocked(request)) {
            Debug() << "[density-cache] hit, queryId=" << existing->queryId << std::endl;
            return existing;
        }
    }

    Debug() << "[density-cache] miss, analyze begin" << std::endl;
    const DensityAnalysisResult result = DensityAnalyzer::analyze(request);
    if (!result.success) {
        return nullptr;
    }

    auto entry = std::make_shared<DensityCacheEntry>();
    entry->queryId = makeDensityQueryId();
    entry->request = request;
    entry->result = result;
    entry->bucketSummaries = buildDensityBucketSummaries(request, result);
    entry->createdAt = std::chrono::steady_clock::now();
    entry->lastAccessAt = entry->createdAt;

    {
        std::lock_guard<std::mutex> lock(g_densityCacheMutex);
        cleanupDensityCacheLocked();
        g_densityCacheById[entry->queryId] = entry;
    }

    Debug() << "[density-cache] store ok, queryId=" << entry->queryId
            << ", vehicleSeconds bytes≈"
            << static_cast<long long>(entry->result.vehicleSeconds.size() * sizeof(float))
            << std::endl;

    return entry;
}

std::shared_ptr<DensityCacheEntry> getDensityCacheById(const std::string& queryId) {
    std::lock_guard<std::mutex> lock(g_densityCacheMutex);
    cleanupDensityCacheLocked();

    auto it = g_densityCacheById.find(queryId);
    if (it == g_densityCacheById.end() || !it->second) {
        return nullptr;
    }

    it->second->lastAccessAt = std::chrono::steady_clock::now();
    return it->second;
}

bool tryReadBucketIndex(const json& body, int& bucketIndex) {
    if (!body.contains("bucketIndex")) {
        return false;
    }
    const json& value = body.at("bucketIndex");
    if (!value.is_number_integer()) {
        return false;
    }
    bucketIndex = value.get<int>();
    return true;
}

bool tryReadGridIndex(const json& body, const char* key, int& out) {
    if (!body.contains(key)) {
        return false;
    }
    const json& value = body.at(key);
    if (!value.is_number_integer()) {
        return false;
    }
    out = value.get<int>();
    return true;
}
json makeSuccess(const json& data) {
    return json{{"success", true}, {"data", data}};
}

json makeError(const std::string& code, const std::string& message) {
    return json{
        {"success", false},
        {"error", {{"code", code}, {"message", message}}}
    };
}

bool parseJsonBody(const httplib::Request& req, json& out, std::string& errorMessage) {
    try {
        out = json::parse(req.body);
        if (!out.is_object()) {
            errorMessage = "request body must be a JSON object";
            return false;
        }
        return true;
    } catch (const std::exception& ex) {
        errorMessage = ex.what();
        return false;
    }
}

void applyCorsHeaders(httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Headers", "Content-Type");
    res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    res.set_header("Access-Control-Max-Age", "86400");
}

json pointToJson(const GPSPoint& point) {
    return json{
        {"id", point.id},
        {"timestamp", point.timestamp},
        {"lon", point.lon},
        {"lat", point.lat}
    };
}

long long toInt64(const json& value, long long defaultValue = 0) {
    if (value.is_number_integer()) {
        return value.get<long long>();
    }
    if (value.is_string()) {
        try {
            return std::stoll(value.get<std::string>());
        } catch (...) {
            return defaultValue;
        }
    }
    return defaultValue;
}

bool tryReadDouble(const json& body, const char* key, double& out) {
    if (!body.contains(key)) {
        return false;
    }
    const json& value = body.at(key);
    if (!value.is_number()) {
        return false;
    }
    out = value.get<double>();
    return std::isfinite(out);
}

bool tryReadRegionBounds(const json& body,
                         double& minLon,
                         double& minLat,
                         double& maxLon,
                         double& maxLat) {
    const bool hasMinLon = tryReadDouble(body, "minLon", minLon);
    const bool hasMinLat = tryReadDouble(body, "minLat", minLat);
    const bool hasMaxLon = tryReadDouble(body, "maxLon", maxLon);
    const bool hasMaxLat = tryReadDouble(body, "maxLat", maxLat);
    if (!(hasMinLon && hasMinLat && hasMaxLon && hasMaxLat)) {
        return false;
    }
    return minLon < maxLon && minLat < maxLat;
}

std::string readTextFile(const fs::path& path) {
    std::ifstream fin(path, std::ios::binary);
    if (!fin.is_open()) {
        return "";
    }
    std::ostringstream oss;
    oss << fin.rdbuf();
    return oss.str();
}

} // namespace

HttpServer::HttpServer(const AppConfig& config)
    : m_config(config) {}

std::string HttpServer::resolveWebRoot() const {
    std::vector<fs::path> roots;

#ifdef APP_SOURCE_WEB_ROOT
    roots.emplace_back(APP_SOURCE_WEB_ROOT);
#endif

    if (!m_config.mapPath.empty()) {
        fs::path configuredPath(m_config.mapPath);
        if (fs::is_regular_file(configuredPath)) {
            roots.push_back(configuredPath.parent_path());
        } else {
            roots.push_back(configuredPath);
        }
    }

    roots.push_back(fs::current_path() / "web");
    roots.push_back(fs::current_path() / "../web");
    roots.push_back(fs::current_path() / "../../web");

    for (const auto& root : roots) {
        const fs::path indexPath = root / "index.html";
        if (fs::exists(indexPath)) {
            return fs::absolute(root).lexically_normal().string();
        }
    }

    if (!m_config.mapPath.empty()) {
        fs::path configuredPath(m_config.mapPath);
        return fs::is_regular_file(configuredPath)
                   ? configuredPath.parent_path().lexically_normal().string()
                   : configuredPath.lexically_normal().string();
    }

    return fs::absolute(fs::current_path() / "../web").lexically_normal().string();
}

bool HttpServer::start(std::uint16_t port) {
    httplib::Server server;
    server.set_default_headers({
        {"Access-Control-Allow-Origin", "*"},
        {"Access-Control-Allow-Headers", "Content-Type"},
        {"Access-Control-Allow-Methods", "GET, POST, OPTIONS"},
        {"Access-Control-Max-Age", "86400"},
        {"Cache-Control", "no-store"}
    });

    server.Options(R"(.*)", [](const httplib::Request&, httplib::Response& res) {
        applyCorsHeaders(res);
        res.status = 204;
    });

    const std::string webRoot = resolveWebRoot();
    Debug() << "Web root: " << webRoot << std::endl;

    server.Get("/api/health", [](const httplib::Request&, httplib::Response& res) {
        const json data = {
            {"status", "ok"},
            {"pointsLoaded", static_cast<long long>(DataManager::getAllPoints().size())}
        };
        res.set_content(makeSuccess(data).dump(), "application/json; charset=utf-8");
    });

    server.Get("/api/meta", [this](const httplib::Request&, httplib::Response& res) {
        const json data = {
            {"minLon", m_config.minLon},
            {"maxLon", m_config.maxLon},
            {"minLat", m_config.minLat},
            {"maxLat", m_config.maxLat},
            {"centerLon", m_config.mapCenterLon},
            {"centerLat", m_config.mapCenterLat},
            {"initialZoom", m_config.mapInitialZoom},
            {"minZoom", m_config.mapMinZoom},
            {"maxZoom", m_config.mapMaxZoom},
            {"totalPoints", static_cast<long long>(DataManager::getAllPoints().size())},
            {"baiduMapAk", m_config.baiduMapAk}
        };
        res.set_content(makeSuccess(data).dump(), "application/json; charset=utf-8");
    });

    server.Post("/api/trajectory", [](const httplib::Request& req, httplib::Response& res) {
        json body;
        std::string errorMessage;
        if (!parseJsonBody(req, body, errorMessage)) {
            res.status = 400;
            res.set_content(makeError("INVALID_JSON", errorMessage).dump(), "application/json; charset=utf-8");
            return;
        }

        const long long taxiId = body.value("taxiId", -1LL);
        if (taxiId < 0) {
            res.status = 400;
            res.set_content(makeError("INVALID_ARGUMENT", "taxiId invalid").dump(), "application/json; charset=utf-8");
            return;
        }

        if (taxiId == 0) {
            const bool hasBounds = body.contains("minLon") && body.contains("minLat") &&
                                   body.contains("maxLon") && body.contains("maxLat");
            if (!hasBounds) {
                res.status = 400;
                res.set_content(makeError("INVALID_ARGUMENT", "all-taxi query requires map bounds").dump(), "application/json; charset=utf-8");
                return;
            }

            const double minLon = body.value("minLon", 0.0);
            const double minLat = body.value("minLat", 0.0);
            const double maxLon = body.value("maxLon", 0.0);
            const double maxLat = body.value("maxLat", 0.0);
            const int zoom = body.value("zoom", 12);

            if (minLon >= maxLon || minLat >= maxLat) {
                res.status = 400;
                res.set_content(makeError("INVALID_ARGUMENT", "bounds invalid").dump(), "application/json; charset=utf-8");
                return;
            }

            const std::vector<GPSPoint> points = DataManager::querySpatial(minLon, minLat, maxLon, maxLat);
            const bool canRenderRaw = zoom >= 18 && points.size() <= 12000;
            if (canRenderRaw) {
                json pointArray = json::array();
                for (const auto& point : points) {
                    pointArray.push_back(pointToJson(point));
                }

                const json data = {
                    {"taxiId", 0},
                    {"mode", "raw"},
                    {"pointCount", static_cast<long long>(points.size())},
                    {"clusterCount", static_cast<long long>(points.size())},
                    {"renderCap", 12000},
                    {"points", pointArray}
                };
                res.set_content(makeSuccess(data).dump(), "application/json; charset=utf-8");
                return;
            }

            const std::vector<ClusterPoint> clusters =
                DataManager::clusterPointsForView(points, minLon, minLat, maxLon, maxLat, zoom);

            json clusterArray = json::array();
            for (const auto& cluster : clusters) {
                clusterArray.push_back({
                    {"lng", cluster.lon},
                    {"lat", cluster.lat},
                    {"count", cluster.count},
                    {"isCluster", cluster.isCluster},
                    {"minLon", cluster.minLon},
                    {"minLat", cluster.minLat},
                    {"maxLon", cluster.maxLon},
                    {"maxLat", cluster.maxLat}
                });
            }

            const json data = {
                {"taxiId", 0},
                {"mode", "cluster"},
                {"pointCount", static_cast<long long>(points.size())},
                {"clusterCount", static_cast<long long>(clusters.size())},
                {"renderCap", 12000},
                {"points", clusterArray}
            };
            res.set_content(makeSuccess(data).dump(), "application/json; charset=utf-8");
            return;
        }

        const std::vector<GPSPoint> points = DataManager::getPointsRangeById(static_cast<int>(taxiId));
        json pointArray = json::array();
        for (const auto& point : points) {
            pointArray.push_back(pointToJson(point));
        }

        const json data = {
            {"taxiId", taxiId},
            {"mode", "trajectory"},
            {"pointCount", static_cast<long long>(points.size())},
            {"points", pointArray}
        };
        res.set_content(makeSuccess(data).dump(), "application/json; charset=utf-8");
    });

    server.Post("/api/region-search", [](const httplib::Request& req, httplib::Response& res) {
        json body;
        std::string errorMessage;
        if (!parseJsonBody(req, body, errorMessage)) {
            res.status = 400;
            res.set_content(makeError("INVALID_JSON", errorMessage).dump(), "application/json; charset=utf-8");
            return;
        }

        const double minLon = body.value("minLon", 0.0);
        const double minLat = body.value("minLat", 0.0);
        const double maxLon = body.value("maxLon", 0.0);
        const double maxLat = body.value("maxLat", 0.0);
        const long long startTime = toInt64(body.value("startTime", 0), 0);
        const long long endTime = toInt64(body.value("endTime", 0), 0);

        if (minLon >= maxLon || minLat >= maxLat || startTime > endTime) {
            res.status = 400;
            res.set_content(makeError("INVALID_ARGUMENT", "region or time range invalid").dump(), "application/json; charset=utf-8");
            return;
        }

        const auto start = std::chrono::steady_clock::now();
        const std::vector<GPSPoint> points = DataManager::querySpatialAndTime(minLon, minLat, maxLon, maxLat, startTime, endTime);
        const int vehicleCount = DataManager::getUniqueCountById(points);
        const double elapsedSeconds = static_cast<double>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count()) / 1000.0;

        const json data = {
            {"pointCount", static_cast<long long>(points.size())},
            {"vehicleCount", vehicleCount},
            {"elapsedSeconds", elapsedSeconds}
        };
        res.set_content(makeSuccess(data).dump(), "application/json; charset=utf-8");
    });

    server.Post("/api/density/meta", [this](const httplib::Request& req, httplib::Response& res) {
    const auto t_start = std::chrono::steady_clock::now();
    
    json body;
    std::string errorMessage;
    if (!parseJsonBody(req, body, errorMessage)) {
        res.status = 400;
        res.set_content(makeError("INVALID_JSON", errorMessage).dump(), "application/json; charset=utf-8");
        const auto t_end = std::chrono::steady_clock::now();
        Debug() << "[density-meta] FAILED (parse error) in " 
                << std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count() << "ms" << std::endl;
        return;
    }

    DensityAnalysisRequest request;
    request.minLon = m_config.minLon;
    request.minLat = m_config.minLat;
    request.maxLon = m_config.maxLon;
    request.maxLat = m_config.maxLat;

    double regionMinLon = 0.0;
    double regionMinLat = 0.0;
    double regionMaxLon = 0.0;
    double regionMaxLat = 0.0;
    const bool useClientRegion = tryReadRegionBounds(body, regionMinLon, regionMinLat, regionMaxLon, regionMaxLat);
    if (body.contains("minLon") || body.contains("minLat") || body.contains("maxLon") || body.contains("maxLat")) {
        if (!useClientRegion) {
            res.status = 400;
            res.set_content(makeError("INVALID_ARGUMENT", "density region bounds invalid").dump(), "application/json; charset=utf-8");
            return;
        }
        request.minLon = regionMinLon;
        request.minLat = regionMinLat;
        request.maxLon = regionMaxLon;
        request.maxLat = regionMaxLat;
    }

    request.startTime = toInt64(body.value("startTime", 0), 0);
    request.endTime = toInt64(body.value("endTime", 0), 0);
    request.intervalMinutes = body.value("intervalMinutes", 30);
    request.cellSizeMeters = body.value("cellSizeMeters", 500.0);

    if (request.minLon >= request.maxLon || request.minLat >= request.maxLat) {
        res.status = 400;
        res.set_content(makeError("INVALID_ARGUMENT", "map bounds invalid").dump(), "application/json; charset=utf-8");
        return;
    }

    const auto t0 = std::chrono::steady_clock::now();
    auto entry = getOrCreateDensityCacheEntry(request);
    const auto t1 = std::chrono::steady_clock::now();
    const auto cacheFetchMs = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    if (!entry) {
        res.status = 400;
        res.set_content(makeError("ANALYSIS_FAILED", "density analyze failed").dump(), "application/json; charset=utf-8");
        const auto t_end = std::chrono::steady_clock::now();
        Debug() << "[density-meta] FAILED (analysis error) in " 
                << std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count() << "ms" << std::endl;
        return;
    }

    json bucketSummaryArray = json::array();
    for (const auto& bucket : entry->bucketSummaries) {
        bucketSummaryArray.push_back({
            {"startTime", bucket.startTime},
            {"endTime", bucket.endTime},
            {"nonZeroCount", bucket.nonZeroCount},
            {"maxDensity", bucket.maxDensity}
        });
    }

    const json data = {
        {"queryId", entry->queryId},
        {"minLon", entry->request.minLon},
        {"minLat", entry->request.minLat},
        {"maxLon", entry->request.maxLon},
        {"maxLat", entry->request.maxLat},
        {"regionSource", useClientRegion ? "selection" : "full-map"},
        {"startTime", entry->request.startTime},
        {"endTime", entry->request.endTime},
        {"intervalMinutes", entry->request.intervalMinutes},
        {"bucketSeconds", entry->request.intervalMinutes * 60},
        {"cellSizeMeters", entry->request.cellSizeMeters},
        {"lonStep", entry->result.lonStep},
        {"latStep", entry->result.latStep},
        {"cellAreaKm2", entry->result.cellAreaKm2},
        {"columnCount", entry->result.columnCount},
        {"rowCount", entry->result.rowCount},
        {"bucketCount", entry->result.bucketCount},
        {"gridCount", entry->result.gridCount},
        {"analysisScale", entry->result.analysisScale},
        {"maxVehicleDensity", entry->result.maxVehicleDensity},
        {"totalPointCount", entry->result.totalPointCount},
        {"totalVehicleCount", entry->result.totalVehicleCount},
        {"elapsedSeconds", entry->result.elapsedSeconds},
        {"cacheFetchCostMs", cacheFetchMs},
        {"buckets", bucketSummaryArray}
    };

    const auto t_end = std::chrono::steady_clock::now();
    const auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();
    Debug() << "[density-meta] " << totalMs << "ms (grid:" << entry->result.columnCount 
            << "x" << entry->result.rowCount << ", buckets:" << entry->result.bucketCount << ")" << std::endl;
    
    res.set_content(makeSuccess(data).dump(), "application/json; charset=utf-8");
});
server.Post("/api/density/bucket", [](const httplib::Request& req, httplib::Response& res) {
    const auto t_start = std::chrono::steady_clock::now();
    
    json body;
    std::string errorMessage;
    if (!parseJsonBody(req, body, errorMessage)) {
        res.status = 400;
        res.set_content(makeError("INVALID_JSON", errorMessage).dump(), "application/json; charset=utf-8");
        const auto t_end = std::chrono::steady_clock::now();
        Debug() << "[density-bucket] FAILED (parse error) in " 
                << std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count() << "ms" << std::endl;
        return;
    }

    const std::string queryId = body.value("queryId", "");
    if (queryId.empty()) {
        res.status = 400;
        res.set_content(makeError("INVALID_ARGUMENT", "queryId required").dump(), "application/json; charset=utf-8");
        return;
    }

    int bucketIndex = 0;
    if (!tryReadBucketIndex(body, bucketIndex)) {
        res.status = 400;
        res.set_content(makeError("INVALID_ARGUMENT", "bucketIndex invalid").dump(), "application/json; charset=utf-8");
        return;
    }

    auto entry = getDensityCacheById(queryId);
    if (!entry) {
        res.status = 404;
        res.set_content(makeError("NOT_FOUND", "density query cache expired or not found").dump(), "application/json; charset=utf-8");
        return;
    }

    if (bucketIndex < 0 || bucketIndex >= entry->result.bucketCount) {
        res.status = 400;
        res.set_content(makeError("INVALID_ARGUMENT", "bucketIndex out of range").dump(), "application/json; charset=utf-8");
        return;
    }

    const int columnCount = entry->result.columnCount;
    const int rowCount = entry->result.rowCount;
    const int gridSize = columnCount * rowCount;
    const int bucketSeconds = entry->request.intervalMinutes * 60;
    const int base = bucketIndex * gridSize;

    const auto& summary = entry->bucketSummaries[static_cast<std::size_t>(bucketIndex)];
    const auto t_validate = std::chrono::steady_clock::now();
    const auto validateMs = std::chrono::duration_cast<std::chrono::milliseconds>(t_validate - t_start).count();

    const auto t_json_begin = std::chrono::steady_clock::now();

    std::ostringstream oss;
    oss.precision(10);

    oss << "{\"success\":true,\"data\":{";
    oss << "\"queryId\":\"" << entry->queryId << "\",";
    oss << "\"bucketIndex\":" << bucketIndex << ",";
    oss << "\"startTime\":" << summary.startTime << ",";
    oss << "\"endTime\":" << summary.endTime << ",";
    oss << "\"nonZeroCount\":" << summary.nonZeroCount << ",";
    oss << "\"bucketSeconds\":" << bucketSeconds << ",";
    oss << "\"cells\":[";

    bool firstCell = true;
    for (int i = 0; i < gridSize; ++i) {
        const float seconds = entry->result.vehicleSeconds[base + i];
        if (seconds <= 0.0f) {
            continue;
        }

        const int gx = i % columnCount;
        const int gy = i / columnCount;

        if (!firstCell) {
            oss << ",";
        }
        firstCell = false;
        oss << "[" << gx << "," << gy << "," << seconds << "]";
    }

    oss << "]}}";

    const auto t_json_end = std::chrono::steady_clock::now();
    const auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(t_json_end - t_start).count();

    Debug() << "[density-bucket] " << totalMs << "ms (bucket:" << bucketIndex 
            << ", cells:" << summary.nonZeroCount << ")" << std::endl;

    res.set_content(oss.str(), "application/json; charset=utf-8");
});
server.Post("/api/density/cell-trend", [](const httplib::Request& req, httplib::Response& res) {
    const auto t_start = std::chrono::steady_clock::now();
    
    json body;
    std::string errorMessage;
    if (!parseJsonBody(req, body, errorMessage)) {
        res.status = 400;
        res.set_content(makeError("INVALID_JSON", errorMessage).dump(), "application/json; charset=utf-8");
        const auto t_end = std::chrono::steady_clock::now();
        Debug() << "[density-cell-trend] FAILED (parse error) in " 
                << std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count() << "ms" << std::endl;
        return;
    }

    const std::string queryId = body.value("queryId", "");
    if (queryId.empty()) {
        res.status = 400;
        res.set_content(makeError("INVALID_ARGUMENT", "queryId required").dump(), "application/json; charset=utf-8");
        return;
    }

    int gx = 0;
    int gy = 0;
    if (!tryReadGridIndex(body, "gx", gx) || !tryReadGridIndex(body, "gy", gy)) {
        res.status = 400;
        res.set_content(makeError("INVALID_ARGUMENT", "gx or gy invalid").dump(), "application/json; charset=utf-8");
        return;
    }

    auto entry = getDensityCacheById(queryId);
    if (!entry) {
        res.status = 404;
        res.set_content(makeError("NOT_FOUND", "density query cache expired or not found").dump(), "application/json; charset=utf-8");
        return;
    }

    if (gx < 0 || gx >= entry->result.columnCount || gy < 0 || gy >= entry->result.rowCount) {
        res.status = 400;
        res.set_content(makeError("INVALID_ARGUMENT", "gx or gy out of range").dump(), "application/json; charset=utf-8");
        return;
    }

    const int bucketCount = entry->result.bucketCount;
    const int columnCount = entry->result.columnCount;
    const int rowCount = entry->result.rowCount;
    const auto t_validate = std::chrono::steady_clock::now();
    const auto validateMs = std::chrono::duration_cast<std::chrono::milliseconds>(t_validate - t_start).count();

    const auto t_json_begin = std::chrono::steady_clock::now();
    
    std::ostringstream oss;
    oss.precision(10);

    oss << "{\"success\":true,\"data\":{";
    oss << "\"queryId\":\"" << entry->queryId << "\",";
    oss << "\"gx\":" << gx << ",";
    oss << "\"gy\":" << gy << ",";
    oss << "\"series\":[";

    bool first = true;
    for (int b = 0; b < bucketCount; ++b) {
        const std::size_t idx =
            static_cast<std::size_t>(b) * static_cast<std::size_t>(columnCount) * static_cast<std::size_t>(rowCount) +
            static_cast<std::size_t>(gy) * static_cast<std::size_t>(columnCount) +
            static_cast<std::size_t>(gx);

        const float seconds = entry->result.vehicleSeconds[idx];
        const auto& summary = entry->bucketSummaries[static_cast<std::size_t>(b)];

        if (!first) {
            oss << ",";
        }
        first = false;
        oss << "[" << b << "," << summary.startTime << "," << summary.endTime << "," << seconds << "]";
    }

    oss << "]}}";

    res.set_content(oss.str(), "application/json; charset=utf-8");
});

    server.set_mount_point("/", webRoot.c_str());

    server.Get("/", [webRoot](const httplib::Request&, httplib::Response& res) {
        const fs::path indexPath = fs::path(webRoot) / "index.html";
        if (!fs::exists(indexPath)) {
            res.status = 404;
            res.set_content(makeError("NOT_FOUND", "index.html not found").dump(), "application/json; charset=utf-8");
            return;
        }

        const std::string content = readTextFile(indexPath);
        if (content.empty()) {
            res.status = 500;
            res.set_content(makeError("FILE_ERROR", "cannot open index.html").dump(), "application/json; charset=utf-8");
            return;
        }

        res.set_content(content, "text/html; charset=utf-8");
    });

    const std::string host = "0.0.0.0";
    return server.listen(host.c_str(), static_cast<int>(port));
}
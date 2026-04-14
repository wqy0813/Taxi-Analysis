#include "httpserver.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "datamanager.h"
#include "densityanalysis.h"
#include "httplib.h"
#include "json.hpp"
#include "logger.h"
using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

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
                                          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count()) / 1000.0;

        const json data = {
            {"pointCount", static_cast<long long>(points.size())},
            {"vehicleCount", vehicleCount},
            {"elapsedSeconds", elapsedSeconds}
        };
        res.set_content(makeSuccess(data).dump(), "application/json; charset=utf-8");
    });

    server.Post("/api/density", [this](const httplib::Request& req, httplib::Response& res) {
        json body;
        std::string errorMessage;
        if (!parseJsonBody(req, body, errorMessage)) {
            res.status = 400;
            res.set_content(makeError("INVALID_JSON", errorMessage).dump(), "application/json; charset=utf-8");
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
            res.status = 500;
            res.set_content(makeError("CONFIG_ERROR", "map bounds invalid").dump(), "application/json; charset=utf-8");
            return;
        }

        const DensityAnalysisResult result = DensityAnalyzer::analyze(request);
        if (!result.success) {
            res.status = 400;
            res.set_content(makeError("ANALYSIS_FAILED", result.errorMessage).dump(), "application/json; charset=utf-8");
            return;
        }

        json bucketArray = json::array();
        for (const auto& bucket : result.buckets) {
            json cellArray = json::array();
            for (const auto& cell : bucket.cells) {
                const double cellMinLon = request.minLon + static_cast<double>(cell.gx) * result.lonStep;
                const double cellMinLat = request.minLat + static_cast<double>(cell.gy) * result.latStep;
                const double cellMaxLon = std::min(cellMinLon + result.lonStep, request.maxLon);
                const double cellMaxLat = std::min(cellMinLat + result.latStep, request.maxLat);
                cellArray.push_back({
                    {"gx", cell.gx},
                    {"gy", cell.gy},
                    {"minLon", cellMinLon},
                    {"minLat", cellMinLat},
                    {"maxLon", cellMaxLon},
                    {"maxLat", cellMaxLat},
                    {"pointCount", cell.pointCount},
                    {"vehicleCount", cell.vehicleCount},
                    {"vehicleDensity", cell.vehicleDensity},
                    {"flowIntensity", cell.flowIntensity},
                    {"deltaVehicleCount", cell.deltaVehicleCount},
                    {"deltaVehicleDensity", cell.deltaVehicleDensity},
                    {"deltaRate", cell.deltaRate}
                });
            }

            bucketArray.push_back({
                {"startTime", bucket.startTime},
                {"endTime", bucket.endTime},
                {"maxVehicleCount", bucket.maxVehicleCount},
                {"maxVehicleDensity", bucket.maxVehicleDensity},
                {"avgVehicleDensity", bucket.avgVehicleDensity},
                {"totalPointCount", static_cast<long long>(bucket.totalPointCount)},
                {"totalVehicleCount", bucket.totalVehicleCount},
                {"totalFlowDensity", bucket.totalFlowDensity},
                {"deltaRate", bucket.deltaRate},
                {"cells", cellArray}
            });
        }

        const json data = {
            {"minLon", request.minLon},
            {"minLat", request.minLat},
            {"maxLon", request.maxLon},
            {"maxLat", request.maxLat},
            {"regionSource", useClientRegion ? "selection" : "full-map"},
            {"totalPointCount", static_cast<long long>(result.totalPointCount)},
            {"totalVehicleCount", result.totalVehicleCount},
            {"elapsedSeconds", result.elapsedSeconds},
            {"lonStep", result.lonStep},
            {"latStep", result.latStep},
            {"cellAreaKm2", result.cellAreaKm2},
            {"columnCount", result.columnCount},
            {"rowCount", result.rowCount},
            {"bucketCount", result.bucketCount},
            {"gridCount", result.gridCount},
            {"analysisScale", result.analysisScale},
            {"maxVehicleDensity", result.maxVehicleDensity},
            {"buckets", bucketArray}
        };
        res.set_content(makeSuccess(data).dump(), "application/json; charset=utf-8");
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

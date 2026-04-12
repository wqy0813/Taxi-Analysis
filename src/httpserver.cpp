#include "httpserver.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QDebug>

#include <cmath>

#include "datamanager.h"
#include "densityanalysis.h"
#include "httplib.h"
#include "json.hpp"

using json = nlohmann::json;

namespace {

// 把统一的成功响应包一层，前端只需要看 success 和 data。
json makeSuccess(const json& data)
{
    return json{{"success", true}, {"data", data}};
}

// 把统一的错误响应包一层，方便前端直接展示 message。
json makeError(const std::string& code, const std::string& message)
{
    return json{
        {"success", false},
        {"error", {{"code", code}, {"message", message}}}
    };
}

// 解析 JSON 请求体，要求 body 必须是对象。
bool parseJsonBody(const httplib::Request& req, json& out, std::string& errorMessage)
{
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

void applyCorsHeaders(httplib::Response& res)
{
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Headers", "Content-Type");
    res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    res.set_header("Access-Control-Max-Age", "86400");
}

// 把单个 GPS 点转成前端能直接使用的 JSON。
json pointToJson(const GPSPoint& point)
{
    return json{
        {"id", point.id},
        {"timestamp", point.timestamp},
        {"lon", point.lon},
        {"lat", point.lat}
    };
}

// 兼容数字和字符串两种输入，统一转成 64 位整数。
long long toInt64(const json& value, long long defaultValue = 0)
{
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

bool tryReadDouble(const json& body, const char* key, double& out)
{
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

bool tryReadRegionBounds(
    const json& body,
    double& minLon,
    double& minLat,
    double& maxLon,
    double& maxLat)
{
    const bool hasMinLon = tryReadDouble(body, "minLon", minLon);
    const bool hasMinLat = tryReadDouble(body, "minLat", minLat);
    const bool hasMaxLon = tryReadDouble(body, "maxLon", maxLon);
    const bool hasMaxLat = tryReadDouble(body, "maxLat", maxLat);
    if (!(hasMinLon && hasMinLat && hasMaxLon && hasMaxLat)) {
        return false;
    }
    return minLon < maxLon && minLat < maxLat;
}

} // namespace

// 保存配置，后续启动 HTTP 服务时使用。
HttpServer::HttpServer(const AppConfig& config)
    : m_config(config)
{
}

// 优先使用源码树里的 web，避免构建目录里的旧副本干扰前端更新。
QString HttpServer::resolveWebRoot() const
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    QStringList roots;

    // 1) 编译期源码目录优先，开发联调时只认本地 web 三件套
#ifdef APP_SOURCE_WEB_ROOT
    roots << QString::fromUtf8(APP_SOURCE_WEB_ROOT);
#endif

    // 2) 配置路径：map_path 可以直接指向 index.html 或 web 目录
    if (!m_config.mapPath.isEmpty()) {
        const QFileInfo configuredPath(m_config.mapPath);
        if (configuredPath.isFile()) {
            roots << configuredPath.dir().absolutePath();
        } else {
            roots << configuredPath.absoluteFilePath();
        }
    }

    // 3) 常见运行目录候选
    roots << appDir.absoluteFilePath("../web");     // build/<target>/.. -> repo/web
    roots << QDir::current().absoluteFilePath("web");
    roots << appDir.absoluteFilePath("web");
    roots << appDir.absoluteFilePath("../../web");  // 兼容旧目录结构

    for (const QString& root : roots) {
        const QFileInfo indexInfo(QDir(root).absoluteFilePath("index.html"));
        if (indexInfo.exists()) {
            return QDir(root).absolutePath();
        }
    }

    // 回退到配置路径所在目录，便于日志定位问题
    if (!m_config.mapPath.isEmpty()) {
        const QFileInfo configuredPath(m_config.mapPath);
        return configuredPath.isFile() ? configuredPath.dir().absolutePath() : configuredPath.absoluteFilePath();
    }

    return appDir.absoluteFilePath("../web");
}

// 启动静态文件服务和所有 API 接口。
bool HttpServer::start(quint16 port)
{
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

    const QString webRoot = resolveWebRoot();
    const std::string webRootUtf8 = webRoot.toStdString();
    qInfo().noquote() << QString("Web root: %1").arg(webRoot);

    // 健康检查接口，只回答“服务是否活着”。
    server.Get("/api/health", [](const httplib::Request&, httplib::Response& res) {
        const json data = {
            {"status", "ok"},
            {"pointsLoaded", static_cast<long long>(DataManager::getAllPoints().size())}
        };
        res.set_content(makeSuccess(data).dump(), "application/json; charset=utf-8");
    });

    // 元数据接口，把地图边界、缩放级别和总点数发给前端。
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
            {"baiduMapAk", m_config.baiduMapAk.toStdString()}
        };
        res.set_content(makeSuccess(data).dump(), "application/json; charset=utf-8");
    });

    // 轨迹接口：
    // taxiId = 0 表示“当前视野内所有车辆”；
    // taxiId > 0 表示“某一辆车的完整轨迹”。
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
            // 全部车辆模式需要地图边界和缩放级别，边界决定查哪些点，缩放决定是原始点还是聚合点。
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

            // 先按视野范围取点，再根据缩放级别决定返回原始点还是聚合结果。
            const std::vector<GPSPoint> points = DataManager::querySpatial(minLon, minLat, maxLon, maxLat);
            const bool canRenderRaw = zoom >= 18 && points.size() <= 12000;
            if (canRenderRaw) {
                // 点数不多、缩放足够大时，直接给原始点，前端渲染更细。
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

            // 点太多时先聚合，减少浏览器压力。
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

        // 单车模式只返回该车辆的完整轨迹。
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

    // 区域查找接口：按矩形范围和时间范围一起筛点，并统计命中的车辆数。
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

        // 这一步就是区域查找的核心：
        // 先用空间 + 时间条件筛出所有点，再对车辆 ID 去重得到车辆数。
        QElapsedTimer timer;
        timer.start();
        const std::vector<GPSPoint> points = DataManager::querySpatialAndTime(minLon, minLat, maxLon, maxLat, startTime, endTime);
        const int vehicleCount = DataManager::getUniqueCountById(points);
        const double elapsedSeconds = static_cast<double>(timer.elapsed()) / 1000.0;

        const json data = {
            {"pointCount", static_cast<long long>(points.size())},
            {"vehicleCount", vehicleCount},
            {"elapsedSeconds", elapsedSeconds}
        };
        res.set_content(makeSuccess(data).dump(), "application/json; charset=utf-8");
    });

    // F4 密度分析接口。
    // 这里固定使用配置里的全图边界，不再依赖用户先框选区域。
    // 前端只需要给时间范围、网格边长 r 和时间粒度。
    // 密度分析接口：
    // 1) 优先使用前端框选区域；
    // 2) 若前端未传区域参数，则回退全图范围；
    // 3) 具体参数与规模保护在 DensityAnalyzer::analyze 中统一校验。
    server.Post("/api/density", [this](const httplib::Request& req, httplib::Response& res) {
        json body;
        std::string errorMessage;
        if (!parseJsonBody(req, body, errorMessage)) {
            res.status = 400;
            res.set_content(makeError("INVALID_JSON", errorMessage).dump(), "application/json; charset=utf-8");
            return;
        }

        DensityAnalysisRequest request;
        // 默认使用全图边界。
        request.minLon = m_config.minLon;
        request.minLat = m_config.minLat;
        request.maxLon = m_config.maxLon;
        request.maxLat = m_config.maxLat;

        // 前后端字段约定：
        // 如果请求体完整提供 minLon/minLat/maxLon/maxLat，则按该区域分析。
        // 若只提供一部分区域字段，直接判定为参数错误，避免语义歧义。
        double regionMinLon = 0.0;
        double regionMinLat = 0.0;
        double regionMaxLon = 0.0;
        double regionMaxLat = 0.0;
        const bool useClientRegion = tryReadRegionBounds(
            body, regionMinLon, regionMinLat, regionMaxLon, regionMaxLat);
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
            res.set_content(makeError("ANALYSIS_FAILED", result.errorMessage.toStdString()).dump(), "application/json; charset=utf-8");
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
            // 网格空间元信息：
            // 前端通过 minLon/minLat/lonStep/latStep + gx/gy 反算边界。
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

    // 把 web 目录挂到根路径下，静态资源和前端页面直接可访问。
    server.set_mount_point("/", webRootUtf8.c_str());
    server.Get("/", [webRootUtf8](const httplib::Request&, httplib::Response& res) {
        const QString indexPath = QDir(QString::fromStdString(webRootUtf8)).absoluteFilePath("index.html");
        if (!QFileInfo::exists(indexPath)) {
            res.status = 404;
            res.set_content(makeError("NOT_FOUND", "index.html not found").dump(), "application/json; charset=utf-8");
            return;
        }

        QFile file(indexPath);
        if (!file.open(QIODevice::ReadOnly)) {
            res.status = 500;
            res.set_content(makeError("FILE_ERROR", "cannot open index.html").dump(), "application/json; charset=utf-8");
            return;
        }

        res.set_content(file.readAll().toStdString(), "text/html; charset=utf-8");
    });

    const std::string host = "0.0.0.0";
    return server.listen(host.c_str(), port);
}

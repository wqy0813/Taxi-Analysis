#include "TrafficAnalysisSystem.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QUrl>
#include <QDebug>
#include <QDir>
#include <QWebEngineSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include "appconfig.h"

TrafficAnalysisSystem::TrafficAnalysisSystem(QWidget *parent)
    : QMainWindow(parent),
    centralWidget(nullptr),
    mainLayout(nullptr),
    buttonPanel(nullptr),
    buttonLayout(nullptr),
    btn1(nullptr),
    btn2(nullptr),
    btn3(nullptr),
    btn4(nullptr),
    btn5(nullptr),
    btn6(nullptr),
    webView(nullptr),
    mapReady(false)
{
    setWindowTitle("交通分析系统");
    resize(1400, 800);

    centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    mainLayout = new QHBoxLayout(centralWidget);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(10);

    // =========================
    // 左侧按钮区
    // =========================
    buttonPanel = new QWidget(centralWidget);
    buttonPanel->setFixedWidth(220);

    buttonLayout = new QVBoxLayout(buttonPanel);
    buttonLayout->setSpacing(10);
    buttonLayout->setContentsMargins(20, 20, 20, 20);

    btn1 = new QPushButton("查询轨迹", buttonPanel);
    btn2 = new QPushButton("区域查找", buttonPanel);
    btn3 = new QPushButton("车辆密度", buttonPanel);
    btn4 = new QPushButton("区域关联分析", buttonPanel);
    btn5 = new QPushButton("频繁路径分析", buttonPanel);
    btn6 = new QPushButton("通行时间分析", buttonPanel);

    buttonLayout->addWidget(btn1);
    buttonLayout->addWidget(btn2);
    buttonLayout->addWidget(btn3);
    buttonLayout->addWidget(btn4);
    buttonLayout->addWidget(btn5);
    buttonLayout->addWidget(btn6);
    buttonLayout->addStretch();

    // =========================
    // 右侧地图区
    // =========================
    webView = new QWebEngineView(centralWidget);

    // 允许本地 html 访问远程资源（例如百度地图 SDK）
    QWebEngineSettings *settings = webView->settings();
    settings->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);

    mainLayout->addWidget(buttonPanel);
    mainLayout->addWidget(webView, 1);

    connect(webView, &QWebEngineView::loadFinished, this, [this](bool ok) {
        mapReady = ok;
        qDebug() << "地图页面加载完成:" << ok;
    });

    connect(btn1, &QPushButton::clicked, this, &TrafficAnalysisSystem::onQueryTrajectory);
    connect(btn2, &QPushButton::clicked, this, &TrafficAnalysisSystem::onRegionSearch);
    connect(btn3, &QPushButton::clicked, this, &TrafficAnalysisSystem::onVehicleDensity);
    connect(btn4, &QPushButton::clicked, this, &TrafficAnalysisSystem::onRegionCorrelation);
    connect(btn5, &QPushButton::clicked, this, &TrafficAnalysisSystem::onFrequentPath);
    connect(btn6, &QPushButton::clicked, this, &TrafficAnalysisSystem::onTravelTimeAnalysis);

    loadMap();
}

TrafficAnalysisSystem::~TrafficAnalysisSystem()
{
}

void TrafficAnalysisSystem::loadMap()
{
    QString configPath = QDir::currentPath() + "/config.ini";
    AppConfig config = AppConfig::load(configPath);

    QString htmlPath = config.mapPath;
    QFileInfo fileInfo(htmlPath);

    if (!fileInfo.exists()) {
        qDebug() << "map.html 不存在:" << htmlPath;
        return;
    }

    QUrl url = QUrl::fromLocalFile(fileInfo.absoluteFilePath());
    webView->load(url);

    qDebug() << "地图页面加载路径:" << url.toString();
}

void TrafficAnalysisSystem::runJs(const QString& jsCode)
{
    if (!webView || !webView->page()) {
        qDebug() << "Web 页面未初始化";
        return;
    }

    if (!mapReady) {
        qDebug() << "地图尚未加载完成";
        return;
    }

    webView->page()->runJavaScript(jsCode);
}

std::vector<GPSPoint> TrafficAnalysisSystem::samplePoints(const std::vector<GPSPoint>& points, size_t maxPoints) const
{
    if (points.empty() || maxPoints == 0) {
        return {};
    }

    if (points.size() <= maxPoints) {
        return points;
    }

    std::vector<GPSPoint> result;
    result.reserve(maxPoints);

    double step = static_cast<double>(points.size() - 1) / static_cast<double>(maxPoints - 1);

    for (size_t i = 0; i < maxPoints; ++i) {
        size_t idx = static_cast<size_t>(i * step);
        if (idx >= points.size()) {
            idx = points.size() - 1;
        }
        result.push_back(points[idx]);
    }

    return result;
}
void TrafficAnalysisSystem::requestViewBounds(std::function<void(double, double, double, double)> callback)
{
    if (!webView || !webView->page()) {
        qDebug() << "Web 页面未初始化";
        return;
    }

    if (!mapReady) {
        qDebug() << "地图尚未加载完成";
        return;
    }

    webView->page()->runJavaScript(
        "getViewBoundsJson();",
        [callback](const QVariant &result)
        {
            QString json = result.toString();

            if (json.isEmpty()) {
                qDebug() << "没有拿到地图范围";
                return;
            }

            QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
            if (!doc.isObject()) {
                qDebug() << "地图范围 JSON 解析失败";
                return;
            }

            QJsonObject obj = doc.object();

            double minLon = obj.value("minLon").toDouble();
            double minLat = obj.value("minLat").toDouble();
            double maxLon = obj.value("maxLon").toDouble();
            double maxLat = obj.value("maxLat").toDouble();

            callback(minLon, minLat, maxLon, maxLat);
        }
        );
}
QString TrafficAnalysisSystem::pointsToJsArray(const std::vector<GPSPoint>& points, size_t maxPoints) const
{
    std::vector<GPSPoint> sampled = samplePoints(points, maxPoints);

    QStringList items;
    items.reserve(static_cast<int>(sampled.size()));

    for (const auto& p : sampled) {
        items << QString("{lng:%1,lat:%2}")
        .arg(p.lon, 0, 'f', 8)
            .arg(p.lat, 0, 'f', 8);
    }

    return "[" + items.join(",") + "]";
}

void TrafficAnalysisSystem::clearMap()
{
    runJs("clearMap();");
}

void TrafficAnalysisSystem::showRect(double minLon, double minLat, double maxLon, double maxLat)
{
    // 前端 drawRect(leftTop, rightBottom)
    // leftTop     = (minLon, maxLat)
    // rightBottom = (maxLon, minLat)
    QString js = QString(
                     "drawRect({lng:%1,lat:%2},{lng:%3,lat:%4});"
                     ).arg(minLon, 0, 'f', 8)
                     .arg(maxLat, 0, 'f', 8)
                     .arg(maxLon, 0, 'f', 8)
                     .arg(minLat, 0, 'f', 8);

    runJs(js);
}

void TrafficAnalysisSystem::showTrajectory(const std::vector<GPSPoint>& points)
{
    if (points.empty()) {
        qDebug() << "showTrajectory: 轨迹点为空";
        return;
    }

    // 轨迹最多传 2000 个点到前端
    QString arr = pointsToJsArray(points, 2000);
    QString js = "addPolyline(" + arr + ");";

    runJs(js);

    qDebug() << "showTrajectory: 原始点数 =" << points.size();
}

void TrafficAnalysisSystem::showPoints(const std::vector<GPSPoint>& points)
{
    if (points.empty()) {
        qDebug() << "showPoints: 点集为空";
        return;
    }

    // marker 太多很容易卡，这里限制到 1500
    QString arr = pointsToJsArray(points, 1500);
    QString js = "addPoints(" + arr + ");";

    runJs(js);

    qDebug() << "showPoints: 原始点数 =" << points.size();
}

void TrafficAnalysisSystem::fitViewToPoints(const std::vector<GPSPoint>& points)
{
    if (points.empty()) {
        qDebug() << "fitViewToPoints: 点集为空";
        return;
    }

    // fit 用于视野计算，可以适当多一点，但也别太大
    QString arr = pointsToJsArray(points, 3000);
    QString js = "fitViewToPoints(" + arr + ");";

    runJs(js);
}

void TrafficAnalysisSystem::fitViewToBounds(double minLon, double minLat, double maxLon, double maxLat)
{
    std::vector<GPSPoint> boundsPoints;
    boundsPoints.push_back({0, 0, minLon, minLat});
    boundsPoints.push_back({0, 0, maxLon, maxLat});

    fitViewToPoints(boundsPoints);
}

// =========================
// 下面这 6 个槽函数现在只是演示/占位
// 后面你接数据库查询时，替换成真实数据逻辑
// =========================

void TrafficAnalysisSystem::onQueryTrajectory()
{
    qDebug() << "点击了：查询轨迹";

    std::vector<GPSPoint> demo;
    demo.push_back({1, 0, 116.40400000, 39.91500000});
    demo.push_back({1, 0, 116.41400000, 39.92500000});
    demo.push_back({1, 0, 116.42400000, 39.93500000});
    demo.push_back({1, 0, 116.43400000, 39.94500000});

    clearMap();
    showTrajectory(demo);
    fitViewToPoints(demo);
}

void TrafficAnalysisSystem::onRegionSearch()
{
    qDebug() << "点击了：区域查找";

    clearMap();
    showRect(115.9, 39.7, 116.6, 40.1);
    fitViewToBounds(115.9, 39.7, 116.6, 40.1);
}

void TrafficAnalysisSystem::onVehicleDensity()
{
    qDebug() << "点击了：车辆密度";

    std::vector<GPSPoint> demo;
    demo.push_back({1, 0, 116.40400000, 39.91500000});
    demo.push_back({2, 0, 116.40600000, 39.91700000});
    demo.push_back({3, 0, 116.40900000, 39.92000000});
    demo.push_back({4, 0, 116.41300000, 39.91800000});
    demo.push_back({5, 0, 116.41700000, 39.92300000});

    clearMap();
    showPoints(demo);
    fitViewToPoints(demo);
}

void TrafficAnalysisSystem::onRegionCorrelation()
{
    qDebug() << "点击了：区域关联分析";

    clearMap();

    // 演示：叠加显示两个矩形，不再互相清掉
    showRect(116.00, 39.80, 116.20, 39.95);
    showRect(116.15, 39.90, 116.35, 40.05);

    std::vector<GPSPoint> bounds;
    bounds.push_back({0, 0, 116.00, 39.80});
    bounds.push_back({0, 0, 116.35, 40.05});
    fitViewToPoints(bounds);
}

void TrafficAnalysisSystem::onFrequentPath()
{
    qDebug() << "点击了：频繁路径分析";

    clearMap();

    // 演示：叠加两条轨迹
    std::vector<GPSPoint> traj1;
    traj1.push_back({10, 0, 116.38000000, 39.90000000});
    traj1.push_back({10, 0, 116.39000000, 39.90500000});
    traj1.push_back({10, 0, 116.40500000, 39.91500000});
    traj1.push_back({10, 0, 116.42000000, 39.93000000});
    traj1.push_back({10, 0, 116.43500000, 39.94000000});

    std::vector<GPSPoint> traj2;
    traj2.push_back({11, 0, 116.36000000, 39.89000000});
    traj2.push_back({11, 0, 116.37500000, 39.90000000});
    traj2.push_back({11, 0, 116.39000000, 39.91000000});
    traj2.push_back({11, 0, 116.41000000, 39.92000000});
    traj2.push_back({11, 0, 116.43000000, 39.93500000});

    showTrajectory(traj1);
    showTrajectory(traj2);

    std::vector<GPSPoint> allPoints = traj1;
    allPoints.insert(allPoints.end(), traj2.begin(), traj2.end());
    fitViewToPoints(allPoints);
}

void TrafficAnalysisSystem::onTravelTimeAnalysis()
{
    qDebug() << "点击了：通行时间分析";
    clearMap();
}
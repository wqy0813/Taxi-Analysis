#include "TrafficAnalysisSystem.h"

#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>
#include <QVariant>

#include <climits>
#include <cmath>
#include <vector>

#include <QWebEngineSettings>
#include "appconfig.h"

RegionSearchDialog::RegionSearchDialog(const QDateTime &startTime,
                                       const QDateTime &endTime,
                                       double minLon,
                                       double minLat,
                                       double maxLon,
                                       double maxLat,
                                       QWidget *parent)
    : QDialog(parent),
    startTimeEdit(new QDateTimeEdit(startTime, this)),
    endTimeEdit(new QDateTimeEdit(endTime, this)),
    minLonEdit(new QDoubleSpinBox(this)),
    minLatEdit(new QDoubleSpinBox(this)),
    maxLonEdit(new QDoubleSpinBox(this)),
    maxLatEdit(new QDoubleSpinBox(this))
{
    setWindowTitle(QStringLiteral("区域查找"));
    resize(680, 430);

    startTimeEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    endTimeEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    startTimeEdit->setCalendarPopup(true);
    endTimeEdit->setCalendarPopup(true);

    const auto setupSpin = [](QDoubleSpinBox *spin, double minValue, double maxValue, double value) {
        spin->setDecimals(6);
        spin->setRange(minValue, maxValue);
        spin->setSingleStep(0.01);
        spin->setValue(value);
        spin->setAlignment(Qt::AlignLeft);
    };

    setupSpin(minLonEdit, 115.0, 118.0, minLon);
    setupSpin(minLatEdit, 39.0, 41.0, minLat);
    setupSpin(maxLonEdit, 115.0, 118.0, maxLon);
    setupSpin(maxLatEdit, 39.0, 41.0, maxLat);

    auto *layout = new QVBoxLayout(this);

    auto *titleLabel = new QLabel(QStringLiteral("区域范围查找"), this);
    titleLabel->setStyleSheet(QStringLiteral("font-size: 24px; font-weight: 600;"));

    auto *descLabel = new QLabel(
        QStringLiteral("统计指定时间段内，用户给定矩形区域中的出租车数量。\n"
                       "轨迹范围建议控制在北纬 39.0~41.0、东经 115.0~118.0 之间。"),
        this);
    descLabel->setWordWrap(true);
    descLabel->setStyleSheet(QStringLiteral("color: #555; font-size: 14px;"));

    auto *timeGroup = new QGroupBox(QStringLiteral("时间范围"), this);
    auto *timeLayout = new QGridLayout(timeGroup);
    timeLayout->addWidget(new QLabel(QStringLiteral("开始时间"), timeGroup), 0, 0);
    timeLayout->addWidget(startTimeEdit, 1, 0);
    timeLayout->addWidget(new QLabel(QStringLiteral("结束时间"), timeGroup), 0, 1);
    timeLayout->addWidget(endTimeEdit, 1, 1);

    auto *regionGroup = new QGroupBox(QStringLiteral("矩形区域"), this);
    auto *regionLayout = new QGridLayout(regionGroup);
    regionLayout->addWidget(new QLabel(QStringLiteral("左上角经度"), regionGroup), 0, 0);
    regionLayout->addWidget(minLonEdit, 1, 0);
    regionLayout->addWidget(new QLabel(QStringLiteral("左上角纬度"), regionGroup), 0, 1);
    regionLayout->addWidget(maxLatEdit, 1, 1);
    regionLayout->addWidget(new QLabel(QStringLiteral("右下角经度"), regionGroup), 2, 0);
    regionLayout->addWidget(maxLonEdit, 3, 0);
    regionLayout->addWidget(new QLabel(QStringLiteral("右下角纬度"), regionGroup), 2, 1);
    regionLayout->addWidget(minLatEdit, 3, 1);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttonBox->button(QDialogButtonBox::Ok)->setText(QStringLiteral("查询"));
    buttonBox->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    layout->addWidget(titleLabel);
    layout->addWidget(descLabel);
    layout->addWidget(timeGroup);
    layout->addWidget(regionGroup);
    layout->addWidget(buttonBox);
}

QDateTime RegionSearchDialog::startTime() const { return startTimeEdit->dateTime(); }
QDateTime RegionSearchDialog::endTime() const { return endTimeEdit->dateTime(); }
double RegionSearchDialog::minLon() const { return minLonEdit->value(); }
double RegionSearchDialog::minLat() const { return minLatEdit->value(); }
double RegionSearchDialog::maxLon() const { return maxLonEdit->value(); }
double RegionSearchDialog::maxLat() const { return maxLatEdit->value(); }

TrafficAnalysisSystem::TrafficAnalysisSystem(DatabaseManager *dbManager, QWidget *parent)
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
    mapReady(false),
    dbManager(dbManager),
    cachedTaxiId(-1),
    hasCachedRegionQuery(false),
    cachedRegionStartTime(0),
    cachedRegionEndTime(0),
    cachedRegionMinLon(0.0),
    cachedRegionMinLat(0.0),
    cachedRegionMaxLon(0.0),
    cachedRegionMaxLat(0.0),
    cachedRegionResult(0),
    allTaxiModeActive(false),
    viewportSyncTimer(new QTimer(this)),
    hasLastViewportState(false),
    lastViewportMinLon(0.0),
    lastViewportMinLat(0.0),
    lastViewportMaxLon(0.0),
    lastViewportMaxLat(0.0),
    lastViewportZoom(-1)
{
    setWindowTitle(QStringLiteral("交通分析系统"));
    resize(1400, 800);

    if (!dbManager) {
        QMessageBox::critical(this, QStringLiteral("数据库错误"),
                              QStringLiteral("数据库管理器为空。"));
    }

    centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    mainLayout = new QHBoxLayout(centralWidget);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(10);

    buttonPanel = new QWidget(centralWidget);
    buttonPanel->setFixedWidth(220);

    buttonLayout = new QVBoxLayout(buttonPanel);
    buttonLayout->setSpacing(10);
    buttonLayout->setContentsMargins(20, 20, 20, 20);

    btn1 = new QPushButton(QStringLiteral("查询轨迹"), buttonPanel);
    btn2 = new QPushButton(QStringLiteral("区域查找"), buttonPanel);
    btn3 = new QPushButton(QStringLiteral("车辆密度"), buttonPanel);
    btn4 = new QPushButton(QStringLiteral("区域关联分析"), buttonPanel);
    btn5 = new QPushButton(QStringLiteral("频繁路径分析"), buttonPanel);
    btn6 = new QPushButton(QStringLiteral("通行时间分析"), buttonPanel);

    buttonLayout->addWidget(btn1);
    buttonLayout->addWidget(btn2);
    buttonLayout->addWidget(btn3);
    buttonLayout->addWidget(btn4);
    buttonLayout->addWidget(btn5);
    buttonLayout->addWidget(btn6);
    buttonLayout->addStretch();

    webView = new QWebEngineView(centralWidget);
    QWebEngineSettings *settings = webView->settings();
    settings->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);

    mainLayout->addWidget(buttonPanel);
    mainLayout->addWidget(webView, 1);

    connect(webView, &QWebEngineView::loadFinished, this, [this](bool ok) {
        mapReady = ok;
        qDebug() << "Map page loaded:" << ok;
    });

    connect(btn1, &QPushButton::clicked, this, &TrafficAnalysisSystem::onQueryTrajectory);
    connect(btn2, &QPushButton::clicked, this, &TrafficAnalysisSystem::onRegionSearch);
    connect(btn3, &QPushButton::clicked, this, &TrafficAnalysisSystem::onVehicleDensity);
    connect(btn4, &QPushButton::clicked, this, &TrafficAnalysisSystem::onRegionCorrelation);
    connect(btn5, &QPushButton::clicked, this, &TrafficAnalysisSystem::onFrequentPath);
    connect(btn6, &QPushButton::clicked, this, &TrafficAnalysisSystem::onTravelTimeAnalysis);

    viewportSyncTimer->setInterval(250);
    connect(viewportSyncTimer, &QTimer::timeout, this, [this]() {
        if (!allTaxiModeActive) {
            return;
        }

        requestViewState([this](double minLon, double minLat, double maxLon, double maxLat, int zoom) {
            const auto changed = [this, minLon, minLat, maxLon, maxLat, zoom]() {
                if (!hasLastViewportState) {
                    return true;
                }

                const double eps = 1e-6;
                return std::fabs(lastViewportMinLon - minLon) > eps ||
                       std::fabs(lastViewportMinLat - minLat) > eps ||
                       std::fabs(lastViewportMaxLon - maxLon) > eps ||
                       std::fabs(lastViewportMaxLat - maxLat) > eps ||
                       lastViewportZoom != zoom;
            };

            if (!changed()) {
                return;
            }

            showAllTaxiPoints(minLon, minLat, maxLon, maxLat, zoom);
        });
    });

    loadMap();
}

TrafficAnalysisSystem::~TrafficAnalysisSystem() = default;

void TrafficAnalysisSystem::loadMap()
{
    const AppConfig& config = AppConfigManager::get();
    const QString htmlPath = config.mapPath;
    QFileInfo fileInfo(htmlPath);

    if (!fileInfo.exists()) {
        qDebug() << "map.html does not exist:" << htmlPath;
        return;
    }

    const QUrl url = QUrl::fromLocalFile(fileInfo.absoluteFilePath());
    webView->load(url);
    qDebug() << "Loading map page:" << url.toString();
}

void TrafficAnalysisSystem::runJs(const QString& jsCode)
{
    if (!webView || !webView->page()) {
        qDebug() << "Web page is not initialized.";
        return;
    }

    if (!mapReady) {
        qDebug() << "Map page is not ready.";
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

    const double step = static_cast<double>(points.size() - 1) / static_cast<double>(maxPoints - 1);
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
        qDebug() << "Web page is not initialized.";
        return;
    }

    if (!mapReady) {
        qDebug() << "Map page is not ready.";
        return;
    }

    webView->page()->runJavaScript(
        "getViewBoundsJson();",
        [callback](const QVariant &result)
        {
            const QString json = result.toString();
            if (json.isEmpty()) {
                qDebug() << "No map bounds were returned.";
                return;
            }

            const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
            if (!doc.isObject()) {
                qDebug() << "Failed to parse map bounds JSON.";
                return;
            }

            const QJsonObject obj = doc.object();
            callback(obj.value("minLon").toDouble(),
                     obj.value("minLat").toDouble(),
                     obj.value("maxLon").toDouble(),
                     obj.value("maxLat").toDouble());
        }
        );
}

void TrafficAnalysisSystem::requestViewState(std::function<void(double, double, double, double, int)> callback)
{
    if (!webView || !webView->page()) {
        qDebug() << "Web page is not initialized.";
        return;
    }

    if (!mapReady) {
        qDebug() << "Map page is not ready.";
        return;
    }

    webView->page()->runJavaScript(
        "getViewStateJson();",
        [callback](const QVariant &result)
        {
            const QString json = result.toString();
            if (json.isEmpty()) {
                qDebug() << "No map view state was returned.";
                return;
            }

            const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
            if (!doc.isObject()) {
                qDebug() << "Failed to parse map view state JSON.";
                return;
            }

            const QJsonObject obj = doc.object();
            callback(obj.value("minLon").toDouble(),
                     obj.value("minLat").toDouble(),
                     obj.value("maxLon").toDouble(),
                     obj.value("maxLat").toDouble(),
                     obj.value("zoom").toInt(-1));
        }
        );
}

QString TrafficAnalysisSystem::pointsToJsArray(const std::vector<GPSPoint>& points, size_t maxPoints) const
{
    const std::vector<GPSPoint> sampled = samplePoints(points, maxPoints);

    QStringList items;
    items.reserve(static_cast<int>(sampled.size()));

    for (const auto& p : sampled) {
        items << QString("{lng:%1,lat:%2}")
        .arg(p.lon, 0, 'f', 8)
            .arg(p.lat, 0, 'f', 8);
    }

    return "[" + items.join(",") + "]";
}

QString TrafficAnalysisSystem::clusterPointsToJsArray(const std::vector<ClusterPoint>& points) const
{
    QStringList items;
    items.reserve(static_cast<int>(points.size()));

    for (const auto& p : points) {
        QStringList childItems;
        childItems.reserve(static_cast<int>(p.children.size()));

        for (const auto& child : p.children) {
            childItems << QString("{lng:%1,lat:%2}")
            .arg(child.lon, 0, 'f', 8)
                .arg(child.lat, 0, 'f', 8);
        }

        items << QString("{lng:%1,lat:%2,count:%3,isCluster:%4,minLon:%5,minLat:%6,maxLon:%7,maxLat:%8,children:[%9]}")
                     .arg(p.lon, 0, 'f', 8)
                     .arg(p.lat, 0, 'f', 8)
                     .arg(p.count)
                     .arg(p.isCluster ? "true" : "false")
                     .arg(p.minLon, 0, 'f', 8)
                     .arg(p.minLat, 0, 'f', 8)
                     .arg(p.maxLon, 0, 'f', 8)
                     .arg(p.maxLat, 0, 'f', 8)
                     .arg(childItems.join(","));
    }

    return "[" + items.join(",") + "]";
}
void TrafficAnalysisSystem::clearMap()
{
    runJs("clearMap();");
}

void TrafficAnalysisSystem::showRect(double minLon, double minLat, double maxLon, double maxLat)
{
    const QString js = QString("drawRect({lng:%1,lat:%2},{lng:%3,lat:%4});")
    .arg(minLon, 0, 'f', 8)
        .arg(maxLat, 0, 'f', 8)
        .arg(maxLon, 0, 'f', 8)
        .arg(minLat, 0, 'f', 8);
    runJs(js);
}

void TrafficAnalysisSystem::showTrajectory(const std::vector<GPSPoint>& points)
{
    if (points.empty()) {
        qDebug() << "showTrajectory received empty points.";
        return;
    }

    const QString arr = pointsToJsArray(points, 1200);
    runJs("addPolyline(" + arr + ");");
}

void TrafficAnalysisSystem::showPoints(const std::vector<GPSPoint>& points)
{
    if (points.empty()) {
        qDebug() << "showPoints received empty points.";
        return;
    }

    const QString arr = pointsToJsArray(points, 800);
    runJs("addPoints(" + arr + ");");
}

void TrafficAnalysisSystem::showClusteredPoints(const std::vector<ClusterPoint>& points)
{
    const QString arr = clusterPointsToJsArray(points);
    runJs("setClusterPoints(" + arr + ");");
}

void TrafficAnalysisSystem::fitViewToPoints(const std::vector<GPSPoint>& points)
{
    if (points.empty()) {
        qDebug() << "fitViewToPoints received empty points.";
        return;
    }

    const QString arr = pointsToJsArray(points, 3000);
    runJs("fitViewToPoints(" + arr + ");");
}

void TrafficAnalysisSystem::fitViewToBounds(double minLon, double minLat, double maxLon, double maxLat)
{
    std::vector<GPSPoint> boundsPoints;
    boundsPoints.push_back({0, 0, minLon, minLat});
    boundsPoints.push_back({0, 0, maxLon, maxLat});
    fitViewToPoints(boundsPoints);
}

void TrafficAnalysisSystem::onQueryTrajectory()
{
    bool ok = false;
    const int taxiId = QInputDialog::getInt(
        this,
        QStringLiteral("轨迹查询"),
        QStringLiteral("请输入出租车 ID（输入 0 表示全部出租车）："),
        0,
        0,
        100000000,
        1,
        &ok
        );

    if (!ok) {
        return;
    }

    if (taxiId == 0) {
        allTaxiModeActive = true;
        hasLastViewportState = false;
        viewportSyncTimer->start();

        requestViewState([this](double minLon, double minLat, double maxLon, double maxLat, int zoom) {
            showAllTaxiPoints(minLon, minLat, maxLon, maxLat, zoom);
        });
        return;
    }

    allTaxiModeActive = false;
    viewportSyncTimer->stop();
    showTaxiTrajectory(taxiId);
}

void TrafficAnalysisSystem::onRegionSearch()
{
    if (!dbManager) {
        QMessageBox::warning(this, QStringLiteral("区域查找"),
                             QStringLiteral("数据库管理器未初始化。"));
        return;
    }

    const qint64 defaultMinTime = QDateTime::fromString(QStringLiteral("2008-01-01 00:00:00"),
                                                        QStringLiteral("yyyy-MM-dd HH:mm:ss")).toSecsSinceEpoch();
    const qint64 defaultMaxTime = QDateTime::fromString(QStringLiteral("2008-12-31 23:59:59"),
                                                        QStringLiteral("yyyy-MM-dd HH:mm:ss")).toSecsSinceEpoch();

    qint64 datasetMinTime = defaultMinTime;
    qint64 datasetMaxTime = defaultMaxTime;
    double datasetMinLon = 115.0;
    double datasetMinLat = 39.0;
    double datasetMaxLon = 118.0;
    double datasetMaxLat = 41.0;

    RegionSearchDialog dialog(QDateTime::fromSecsSinceEpoch(datasetMinTime),
                              QDateTime::fromSecsSinceEpoch(datasetMaxTime),
                              datasetMinLon, datasetMinLat,
                              datasetMaxLon, datasetMaxLat,
                              this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    allTaxiModeActive = false;
    viewportSyncTimer->stop();

    SimpleTimer timer = SimpleTimer("区域查询", true);

    const double minLon = dialog.minLon();
    const double minLat = dialog.minLat();
    const double maxLon = dialog.maxLon();
    const double maxLat = dialog.maxLat();

    if (minLon >= maxLon || minLat >= maxLat) {
        QMessageBox::warning(this, QStringLiteral("区域查找"),
                             QStringLiteral("矩形范围错误，请确保左上角和右下角坐标有效。"));
        return;
    }

    const QDateTime startDt = dialog.startTime();
    const QDateTime endDt = dialog.endTime();
    const long long startTime = startDt.toSecsSinceEpoch();
    const long long endTime = endDt.toSecsSinceEpoch();

    qint64 count = 0;
    if (hasCachedRegionQuery &&
        cachedRegionStartTime == startTime &&
        cachedRegionEndTime == endTime &&
        cachedRegionMinLon == minLon &&
        cachedRegionMinLat == minLat &&
        cachedRegionMaxLon == maxLon &&
        cachedRegionMaxLat == maxLat) {
        count = cachedRegionResult;
    } else {
        std::vector<GPSPoint> points = DataManager::querySpatialAndTime(minLon, minLat, maxLon, maxLat,
                                                                        startTime, endTime);
        timer.print("命中结果");
        count = DataManager::getUniqueCountById(points);
        timer.print("id去重");

        if (count >= 0) {
            hasCachedRegionQuery = true;
            cachedRegionStartTime = startTime;
            cachedRegionEndTime = endTime;
            cachedRegionMinLon = minLon;
            cachedRegionMinLat = minLat;
            cachedRegionMaxLon = maxLon;
            cachedRegionMaxLat = maxLat;
            cachedRegionResult = count;
        }
    }

    timer.stop();

    if (count < 0) {
        QMessageBox::warning(this, QStringLiteral("区域查找"),
                             QStringLiteral("查询失败。"));
        return;
    }

    clearMap();
    showRect(minLon, minLat, maxLon, maxLat);
    fitViewToBounds(minLon, minLat, maxLon, maxLat);

    QMessageBox::information(
        this,
        QStringLiteral("区域查找结果"),
        QStringLiteral("指定时间段内，矩形区域中共有 %1 辆出租车。").arg(count)
        );
}

void TrafficAnalysisSystem::onVehicleDensity()
{
    QMessageBox::information(this,
                             QStringLiteral("提示"),
                             QStringLiteral("当前“车辆密度”已改为后端聚合模式。\n请在“查询轨迹”中输入 0 查看全部出租车聚合结果。"));
}

void TrafficAnalysisSystem::onRegionCorrelation()
{
    allTaxiModeActive = false;
    viewportSyncTimer->stop();

    showRect(116.00, 39.80, 116.20, 39.95);
    showRect(116.15, 39.90, 116.35, 40.05);

    std::vector<GPSPoint> bounds;
    bounds.push_back({0, 0, 116.00, 39.80});
    bounds.push_back({0, 0, 116.35, 40.05});
    fitViewToPoints(bounds);
}

void TrafficAnalysisSystem::onFrequentPath()
{
    allTaxiModeActive = false;
    viewportSyncTimer->stop();

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
    allTaxiModeActive = false;
    viewportSyncTimer->stop();
    clearMap();
}

void TrafficAnalysisSystem::showTaxiTrajectory(int taxiId)
{
    if (!dbManager) {
        QMessageBox::warning(this, QStringLiteral("轨迹查询"),
                             QStringLiteral("数据库管理器未初始化。"));
        return;
    }

    const std::vector<GPSPoint>* trajectory = nullptr;
    if (cachedTaxiId == taxiId && !cachedTrajectory.empty()) {
        trajectory = &cachedTrajectory;
    } else {
        SimpleTimer timer("id查询", true);
        cachedTrajectory = DataManager::getPointsRangeById(taxiId);
        timer.stop();
        cachedTaxiId = taxiId;
        trajectory = &cachedTrajectory;
    }

    if (trajectory->empty()) {
        QMessageBox::information(this, QStringLiteral("轨迹查询"),
                                 QStringLiteral("未查询到该出租车的轨迹数据。"));
        return;
    }

    clearMap();
    showTrajectory(*trajectory);
    fitViewToPoints(*trajectory);

    qDebug() << "F1 trajectory query completed. taxiId =" << taxiId
             << ", pointCount =" << trajectory->size();
}

void TrafficAnalysisSystem::showAllTaxiPoints(double minLon, double minLat, double maxLon, double maxLat, int zoom)
{
    qDebug() << "当前视野:" << minLon << "," << minLat << "-" << maxLon << "," << maxLat
             << ", zoom =" << zoom;

    if (!dbManager) {
        QMessageBox::warning(this, QStringLiteral("轨迹查询"),
                             QStringLiteral("数据库管理器未初始化。"));
        return;
    }

    std::vector<GPSPoint> points = DataManager::querySpatial(minLon, minLat, maxLon, maxLat);
    std::vector<ClusterPoint> clustered = DataManager::clusterPointsForView(points,
                                                                            minLon, minLat,
                                                                            maxLon, maxLat,
                                                                            zoom);

    cachedAllPoints = std::move(points);

    clearMap();
    showClusteredPoints(clustered);

    hasLastViewportState = true;
    lastViewportMinLon = minLon;
    lastViewportMinLat = minLat;
    lastViewportMaxLon = maxLon;
    lastViewportMaxLat = maxLat;
    lastViewportZoom = zoom;

    qDebug() << "后端聚合显示完成。原始点数 =" << cachedAllPoints.size()
             << ", 聚合后对象数 =" << clustered.size();
}
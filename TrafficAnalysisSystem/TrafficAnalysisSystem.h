#ifndef TRAFFICANALYSISSYSTEM_H
#define TRAFFICANALYSISSYSTEM_H

#include <QMainWindow>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QString>
#include <QStringList>
#include <QtWebEngineWidgets/QWebEngineView>
#include <functional>
#include <vector>
#include "datamanager.h"

class TrafficAnalysisSystem : public QMainWindow
{
    Q_OBJECT

public:
    explicit TrafficAnalysisSystem(QWidget *parent = nullptr);
    ~TrafficAnalysisSystem();

    // =========================
    // 地图渲染接口
    // 注意：这些函数默认只“添加显示”，不主动清图，不主动缩放
    // =========================

    // 清空所有覆盖物
    void clearMap();

    // 显示一个矩形区域（只画，不清图）
    void showRect(double minLon, double minLat, double maxLon, double maxLat);

    // 显示一条轨迹（只画，不清图）
    void showTrajectory(const std::vector<GPSPoint>& points);

    // 显示一批点（只画，不清图）
    void showPoints(const std::vector<GPSPoint>& points);

    // 视野控制（按需手动调用）
    void fitViewToPoints(const std::vector<GPSPoint>& points);
    void fitViewToBounds(double minLon, double minLat, double maxLon, double maxLat);
    void requestViewBounds(std::function<void(double, double, double, double)> callback);
private slots:
    // 原来的 6 个按钮
    void onQueryTrajectory();
    void onRegionSearch();
    void onVehicleDensity();
    void onRegionCorrelation();
    void onFrequentPath();
    void onTravelTimeAnalysis();

private:
    void loadMap();
    void runJs(const QString& jsCode);

    // 数据处理辅助函数
    std::vector<GPSPoint> samplePoints(const std::vector<GPSPoint>& points, size_t maxPoints) const;
    QString pointsToJsArray(const std::vector<GPSPoint>& points, size_t maxPoints) const;

private:
    QWidget *centralWidget;

    // 左边按钮区 + 右边地图区
    QHBoxLayout *mainLayout;

    QWidget *buttonPanel;
    QVBoxLayout *buttonLayout;

    QPushButton *btn1;
    QPushButton *btn2;
    QPushButton *btn3;
    QPushButton *btn4;
    QPushButton *btn5;
    QPushButton *btn6;

    QWebEngineView *webView;
    bool mapReady;
};

#endif // TRAFFICANALYSISSYSTEM_H
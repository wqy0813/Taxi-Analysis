#ifndef TRAFFICANALYSISSYSTEM_H
#define TRAFFICANALYSISSYSTEM_H

#include <QHBoxLayout>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDateTimeEdit>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QGridLayout>
#include <QGroupBox>
#include <QMainWindow>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>
#include <QtWebEngineWidgets/QWebEngineView>
#include <functional>
#include <vector>
#include "databasemanager.h"
#include "datamanager.h"
#include "utils.h"
class RegionSearchDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RegionSearchDialog(const QDateTime &startTime,
                                const QDateTime &endTime,
                                double minLon,
                                double minLat,
                                double maxLon,
                                double maxLat,
                                QWidget *parent = nullptr);

    QDateTime startTime() const;
    QDateTime endTime() const;
    double minLon() const;
    double minLat() const;
    double maxLon() const;
    double maxLat() const;

private:
    QDateTimeEdit *startTimeEdit;
    QDateTimeEdit *endTimeEdit;
    QDoubleSpinBox *minLonEdit;
    QDoubleSpinBox *minLatEdit;
    QDoubleSpinBox *maxLonEdit;
    QDoubleSpinBox *maxLatEdit;
};

class TrafficAnalysisSystem : public QMainWindow
{
    Q_OBJECT

public:
    explicit TrafficAnalysisSystem(DatabaseManager *dbManager, QWidget *parent = nullptr);
    ~TrafficAnalysisSystem();

    void clearMap();
    void showRect(double minLon, double minLat, double maxLon, double maxLat);
    void showTrajectory(const std::vector<GPSPoint>& points);
    void showPoints(const std::vector<GPSPoint>& points);
    void fitViewToPoints(const std::vector<GPSPoint>& points);
    void fitViewToBounds(double minLon, double minLat, double maxLon, double maxLat);
    void requestViewBounds(std::function<void(double, double, double, double)> callback);

private slots:
    void onQueryTrajectory();
    void onRegionSearch();
    void onVehicleDensity();
    void onRegionCorrelation();
    void onFrequentPath();
    void onTravelTimeAnalysis();

private:
    void loadMap();
    void runJs(const QString& jsCode);
    void showTaxiTrajectory(int taxiId);
    void showAllTaxiPoints(double minLon,double minlat ,double maxLon,double maxlat);

    std::vector<GPSPoint> samplePoints(const std::vector<GPSPoint>& points, size_t maxPoints) const;
    QString pointsToJsArray(const std::vector<GPSPoint>& points, size_t maxPoints) const;

private:
    QWidget *centralWidget;
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
    DatabaseManager *dbManager;
    int cachedTaxiId;
    std::vector<GPSPoint> cachedTrajectory;
    std::vector<GPSPoint> cachedAllPoints;
    bool hasCachedRegionQuery;
    qint64 cachedRegionStartTime;
    qint64 cachedRegionEndTime;
    double cachedRegionMinLon;
    double cachedRegionMinLat;
    double cachedRegionMaxLon;
    double cachedRegionMaxLat;
    qint64 cachedRegionResult;
};

#endif // TRAFFICANALYSISSYSTEM_H

#include <QApplication>
#include "TrafficAnalysisSystem/TrafficAnalysisSystem.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    
    TrafficAnalysisSystem window;
    window.show();
    
    return app.exec();
}
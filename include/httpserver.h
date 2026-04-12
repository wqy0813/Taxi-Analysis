#ifndef HTTPSERVER_H
#define HTTPSERVER_H

#include <QString>

#include "appconfig.h"

class HttpServer
{
public:
    explicit HttpServer(const AppConfig& config);

    bool start(quint16 port);

private:
    QString resolveWebRoot() const;

    AppConfig m_config;
};

#endif // HTTPSERVER_H

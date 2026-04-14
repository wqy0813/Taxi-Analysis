#ifndef HTTPSERVER_H
#define HTTPSERVER_H

#include <cstdint>
#include <string>

#include "appconfig.h"

class HttpServer {
public:
    explicit HttpServer(const AppConfig& config);

    bool start(std::uint16_t port);

private:
    std::string resolveWebRoot() const;

    AppConfig m_config;
};

#endif

#ifndef SIMPLETIMER_H
#define SIMPLETIMER_H

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include "logger.h"
class SimpleTimer {
public:
    explicit SimpleTimer(std::string name = "", bool start_now = false)
        : m_name(std::move(name)), m_running(false)
    {
        if (start_now) {
            start();
        }
    }

    void start() {
        m_start = std::chrono::steady_clock::now();
        m_running = true;
        Debug() << "[Timer Start] " << m_name << std::endl;
    }

    void stop() {
        if (!m_running) {
            Debug() << "[Timer Warning] stop() called before start()" << std::endl;
            return;
        }

        const auto ms = elapsed();
        Debug() << "[Timer Stop] " << m_name << " | elapsed = " << ms << "ms" << std::endl;
        m_running = false;
    }

    void print(const std::string& tag = "") const {
        if (!m_running) {
            Debug() << "[Timer Warning] print() called before start()" << std::endl;
            return;
        }

        Debug() << "[Timer] " << m_name << " " << tag << " | elapsed = " << elapsed() << "ms" << std::endl;
    }

    std::int64_t elapsed() const {
        const auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - m_start).count();
    }

private:
    std::string m_name;
    bool m_running;
    std::chrono::steady_clock::time_point m_start{};
};

#endif

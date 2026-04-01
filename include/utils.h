#ifndef SIMPLETIMER_H
#define SIMPLETIMER_H

#include <QElapsedTimer>
#include <QDebug>
#include <QString>

class SimpleTimer {
public:
    explicit SimpleTimer(const QString& name = "",bool start=false)
        : m_name(name), m_running(false)
    {if(start) this->start();}

    void start() {
        m_timer.start();
        m_running = true;
        qDebug() << "[Timer Start]" << m_name;
    }

    void stop() {
        if (!m_running) {
            qDebug() << "[Timer Warning] stop() called before start()";
            return;
        }

        qint64 ms = m_timer.elapsed();
        qDebug() << "[Timer Stop]" << m_name
                 << "| elapsed =" << ms << "ms";

        m_running = false;
    }

    void print(const QString& tag = "") {
        if (!m_running) {
            qDebug() << "[Timer Warning] print() called before start()";
            return;
        }

        qDebug() << "[Timer]" << m_name
                 << tag
                 << "| elapsed =" << m_timer.elapsed() << "ms";
    }

    qint64 elapsed() const {
        return m_timer.elapsed();
    }

private:
    QString m_name;
    bool m_running;
    QElapsedTimer m_timer;
};

#endif
#ifndef LOGGER_H
#define LOGGER_H

#include <sstream>
#include <string>
#include <mutex>
#include <ostream>

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error
};

class LogStream {
public:
    explicit LogStream(LogLevel level);
    ~LogStream();

    LogStream& operator<<(const wchar_t* value);
    LogStream& operator<<(const std::wstring& value);

    LogStream& operator<<(const char* value);
    LogStream& operator<<(const std::string& value);

    template<typename T>
    LogStream& operator<<(const T& value) {
        stream_ << value;
        return *this;
    }

    LogStream& operator<<(std::wostream& (*manip)(std::wostream&)) {
        manip(stream_);
        return *this;
    }

    LogStream& operator<<(std::wios& (*manip)(std::wios&)) {
        manip(stream_);
        return *this;
    }

    LogStream& operator<<(std::ios_base& (*manip)(std::ios_base&)) {
        manip(stream_);
        return *this;
    }

private:
    LogLevel level_;
    std::wostringstream stream_;

    static std::mutex mutex_;

    static std::wstring currentTimeString();
    static const wchar_t* levelToString(LogLevel level);
    static void writeLine(LogLevel level, const std::wstring& line);
    static std::wstring utf8ToWide(const std::string& str);
};

inline LogStream Debug()   { return LogStream(LogLevel::Debug); }
inline LogStream Info()    { return LogStream(LogLevel::Info); }
inline LogStream Warning() { return LogStream(LogLevel::Warning); }
inline LogStream Error()   { return LogStream(LogLevel::Error); }

#endif
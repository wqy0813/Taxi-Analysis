#include "logger.h"

#include <chrono>
#include <ctime>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#endif

std::mutex LogStream::mutex_;

LogStream::LogStream(LogLevel level)
    : level_(level) {}

LogStream::~LogStream() {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::wstring line =
        L"[" + currentTimeString() + L"] [" + levelToString(level_) + L"] " + stream_.str();
    writeLine(level_, line);
}

LogStream& LogStream::operator<<(const wchar_t* value) {
    if (value) {
        stream_ << value;
    }
    return *this;
}

LogStream& LogStream::operator<<(const std::wstring& value) {
    stream_ << value;
    return *this;
}

LogStream& LogStream::operator<<(const char* value) {
    if (value) {
        stream_ << utf8ToWide(std::string(value));
    }
    return *this;
}

LogStream& LogStream::operator<<(const std::string& value) {
    stream_ << utf8ToWide(value);
    return *this;
}

std::wstring LogStream::currentTimeString() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);

    std::tm localTime{};
#ifdef _WIN32
    localtime_s(&localTime, &time);
#else
    localtime_r(&time, &localTime);
#endif

    wchar_t buf[32];
    wcsftime(buf, sizeof(buf) / sizeof(wchar_t), L"%Y-%m-%d %H:%M:%S", &localTime);
    return std::wstring(buf);
}

const wchar_t* LogStream::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:   return L"DEBUG";
        case LogLevel::Info:    return L"INFO";
        case LogLevel::Warning: return L"WARNING";
        case LogLevel::Error:   return L"ERROR";
        default:                return L"UNKNOWN";
    }
}

std::wstring LogStream::utf8ToWide(const std::string& str) {
    if (str.empty()) {
        return L"";
    }

#ifdef _WIN32
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    if (size <= 0) {
        return L"[Invalid UTF-8]";
    }

    std::wstring result(size - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &result[0], size);
    return result;
#else
    return std::wstring(str.begin(), str.end());
#endif
}

void LogStream::writeLine(LogLevel level, const std::wstring& line) {
#ifdef _WIN32
    HANDLE handle = (level == LogLevel::Error)
        ? GetStdHandle(STD_ERROR_HANDLE)
        : GetStdHandle(STD_OUTPUT_HANDLE);

    DWORD mode = 0;
    if (GetConsoleMode(handle, &mode)) {
        std::wstring content = line + L"\n";
        DWORD written = 0;
        WriteConsoleW(handle, content.c_str(),
                      static_cast<DWORD>(content.size()),
                      &written, nullptr);
        return;
    }
#endif

    if (level == LogLevel::Error) {
        std::wcerr << line << std::endl;
    } else {
        std::wcout << line << std::endl;
    }
}
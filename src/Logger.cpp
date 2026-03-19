#include "virtualtorrentfs/Logger.hpp"

#include <iostream>

namespace vtfs {

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::log(LogLevel level, const std::wstring& message) {
    std::scoped_lock lock(mutex_);
    const wchar_t* prefix = L"[INFO]";
    switch (level) {
    case LogLevel::Warning:
        prefix = L"[WARN]";
        break;
    case LogLevel::Error:
        prefix = L"[ERROR]";
        break;
    case LogLevel::Debug:
        prefix = L"[DEBUG]";
        break;
    case LogLevel::Info:
        break;
    }

    std::wcout << prefix << L' ' << message << std::endl;
}

} // namespace vtfs

#pragma once

#include <mutex>
#include <string>

namespace vtfs {

enum class LogLevel { Info, Warning, Error, Debug };

class Logger {
public:
    static Logger& instance();
    void log(LogLevel level, const std::wstring& message);

private:
    std::mutex mutex_;
};

} // namespace vtfs

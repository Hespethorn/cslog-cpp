#ifndef CSLOG_H
#define CSLOG_H

#include <string>
#include <sstream>
#include <fstream>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <queue>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <vector>
#include <yaml-cpp/yaml.h>
#include "version.h"

#define CSLOG_CONFIG_PATH "../config/config.yaml"

namespace csLog {

enum LogLevel {
    LOG_LEVEL_OFF   = -1,
    LOG_LEVEL_ERROR = 0,
    LOG_LEVEL_WARN,
    LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG
};

inline const char* levelName(LogLevel lvl) {
    switch (lvl) {
        case LOG_LEVEL_ERROR: return "ERROR";
        case LOG_LEVEL_WARN:  return "WARN";
        case LOG_LEVEL_INFO:  return "INFO";
        case LOG_LEVEL_DEBUG: return "DEBUG";
        default:              return "OFF";
    }
}

inline const char* levelColor(LogLevel lvl) {
    switch (lvl) {
        case LOG_LEVEL_ERROR: return "\033[31m";
        case LOG_LEVEL_WARN:  return "\033[33m";
        case LOG_LEVEL_INFO:  return "\033[36m";
        case LOG_LEVEL_DEBUG: return "\033[37m";
        default:              return "";
    }
}

static constexpr const char* COLOR_RESET = "\033[0m";

struct LogConfig {
    bool enable     = true;
    bool toConsole  = true;
    bool toFile     = true;

    LogLevel level  = LOG_LEVEL_DEBUG;

    std::string baseName = "server";
    std::string logPath  = "./logs/";

    int    maxFileCount      = 5;
    size_t maxFileSize       = 5 * 1024 * 1024;
    size_t maxLogsTotalSize  = 50 * 1024 * 1024;

    size_t      maxQueueSize = 20000;
    std::string queuePolicy  = "block";
};

LogConfig& config();

struct LogTask {
    LogLevel    lvl;
    std::string text;
};

class Logger {
public:
    static Logger& instance();
    void push(LogLevel lvl, const std::string& msg);
    void stop();

private:
    Logger();
    ~Logger();

    void loadConfigFromFile();

    std::mutex              mtx;
    std::condition_variable cv;
    std::queue<LogTask>     queue;
    bool                    exitFlag = false;

    std::ofstream file;
    size_t        currentSize    = 0;
    std::string   currentFileName;

    std::thread   worker;

    std::vector<char> fileBuffer;
    size_t            bytesSinceFlush = 0;

    void workerThread();
    void rotate();
    void openFileOnce();

    void cleanupOldLogFiles();
    void createNewLogFile();
};

class LogLine {
public:
    LogLine(LogLevel lvl, const char* file, int line, const char* func)
        : level(lvl), fileName(file), lineNum(line), funcName(func) {}

    LogLine(LogLevel lvl) : level(lvl) {}

    ~LogLine();

    std::ostringstream& stream() { return ss; }

private:
    LogLevel    level;
    const char* fileName = nullptr;
    int         lineNum  = 0;
    const char* funcName = nullptr;

    std::ostringstream ss;
};

} // namespace csLog

#define LOG_ERROR   csLog::LogLine(csLog::LOG_LEVEL_ERROR).stream()
#define LOG_WARN    csLog::LogLine(csLog::LOG_LEVEL_WARN ).stream()
#define LOG_INFO    csLog::LogLine(csLog::LOG_LEVEL_INFO ).stream()
#define LOG_DEBUG   csLog::LogLine(csLog::LOG_LEVEL_DEBUG).stream()

#define LOG_ERROR_F csLog::LogLine(csLog::LOG_LEVEL_ERROR, __FILE__, __LINE__, __FUNCTION__).stream()
#define LOG_WARN_F  csLog::LogLine(csLog::LOG_LEVEL_WARN,  __FILE__, __LINE__, __FUNCTION__).stream()
#define LOG_INFO_F  csLog::LogLine(csLog::LOG_LEVEL_INFO,  __FILE__, __LINE__, __FUNCTION__).stream()
#define LOG_DEBUG_F csLog::LogLine(csLog::LOG_LEVEL_DEBUG, __FILE__, __LINE__, __FUNCTION__).stream()

#endif // CSLOG_H

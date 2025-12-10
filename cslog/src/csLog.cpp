#include "cslog/csLog.h"
#include <cstdio>
#include <algorithm>
#include <filesystem>
#include <vector>
#include <ctime>

namespace csLog {

static LogConfig g_cfg;
LogConfig& config() { return g_cfg; }

static std::string jsonEscape(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '\"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if ((unsigned char)c < 0x20) {
                    char buf[7];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c & 0xff);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

Logger::Logger()
{
    loadConfigFromFile();
    worker = std::thread(&Logger::workerThread, this);
}

Logger::~Logger() {
    stop();
}

void Logger::loadConfigFromFile()
{
    YAML::Node root = YAML::LoadFile(CSLOG_CONFIG_PATH);

    if (auto node = root["csLog"]) {

        auto get = [&](const char* key, auto& dst) {
            if (node[key]) dst = node[key].as<std::decay_t<decltype(dst)>>();
        };

        get("enable",           config().enable);
        get("toConsole",        config().toConsole);
        get("toFile",           config().toFile);
        get("logPath",          config().logPath);
        get("fileName",         config().baseName);
        get("maxFileCount",     config().maxFileCount);
        get("maxFileSize",      config().maxFileSize);
        get("maxLogsTotalSize", config().maxLogsTotalSize);
        get("maxQueueSize",     config().maxQueueSize);
        get("queuePolicy",      config().queuePolicy);

        if (node["level"]) {
            std::string s = node["level"].as<std::string>();
            std::transform(s.begin(), s.end(), s.begin(), ::toupper);
            switch (s[0]) {
                case 'E': config().level = LOG_LEVEL_ERROR; break;
                case 'W': config().level = LOG_LEVEL_WARN;  break;
                case 'I': config().level = LOG_LEVEL_INFO;  break;
                case 'D': config().level = LOG_LEVEL_DEBUG; break;
                default:  config().level = LOG_LEVEL_INFO;  break;
            }
        }
    }
}

void Logger::cleanupOldLogFiles()
{
    namespace fs = std::filesystem;

    if (config().maxLogsTotalSize == 0) return;

    std::vector<fs::directory_entry> files;

    std::string prefix = config().baseName + "_";
    std::string suffix = ".log";

    if (fs::exists(config().logPath)) {
        for (auto& e : fs::directory_iterator(config().logPath)) {
            if (!e.is_regular_file()) continue;

            std::string name = e.path().filename().string();

            if (name.rfind(prefix, 0) == 0 &&
                name.size() > prefix.size() + suffix.size() &&
                name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
            {
                files.push_back(e);
            }
        }
    }

    if (files.empty()) return;

    std::uintmax_t totalSize = 0;
    for (auto& f : files) {
        totalSize += f.file_size();
    }

    if (totalSize <= config().maxLogsTotalSize)
        return;

    std::sort(files.begin(), files.end(),
              [](const fs::directory_entry& a,
                 const fs::directory_entry& b) {
                  return a.last_write_time() < b.last_write_time();
              });

    for (auto& f : files) {
        if (totalSize <= config().maxLogsTotalSize)
            break;

        auto p  = f.path();
        auto sz = f.file_size();

        fs::remove(p);

        if (totalSize >= sz) {
            totalSize -= sz;
        } else {
            totalSize = 0;
        }
    }
}

void Logger::createNewLogFile()
{
    auto now = std::chrono::system_clock::now();
    time_t t = std::chrono::system_clock::to_time_t(now);

    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif

    char buf[64];
    std::snprintf(buf, sizeof(buf),
        "_%04d-%02d-%02d_%02d-%02d-%02d.log",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec
    );

    currentFileName = config().logPath + config().baseName + buf;

    LOG_INFO << "日志文件：" << currentFileName;

    file.open(currentFileName, std::ios::binary | std::ios::app);
    if (!file.is_open()) {
        currentSize = 0;
        return;
    }

    if (fileBuffer.empty()) {
        fileBuffer.resize(64 * 1024);
    }
    file.rdbuf()->pubsetbuf(fileBuffer.data(), fileBuffer.size());

    file.seekp(0, std::ios::end);
    currentSize = static_cast<size_t>(file.tellp());
    bytesSinceFlush = 0;
}

void Logger::openFileOnce() {
    if (file.is_open()) return;

    std::filesystem::create_directories(config().logPath);

    cleanupOldLogFiles();

    createNewLogFile();
}

void Logger::rotate()
{
    if (currentSize < config().maxFileSize)
        return;

    if (file.is_open()) {
        file.flush();
        file.close();
    }

    cleanupOldLogFiles();

    createNewLogFile();
}

void Logger::push(LogLevel lvl, const std::string& msg) {
    if (!config().enable || lvl > config().level)
        return;

    std::unique_lock<std::mutex> lock(mtx);

    if (queue.size() >= config().maxQueueSize) {

        if (config().queuePolicy == "drop") return;

        if (config().queuePolicy == "warn") {
            std::cerr << "\033[33m[WARN] 日志队列已满，此日志被丢弃！\033[0m\n";
            return;
        }

        if (config().queuePolicy == "block") {
            cv.wait(lock, [&] { return queue.size() < config().maxQueueSize; });
        }
    }

    queue.push(LogTask{lvl, msg});
    cv.notify_one();
}

void Logger::workerThread()
{
    using namespace std::chrono;

    const size_t FLUSH_BYTES_THRESHOLD = 32 * 1024;
    const int    FLUSH_INTERVAL_MS     = 1000;

    auto lastFlush = steady_clock::now();

    while (true) {
        LogTask task;
        bool hasTask = false;

        {
            std::unique_lock<std::mutex> lock(mtx);

            cv.wait_for(lock, milliseconds(FLUSH_INTERVAL_MS), [&] {
                return exitFlag || !queue.empty();
            });

            if (exitFlag && queue.empty()) {
                break;
            }

            if (!queue.empty()) {
                task = std::move(queue.front());
                queue.pop();
                hasTask = true;
            }
        }

        if (hasTask) {
            if (config().toConsole) {
                std::cout << levelColor(task.lvl)
                          << task.text
                          << COLOR_RESET;
                std::cout.flush();
            }

            if (config().toFile) {
                openFileOnce();
                if (file.is_open()) {
                    file.write(task.text.data(), task.text.size());
                    currentSize     += task.text.size();
                    bytesSinceFlush += task.text.size();
                    rotate();

                    if (task.lvl <= LOG_LEVEL_ERROR) {
                        file.flush();
                        bytesSinceFlush = 0;
                        lastFlush = steady_clock::now();
                    }
                }
            }
        }

        if (config().toFile && file.is_open()) {
            bool needFlush = false;

            if (bytesSinceFlush >= FLUSH_BYTES_THRESHOLD) {
                needFlush = true;
            } else {
                auto now = steady_clock::now();
                if (duration_cast<milliseconds>(now - lastFlush).count() >= FLUSH_INTERVAL_MS) {
                    needFlush = true;
                }
            }

            if (needFlush) {
                file.flush();
                bytesSinceFlush = 0;
                lastFlush = steady_clock::now();
            }
        }

        {
            std::unique_lock<std::mutex> lock(mtx);
            cv.notify_all();
        }
    }

    if (config().toFile && file.is_open()) {
        file.flush();
    }
}

void Logger::stop()
{
    {
        std::lock_guard<std::mutex> lock(mtx);
        exitFlag = true;
    }
    cv.notify_all();
    if (worker.joinable()) worker.join();
    if (file.is_open()) file.close();
}

LogLine::~LogLine()
{
    if (!config().enable || level > config().level)
        return;

    std::string msg = ss.str();

    while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) {
        msg.pop_back();
    }

    auto now = std::chrono::system_clock::now();
    time_t t = std::chrono::system_clock::to_time_t(now);

    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif

    char tsBuf[32];
    std::strftime(tsBuf, sizeof(tsBuf), "%Y-%m-%d %H:%M:%S", &tm);

    std::ostringstream out;
    out << "{";
    out << "\"time\":\"" << tsBuf << "\"";
    out << ",\"level\":\"" << levelName(level) << "\"";

    if (fileName) {
        out << ",\"file\":\""  << jsonEscape(fileName) << "\"";
        out << ",\"line\":"    << lineNum;
        out << ",\"func\":\""  << jsonEscape(funcName ? funcName : "") << "\"";
    }

    out << ",\"msg\":\"" << jsonEscape(msg) << "\"";
    out << "}\n";

    Logger::instance().push(level, out.str());
}

} // namespace csLog

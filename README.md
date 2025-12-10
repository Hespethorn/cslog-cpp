# cslog

本模块提供一个 **高性能、线程安全、异步写入** 的 C++ 日志系统，实现：

* 异步写入（后台线程 + 队列）
* 单行 JSON 格式日志，便于机器解析
* 单文件大小回滚 + 日志总大小上限控制
* 智能 flush：按字节、按时间、按 ERROR 级别触发
* 彩色控制台输出（可选）

---

## ✨ 特性

* **结构化日志**：每行都是完整 JSON，方便 ELK / Loki / jq / Python 分析
* **异步 + 低锁竞争**：业务线程只负责入队，真正 I/O 在后台线程执行
* **多重限流**：

  * 单文件大小上限（`maxFileSize`）自动滚动新文件
  * 所有日志文件总大小（`maxLogsTotalSize`）自动删除最旧日志
* **智能 flush 策略**：兼顾性能和崩溃场景下的日志安全
* **可配置**：通过 `config.yaml` 控制输出等级、输出位置、队列策略等

---

## 🔧 对外主要接口（宏 + Logger 单例）

一般只需要用日志宏，不直接操作 `Logger`：

```cpp
// 常规日志（不带源信息）
LOG_ERROR   << "错误信息";
LOG_WARN    << "警告信息";
LOG_INFO    << "普通信息";
LOG_DEBUG   << "调试信息";

// 带文件名 / 行号 / 函数名
LOG_ERROR_F << "严重错误，code=" << code;
LOG_INFO_F  << "启动模块: " << moduleName;
```

`Logger` 自身是单例：

```cpp
csLog::Logger& log = csLog::Logger::instance();

// 不建议在业务逻辑中直接调用，但可以在退出时主动 stop
log.stop();
```

> 注意：
>
> * 第一次使用 `LOG_XXX` 会触发 `Logger::instance()` 构造，**自动加载 YAML 配置并启动后台线程**。
> * 无需手动初始化。

---

## 🚀 快速上手

```cpp
#include "cslog/csLog.h"

int main()
{
    LOG_INFO << "程序启动";
    LOG_DEBUG << "调试信息";
    LOG_ERROR_F << "模块初始化失败, code=" << -1;

    // 程序结束前不需要显式 flush，Logger 析构时会自动 stop + flush
    return 0;
}
```

日志文件（JSON，每行一条）示例：

```json
{"time":"2025-12-10 18:00:01","level":"INFO","msg":"程序启动"}
{"time":"2025-12-10 18:00:01","level":"ERROR","file":"main.cpp","line":12,"func":"main","msg":"模块初始化失败, code=-1"}
```

---

## ⚙ 配置文件（config.yaml）

```yaml
csLog:
  enable: true
  level: DEBUG              # ERROR / WARN / INFO / DEBUG
  toConsole: true           # 是否输出到控制台（彩色）
  toFile: true              # 是否输出到文件

  logPath: "./logs/"
  fileName: "CS-Y2526-03-client"

  maxFileSize: 5242880       # 单文件最大 5MB
  maxLogsTotalSize: 52428800 # 所有 .log 总大小 50MB，超过会删最旧

  maxQueueSize: 20000
  queuePolicy: "block"       # block / drop / warn
```

---

## 🧠 内部架构概览

### 日志数据流

1. 业务代码调用 `LOG_INFO << "xxx"`
2. 生成 `LogLine` 对象，累积内容到 `std::ostringstream`
3. `LogLine` 在析构时：

   * 获取当前时间
   * 拼出一行 JSON 字符串
   * 调用 `Logger::instance().push(level, jsonLine)` 入队
4. 后台线程 `workerThread()` 从队列中取日志：

   * 按等级着色输出到控制台（可选）
   * 追加写入当前日志文件
   * 根据策略进行 flush / 滚动 / 删除旧文件

---

## 🧵 线程模型 & 队列策略

### 线程模型

* **生产者**：业务线程（任意线程）调用 `LOG_XXX`
* **消费者**：一个独立的后台线程 `workerThread()`

优点：

* 文件 I/O 只在一个线程中进行，保证顺序、不需要文件级别锁
* 业务线程只做字符串拼接和入队，开销小

### 队列与策略

内部使用：

```cpp
std::queue<LogTask> queue;
std::mutex          mtx;
std::condition_variable cv;
```

当队列长度达到 `maxQueueSize` 时：

* `queuePolicy = "block"`

  * 生产者在 `push()` 中阻塞等待队列变小 → 不丢日志，可能卡主
* `queuePolicy = "drop"`

  * 直接丢弃本条日志 → 不阻塞，可能丢日志
* `queuePolicy = "warn"`

  * 打印黄色警告（stderr），然后丢弃本条日志

---

## 💾 文件写入 & Flush 策略

后台线程在处理每条日志时：

1. 如果 `toConsole = true` → 带颜色输出到 `std::cout`，立即 flush
2. 如果 `toFile = true`：

   * 调用 `openFileOnce()` 确保当前日志文件已打开
   * 写入这条 JSON 行
   * 增加 `currentSize` & `bytesSinceFlush`
   * 调用 `rotate()` 判断是否需要按单文件大小进行滚动

此外，线程每次循环会检查是否需要 flush：

* **立即 flush 场景**：

  * 日志等级为 `ERROR`（`LOG_ERROR / LOG_ERROR_F`）
* **批量 flush 场景**：

  * `bytesSinceFlush >= 32KB`
  * 距离上次 flush 超过 1 秒

> 这样可以在：
>
> * 高频日志场景下减少 flush 次数（提高性能）
> * 低频日志场景下保证日志尽快落盘（延迟最长约 1 秒）
> * ERROR 场景下尽可能不丢关键出错日志。

---

## 📂 文件回滚与磁盘占用控制

### 1. 单日志文件大小回滚（maxFileSize）

当当前文件大小 `currentSize >= maxFileSize` 时：

1. flush + close 当前文件
2. 触发 `cleanupOldLogFiles()`（按总大小删最旧）
3. 调用 `createNewLogFile()` 创建一个新的时间戳文件：

```text
CS-Y2526-03-client_2025-12-10_17-45-26.log
```

### 2. 所有日志文件总大小限制（maxLogsTotalSize）

`cleanupOldLogFiles()` 会：

1. 遍历 `logPath` 下所有满足 `{fileName}_*.log` 的文件
2. 统计总大小
3. 若总大小 > `maxLogsTotalSize`：

   * 按 `last_write_time` 升序排序
   * 从最旧的文件开始依次 `remove()`
   * 直到总大小 <= 上限

> 确保日志不会无限占用磁盘空间，尤其适合嵌入式和长期运行服务。

---

## 🔍 日志等级与过滤

枚举定义：

```cpp
enum LogLevel {
    LOG_LEVEL_OFF   = -1,
    LOG_LEVEL_ERROR = 0,
    LOG_LEVEL_WARN,
    LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG
};
```

当配置为：

```yaml
level: INFO
```

则：

* ✅ 输出：ERROR / WARN / INFO
* ❌ 不输出：DEBUG

过滤逻辑在 `LogLine::~LogLine()` 和 `Logger::push()` 中统一判断：

```cpp
if (!config().enable || level > config().level)
    return;
```

---

## ✅ 小结

* **对外 API 简单**：只用 `LOG_XXX` 宏就能完成绝大多数需求
* **内部架构清晰**：

  * RAII 构造 JSON 行
  * 单例 Logger 管理线程和资源
  * 单消费线程 I/O，队列 + 条件变量做桥接
* **可靠性**：

  * ERROR 级别立刻 flush
  * 批量 flush 降低性能损耗
  * 单文件大小 + 总大小双重控制磁盘占用


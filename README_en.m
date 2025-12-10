# cslog Architecture Documentation

`cslog` is a high-performance, thread-safe, asynchronous C++ logging system designed for production environments. It provides:

* Asynchronous logging with minimal contention
* JSON per-line structured logs
* File rolling (max file size)
* Total log size enforcement (delete oldest files)
* Intelligent flush strategy (byte threshold / time threshold / ERROR immediate flush)
* Optional colored console output
* YAML-driven configuration

---

## ✨ Features

* **Clean API** — logging is done via simple macros (`LOG_INFO`, `LOG_ERROR_F`, etc.)
* **Zero-overhead for user code** — main thread only builds JSON & enqueues log tasks
* **Single background worker thread** handles all file I/O
* **Machine-friendly format** — every log entry is valid JSON
* **Disk usage control** — file size limit + total size limit
* **Crash-resilient** — ERROR logs flush immediately

---

## 🔧 Public API Overview

### Log Macros (recommended)

```cpp
LOG_ERROR   << "error";
LOG_WARN    << "warning";
LOG_INFO    << "info";
LOG_DEBUG   << "debug";

// include file / line / function info:
LOG_ERROR_F << "fatal error";
LOG_INFO_F  << "initialized";
```

### Logger singleton (rarely needed directly)

```cpp
auto& log = csLog::Logger::instance();
log.stop();   // optional, called automatically on program exit
```

> There is **no explicit initialization step**.
> Logging macros will automatically trigger `Logger::instance()`, load YAML config, and start the worker thread.

---

# 🚀 Quick Start

```cpp
#include "cslog/csLog.h"

int main() {
    LOG_INFO << "System started";
    LOG_DEBUG << "Debug info";
    LOG_ERROR_F << "Critical failure";

    return 0; // Logger flushes and stops automatically
}
```

Example JSON output:

```json
{"time":"2025-12-10 18:00:01","level":"INFO","msg":"System started"}
{"time":"2025-12-10 18:00:01","level":"ERROR","file":"main.cpp","line":12,"func":"main","msg":"Critical failure"}
```

---

# ⚙ YAML Configuration

```yaml
csLog:
  enable: true
  level: DEBUG

  toConsole: true
  toFile: true

  logPath: "./logs/"
  fileName: "cslog-demo"

  maxFileSize: 5242880       # 5MB per file
  maxLogsTotalSize: 52428800 # total logs capped at 50MB

  maxQueueSize: 20000
  queuePolicy: "block"       # block / drop / warn
```

---

# 🧠 Internal Architecture Overview

cslog's architecture is intentionally simple and robust:

```
User Code → LogLine (RAII) → Logger::push() → Worker Thread → File Output
```

---

# 1. Log Data Flow

## (A) Producer Path (user thread)

1. User writes:

   ```cpp
   LOG_INFO << "message";
   ```
2. `LogLine` object collects message text.
3. On destruction, `LogLine` builds a **complete JSON string**.
4. JSON is passed to:

   ```cpp
   Logger::instance().push(level, jsonLine);
   ```
5. The log entry is pushed into a thread-safe queue.

**No disk I/O happens on the user thread.**
This keeps logging overhead extremely low.

---

## (B) Consumer Path (background worker thread)

* Waits for log tasks via condition variable
* Pops tasks one by one
* Colored console output (optional)
* Appends JSON line to log file
* Flushes based on:

  * ERROR level
  * Byte threshold (32 KB)
  * Time threshold (1 second)
* Triggers file rolling when file exceeds `maxFileSize`
* Triggers old file deletion when total size exceeds `maxLogsTotalSize`

Only the worker thread touches the filesystem — eliminating race conditions.

---

# 2. Core Components

## LogLine

* RAII object
* Builds JSON string in its destructor
* Automatically injects:

  * timestamp
  * log level
  * file / line / function (for `_F` macros)
* Sends the completed JSON to `Logger::push`

Simple and fast; only string operations, no I/O.

---

## Logger (Singleton)

Responsible for:

* Reading YAML configuration
* Starting & stopping the worker thread
* Managing the task queue
* Managing the current log file
* Implementing flush logic
* File rolling logic
* Total log size enforcement

Internally split into:

```
loadConfigFromFile()
push()
workerThread()
openFileOnce()
rotate()
createNewLogFile()
cleanupOldLogFiles()
```

---

# 3. Thread Model

cslog uses a classic **multi-producer / single-consumer** pattern:

* User threads → produce log messages
* Worker thread → consumes & writes to disk

Advantages:

* Log file writes are serialized → no locking for file operations
* Minimal contention → producer only locks queue briefly
* Predictable behavior → logs always in correct order

---

# 4. Queue + Backpressure Strategy

Queue structure:

```cpp
std::queue<LogTask> queue;
std::mutex          mtx;
std::condition_variable cv;
```

If queue is full:

| queuePolicy | Behavior                          |
| ----------- | --------------------------------- |
| `block`     | Producer waits (no logs lost)     |
| `drop`      | Discard current log (low latency) |
| `warn`      | Warn on console + discard         |

Choose according to system needs.

---

# 5. File Writing & Flush Logic

## Immediate flush on ERROR

```cpp
if (task.lvl <= LOG_LEVEL_ERROR)
    file.flush();
```

Ensures critical errors are not lost.

## Byte-based flush

Flush when:

```
bytesSinceFlush >= 32 KB
```

## Time-based flush

Flush if:

```
> 1 second since last flush
```

This guarantees:

* high throughput (batched writes)
* low latency (≤ 1 second)
* maximum reliability for error conditions

---

# 6. File Rolling Mechanism

When the current file exceeds:

```
maxFileSize
```

the logger:

1. Flushes & closes the file
2. Calls `cleanupOldLogFiles()`
3. Creates a new file:

```
cslog-demo_2025-12-10_17-45-26.log
```

Files never grow beyond the configured size limit.

---

# 7. Total Log Size Enforcement

When the sum of `{fileName}_*.log` exceeds:

```
maxLogsTotalSize
```

cslog:

1. Sorts files by last modification time
2. Deletes the **oldest** files first
3. Continues until total size ≤ limit

This ensures your log directory never overruns disk space — crucial on embedded devices or long-running servers.

---

# 8. Error Handling & Safety

* YAML load errors → propagate exception
* File open failure → logger continues but skips file output
* Queue overflow → depends on `queuePolicy`
* Error logs → immediate flush
* Program shutdown → worker thread forced flush + safe exit

cslog never crashes the application due to logging failure.

---

# 9. Extensibility Points

cslog is easy to extend:

* Add daily rolling (by date)
* Add multiple log files (info.log / error.log)
* Add compression (zstd/lz4) for old logs
* Add remote logging (TCP/UDP/syslog/journald)
* Add multiple logger instances
* Make JSON schema customizable

Because the worker thread is the only I/O path and is well-isolated, modifying behavior is straightforward.

---

# ✅ Summary

cslog offers a balanced combination of:

* **High performance**
* **Thread safety**
* **Structured JSON logging**
* **Robust file management**
* **Ease of configuration**
* **Crash resilience**

Its architecture is clean and purpose-built for embedded systems, servers, daemons, and any application requiring stable long-term logging.

---

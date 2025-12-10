# 🎉 cslog —— 高性能 C++ JSON 日志库

**cslog** 是一个轻量级、高性能、线程安全的 C++ 日志库，具备：

- 🚀 **异步写入（后台线程）**
- 📄 **单行 JSON 格式日志**
- 🧠 **智能 flush（性能 × 安全）**
- 📦 **单文件大小滚动**
- 🗑 **日志总大小上限自动清理**
- 🎨 **彩色控制台输出（可选）**
- ⚙ **YAML 配置驱动**
- 🔧 **构造即初始化（无启动输出）**

适用于嵌入式设备、网络服务、后台守护进程、高并发程序等需要稳定日志系统的场景。

------

# ✨ 功能特性

## 🔹 1. 异步 & 多线程安全

日志写入通过独立线程异步处理，不阻塞主线程。

支持队列策略：

- `block`   —— 队列满时阻塞
- `drop`    —— 直接丢弃
- `warn`    —— 控制台警告并丢弃

------

## 🔹 2. JSON 格式日志（1行 = 1条完整 JSON）

适用于：

- ELK / Loki / Splunk
- FluentBit / Promtail
- Python/pandas 分析
- `jq` 命令行处理

示例：

```json
{
  "time": "2025-12-10 17:45:26",
  "level": "INFO",
  "file": "main.cpp",
  "line": 12,
  "func": "initModule",
  "msg": "module initialized"
}
```

------

## 🔹 3. 智能 Flush（保证性能又避免丢日志）

为保证性能与安全性平衡，cslog 内置三层 flush 策略：

| 场景       | Flush 行为                               |
| ---------- | ---------------------------------------- |
| ERROR 级别 | **立刻 flush**（尽量避免崩溃丢关键日志） |
| 普通日志   | 每累计 `32KB` flush 一次                 |
| 周期检查   | 每 `1 秒` flush 一次                     |

程序正常退出时还会进行最后一次 flush。

------

## 🔹 4. 单文件大小滚动（maxFileSize）

当单个 `.log` 文件达到上限：

```
./logs/myApp_2025-12-10_17-00-01.log
```

自动创建新文件：

```
./logs/myApp_2025-12-10_17-05-32.log
```

------

## 🔹 5. 总大小限制（maxLogsTotalSize）

当日志目录中所有 `baseName_*.log` 文件的大小总和超过上限时：

- 自动按时间从旧到新排序
- 删除最旧文件
- 直到总大小 ≤ 上限

保证磁盘不会被耗尽。

------

## 🔹 6. 彩色控制台输出

| Level | 颜色 |
| ----- | ---- |
| ERROR | 红色 |
| WARN  | 黄色 |
| INFO  | 青色 |
| DEBUG | 灰色 |

可通过配置关闭：

```yaml
toConsole: false
```

------

# 📦 安装与集成

## 1. 使用 git submodule（推荐）

```bash
git submodule add https://github.com/yourname/cslog.git external/cslog
```

在你的项目 CMakeLists.txt 中加入：

```cmake
add_subdirectory(external/cslog)
target_link_libraries(your_app PRIVATE cslog)
```

------

## 2. 系统安装（支持 find_package）

```bash
cmake -B build
cmake --build build
sudo cmake --install build
```

安装后可直接使用：

```cmake
find_package(cslog REQUIRED)
target_link_libraries(your_app PRIVATE cslog::cslog)
```

------

# ⚙ 配置文件（YAML）

在 `config/config.yaml` 中：

```yaml
csLog:
  enable: true
  level: DEBUG              # ERROR / WARN / INFO / DEBUG
  toConsole: true
  toFile: true

  logPath: "./logs/"
  fileName: "myApp"

  maxFileSize: 5242880       # 单文件大小上限 5MB
  maxLogsTotalSize: 52428800 # 所有日志总大小上限 50MB

  maxQueueSize: 20000
  queuePolicy: "block"       # block / drop / warn
```

------

# 🧩 使用示例

## 示例 1：基础日志

```cpp
#include "cslog/csLog.h"

int main() {
    LOG_INFO << "程序启动";
    LOG_DEBUG << "调试信息";
    LOG_WARN  << "低电量警告";
    LOG_ERROR << "严重错误！";

    return 0;
}
```

## 示例 2：带文件/行号/函数

```cpp
LOG_ERROR_F << "模块初始化失败";
```

输出：

```json
{"time":"2025-12-10 17:20:30","level":"ERROR","file":"main.cpp","line":72,"func":"init","msg":"模块初始化失败"}
```

------

# 📁 目录结构

```
cslog/
│
├── CMakeLists.txt
├── LICENSE
├── README.md
├── CHANGELOG.md
├── logo.svg
│
├── include/
│   └── cslog/
│       ├── csLog.h
│       └── version.h
│
├── src/
│   └── csLog.cpp
│
├── config/
│   └── config.yaml.example
│
└── examples/
    ├── CMakeLists.txt
    └── basic/
        └── main.cpp
```

------

# 🧪 示例工程运行

```bash
cmake -B build -DCSLOG_BUILD_EXAMPLES=ON
cmake --build build
./build/examples/cslog_example_basic
```

------

# 📜 错误与崩溃安全

- `ERROR` 日志立即 flush，最大化保存错误信息
- 普通日志有缓冲，但最大损失量通常 ≤ 32KB 或 ≤ 1s
- 正常退出一定完整落盘

------

# 📄 许可证

本项目使用 **MIT License**，完全可免费商用。

------

# 🤝 贡献

欢迎提交 Issue 或 PR，如果你有更多想法：

- 按天日志滚动
- 模块化 logger
- 压缩日志（zstd/lz4）
- 输出到 syslog/journald
- 网络日志（TCP/UDP）

都可以一起讨论与扩展！


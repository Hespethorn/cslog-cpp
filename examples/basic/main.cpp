#include "cslog/csLog.h"
#include <thread>
#include <chrono>

int main() {
    LOG_INFO << "cslog example started";
    LOG_DEBUG << "This is a debug message";
    LOG_WARN  << "This is a warning";
    LOG_ERROR_F << "This is an error with file/line/func info";

    for (int i = 0; i < 10; ++i) {
        LOG_INFO_F << "loop index = " << i;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    LOG_INFO << "cslog example finished";
    return 0;
}

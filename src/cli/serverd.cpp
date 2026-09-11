// platformd：无 GUI 的后台守护进程，只跑平台核心 + HTTP API。
// 用于无 Qt 环境的部署或测试；工作台 GUI 内置同款服务器。
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include "core/platform.h"
#include "core/util.h"

int main() {
    ah::Platform platform(ah::defaultHomeDir());
    std::string err;
    if (!platform.bootstrap(err)) {
        std::cerr << "[platformd] bootstrap failed: " << err << "\n";
        return 1;
    }
    int port = 8787;
    {
        std::string portEnv = ah::envOr("AGENTHIVE_PORT", "ZCODE_PLATFORM_PORT");
        if (!portEnv.empty()) port = std::atoi(portEnv.c_str());
    }
    if (!platform.startHttpServer(port, err)) {
        std::cerr << "[platformd] cannot start server: " << err << "\n";
        return 1;
    }
    std::cout << "[platformd] listening on http://127.0.0.1:" << platform.httpPort() << "\n";
    std::cout << "[platformd] data dir: " << platform.homeDir() << "\n";
    std::cout << "[platformd] press Ctrl+C to stop\n";
    std::cout.flush();

    std::signal(SIGINT, [](int) {});
    std::signal(SIGTERM, [](int) {});

    // 等待中断信号
    while (true) {
        std::string line;
        if (!std::getline(std::cin, line)) {
            // stdin 关闭（服务方式运行）→ 睡眠等待信号
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (!platform.httpRunning()) break;
        } else if (line == "quit" || line == "exit") {
            break;
        }
    }
    platform.stopHttpServer();
    std::cout << "[platformd] stopped\n";
    return 0;
}

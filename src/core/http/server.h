#pragma once
// HTTP API 服务器：仅绑定 127.0.0.1，本机 Agent 通过 HTTP + JSON 接入。
// 接口完整文档见 docs/api.md。
#include <atomic>
#include <memory>
#include <string>

namespace zp {

class Platform;

class HttpServer {
public:
    explicit HttpServer(Platform& platform);
    ~HttpServer();
    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    bool start(int port, std::string& err);
    void stop();
    bool running() const { return running_; }
    int port() const { return port_; }

private:
    struct Impl;
    void setupRoutes();

    Platform& platform_;
    std::unique_ptr<Impl> impl_;
    std::atomic<bool> running_{false};
    int port_ = 0;
};

}  // namespace zp

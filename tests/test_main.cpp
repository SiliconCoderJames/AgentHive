// 核心层单元测试（无外部测试框架，断言失败即退出码非 0）。
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <string>
#include <thread>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "core/embed/embedder.h"
#include "core/http/url_guard.h"
#include "core/platform.h"
#include "core/util.h"

namespace fs = std::filesystem;
using nlohmann::json;

static int g_checks = 0;
static int g_failures = 0;

static std::string toText(const std::string& s) { return "\"" + s + "\""; }
template <class T>
static std::string toText(const T& v) {
    std::ostringstream os;
    os << v;
    return os.str();
}

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_failures;                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
        }                                                                    \
    } while (0)

#define CHECK_EQ(a, b)                                                       \
    do {                                                                     \
        ++g_checks;                                                          \
        auto va = (a);                                                       \
        auto vb = (b);                                                       \
        if (!(va == vb)) {                                                   \
            ++g_failures;                                                    \
            std::printf("FAIL %s:%d: %s == %s (%s vs %s)\n", __FILE__,       \
                        __LINE__, #a, #b, toText(va).c_str(),                \
                        toText(vb).c_str());                                 \
        }                                                                    \
    } while (0)

static void test_sha256() {
    CHECK_EQ(zp::sha256Hex("abc"),
             "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK_EQ(zp::sha256Hex(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

static void test_week_start() {
    std::string ws = zp::weekStartIso();  // YYYY-MM-DD
    std::tm tm{};
    std::istringstream is(ws);
    is >> std::get_time(&tm, "%Y-%m-%d");
    CHECK(!is.fail());
    // get_time 不填 tm_wday，需经 time_t 往返换算星期
#ifdef _WIN32
    std::time_t t = _mkgmtime(&tm);
#else
    std::time_t t = timegm(&tm);
#endif
    std::tm lt{};
#ifdef _WIN32
    gmtime_s(&lt, &t);
#else
    gmtime_r(&t, &lt);
#endif
    CHECK_EQ(lt.tm_wday, 1);  // 周一
}

static void test_embedder() {
    zp::NgramHashEmbedder emb(384);
    auto a = emb.embed("CMake 项目使用 sqlite-vec 做向量检索");
    auto b = emb.embed("CMake 项目使用 sqlite-vec 做向量检索");
    CHECK_EQ(a.size(), 384u);
    CHECK(a == b);  // 确定性
    // L2 归一化
    double norm = 0;
    for (float v : a) norm += static_cast<double>(v) * v;
    CHECK(std::fabs(std::sqrt(norm) - 1.0) < 1e-5);
    // 不同文本向量不同
    auto c = emb.embed("今天晚饭吃什么，去楼下吃面吧");
    CHECK(a != c);
    // 相似文本的余弦相似度高于无关文本
    auto d = emb.embed("sqlite-vec 向量检索在 CMake 项目中的用法");
    auto dot = [](const std::vector<float>& x, const std::vector<float>& y) {
        double s = 0;
        for (size_t i = 0; i < x.size(); ++i) s += static_cast<double>(x[i]) * y[i];
        return s;
    };
    CHECK(dot(a, d) > dot(a, c));
}

static void test_url_guard() {
    using zp::net::isSafeOutboundUrl;
    CHECK(!isSafeOutboundUrl("http://127.0.0.1:8080/x"));
    CHECK(!isSafeOutboundUrl("http://localhost/x"));
    CHECK(!isSafeOutboundUrl("http://foo.localhost/x"));
    CHECK(!isSafeOutboundUrl("http://10.0.0.1/"));
    CHECK(!isSafeOutboundUrl("http://172.16.5.5/"));
    CHECK(!isSafeOutboundUrl("http://192.168.1.1/"));
    CHECK(!isSafeOutboundUrl("http://169.254.1.1/"));
    CHECK(!isSafeOutboundUrl("http://0.0.0.0/"));
    CHECK(!isSafeOutboundUrl("http://[::1]/"));
    CHECK(!isSafeOutboundUrl("http://[fd00::1]/"));
    CHECK(!isSafeOutboundUrl("file:///etc/passwd"));
    CHECK(!isSafeOutboundUrl("ftp://example.com/x"));
    CHECK(!isSafeOutboundUrl("http://user@example.com/"));
    CHECK(isSafeOutboundUrl("https://example.com/page"));
    CHECK(isSafeOutboundUrl("http://93.184.216.34:8080/x"));
}

static std::string readFile(const std::string& path) {
    std::ifstream in(path);
    std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    // 去掉文件末尾换行，与 bootstrap 的 getline 读取一致
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
}

static void step(const char* s) {
    std::printf("  . %s\n", s);
    std::fflush(stdout);
}

static void test_platform_end_to_end() {
    fs::path tmp = fs::temp_directory_path() / ("zcode_platform_test_" + zp::randomHex(8));
    fs::create_directories(tmp);

    {
        step("bootstrap");
        zp::Platform p(tmp.string());
        std::string err;
        CHECK(p.bootstrap(err));

        // 主密钥来自运行期生成的配置文件
        std::string masterKey = readFile((tmp / "config" / "master.key").string());
        CHECK(!masterKey.empty());
        CHECK(p.authenticateMaster(masterKey));

        // 注册 Agent
        step("register");
        std::string key, err2;
        CHECK(p.registerAgent(masterKey, "hermes", "member", key, err2));
        CHECK(!key.empty());
        CHECK(p.authenticate("hermes", key));
        CHECK(!p.authenticate("hermes", "wrong-key"));
        CHECK(!p.authenticateMaster("wrong-master"));
        CHECK(!p.isManager("hermes"));
        CHECK(p.isManager("zcode"));

        // 心跳与在线状态
        step("heartbeat");
        p.heartbeat("hermes", "写单元测试");
        std::vector<zp::AgentInfo> agents;
        CHECK(p.listAgents(agents, err));
        bool hermesOnline = false;
        for (const auto& a : agents)
            if (a.name == "hermes" && a.status == "online" && a.current_task == "写单元测试")
                hermesOnline = true;
        CHECK(hermesOnline);

        // 知识库：创建 + 关键词/语义搜索 + 版本只追加
        step("knowledge.create");
        zp::KnowledgeEntry e1;
        CHECK(p.knowledgeCreate("hermes", "sqlite-vec 接入指南",
                                "把 sqlite-vec 静态编译进进程，用 vec0 虚拟表做向量检索，配合 CMake FetchContent。",
                                {"cmake", "向量"}, "技术文档", {}, "", e1, err));
        zp::KnowledgeEntry e2;
        CHECK(p.knowledgeCreate("claude", "Qt 布局技巧",
                                "QSplitter 加 QTableWidget 做左右分栏，定时器刷新面板。",
                                {"qt"}, "技术文档", {}, "", e2, err));

        std::vector<zp::KnowledgeHit> hits;
        step("knowledge.search.semantic");
        CHECK(p.knowledgeSearch("向量检索", zp::SearchMode::Semantic, 10, "", hits, err));
        CHECK(!hits.empty());
        CHECK_EQ(hits[0].entry.title, "sqlite-vec 接入指南");

        std::vector<zp::KnowledgeHit> kws;
        CHECK(p.knowledgeSearch("Qt", zp::SearchMode::Keyword, 10, "", kws, err));
        CHECK_EQ(kws.size(), 1u);

        // 追加版本：旧版本保留
        zp::KnowledgeEntry e3;
        CHECK(p.knowledgeAddVersion("hermes", e1.uuid, "", "第二版内容：补充 Windows 下的编译选项说明。",
                                    {}, "", e3, err));
        CHECK_EQ(e3.version, 2);
        std::vector<zp::KnowledgeEntry> versions;
        CHECK(p.knowledgeVersions(e1.uuid, versions, err));
        CHECK_EQ(versions.size(), 2u);
        CHECK_EQ(versions[1].version, 1);  // v1 内容原样
        CHECK(versions[1].content.find("sqlite-vec 静态编译") != std::string::npos);

        // 技能：先注册后调用
        step("skills");
        std::string err3;
        CHECK(!p.skillInvoke("hermes", "ghost-skill", "{}", "", "success", 1, 0, 0, err3));
        zp::SkillInfo sk;
        CHECK(p.skillRegister("hermes", "code-review", "代码审查", "审查代码并给出意见",
                              "开发", "{\"type\":\"object\"}", sk, err));
        CHECK(p.skillInvoke("claude", "code-review", "{\"file\":\"a.cpp\"}", "通过", "success",
                            120, 3000, 2000, err));

        // 记忆：两次 set 生成两个版本
        step("memory");
        zp::MemoryEntry m1, m2;
        CHECK(p.memorySet("hermes", "project", "current", "正在开发多 Agent 平台", m1, err));
        CHECK_EQ(m1.version, 1);
        CHECK(p.memorySet("claude", "project", "current", "正在开发多 Agent 平台（Qt 工作台）", m2, err));
        CHECK_EQ(m2.version, 2);
        std::vector<zp::MemoryEntry> hist;
        CHECK(p.memoryHistory("project", "current", hist, err));
        CHECK_EQ(hist.size(), 2u);
        std::vector<zp::MemoryEntry> allMem;
        CHECK(p.memoryList("", allMem, err));
        CHECK(!allMem.empty());

        // 消息：发送 + 状态流转
        step("messages");
        zp::Message msg;
        CHECK(p.messageSend("task", "claude", "hermes", "修个 bug", "知识搜索返回空，请排查", msg, err));
        CHECK_EQ(msg.status, "pending");
        CHECK(p.messageSetStatus("hermes", msg.uuid, "accepted", msg, err));
        CHECK_EQ(msg.status, "accepted");
        CHECK(p.messageSetStatus("hermes", msg.uuid, "done", msg, err));
        CHECK_EQ(msg.status, "done");
        // 非法流转被拒绝
        std::string err4;
        CHECK(!p.messageSetStatus("hermes", msg.uuid, "accepted", msg, err4));

        // 错误：上报 + 解决（说明追加）
        step("errors");
        zp::ErrorReport er;
        CHECK(p.errorReport("hermes", "error", "search", "向量表未创建", "distance 查询报错", "", er, err));
        CHECK(p.errorResolve("hermes", er.uuid, "重建 vec 表后恢复", er, err));
        CHECK_EQ(er.status, "resolved");
        CHECK(er.resolution_notes.find("重建") != std::string::npos);
        // 非上报者不能解决
        zp::ErrorReport er2;
        CHECK(p.errorReport("claude", "warning", "gui", "布局警告", "占位", "", er2, err));
        std::string err5;
        CHECK(!p.errorResolve("hermes", er2.uuid, "越权", er2, err5));
        CHECK(p.errorResolve("zcode", er2.uuid, "管理者代解决", er2, err5));

        // Token 预算：80% 告警、95% 预警、超限
        // （此前技能调用已消耗 5000 Token；预算以 10 万为基准分段验证）
        step("usage");
        CHECK(p.usageSetBudget("user", 100000, err));
        zp::UsageSummary sum;
        CHECK(p.usageSummary(sum, err));
        CHECK_EQ(sum.total_tokens, static_cast<int64_t>(5000));
        CHECK_EQ(sum.alert_level, "none");
        CHECK(p.usageReport("hermes", 500, 400, "skill", "x", err));  // 50.9%
        CHECK(p.usageSummary(sum, err));
        CHECK_EQ(sum.alert_level, "none");
        CHECK(p.usageReport("hermes", 75000, 0, "skill", "x", err));  // 80.9%
        CHECK(p.usageSummary(sum, err));
        CHECK_EQ(sum.alert_level, "warn");
        CHECK(p.usageReport("hermes", 15000, 0, "skill", "x", err));  // 95.9%
        CHECK(p.usageSummary(sum, err));
        CHECK_EQ(sum.alert_level, "critical");
        CHECK(p.usageReport("hermes", 10000, 0, "skill", "x", err));  // 105.9%
        CHECK(p.usageSummary(sum, err));
        CHECK_EQ(sum.alert_level, "over");

        // 审计留痕
        step("audit");
        std::vector<zp::AuditRecord> audit;
        CHECK(p.auditList("", "", "", 500, audit, err));
        CHECK(!audit.empty());
        bool foundReg = false;
        for (const auto& r : audit)
            if (r.action == "agent.register") foundReg = true;
        CHECK(foundReg);

        // HTTP API 冒烟：启动服务，Agent 客户端访问
        step("http.start");
        int port = 0;
        for (int cand = 17890; cand < 17990 && port == 0; ++cand) {
            if (p.startHttpServer(cand, err)) port = p.httpPort();
        }
        CHECK(port > 0);
        step("http.client");
        // 注意：httplib 0.16.x 的 Client("host", port) 不剥 scheme，必须用单串构造
        httplib::Client cli("http://127.0.0.1:" + std::to_string(port));
        auto res = cli.Get("/api/agents", {{"X-Agent-Name", "hermes"}, {"X-Api-Key", key}});
        if (!res) std::printf("  http err code=%d\n", static_cast<int>(res.error()));
        CHECK(res && res->status == 200);
        if (res) {
            auto body = json::parse(res->body);
            CHECK_EQ(body["code"], 0);
            CHECK(body["data"].is_array());
        }
        auto res2 = cli.Get("/api/memory?section=project",
                            {{"X-Agent-Name", "hermes"}, {"X-Api-Key", key}});
        CHECK(res2 && res2->status == 200);
        auto bad = cli.Get("/api/agents", {{"X-Agent-Name", "hermes"}, {"X-Api-Key", "nope"}});
        if (!bad)
            std::printf("  bad-key err code=%d\n", static_cast<int>(bad.error()));
        else if (bad->status != 401)
            std::printf("  bad-key status=%d body=%s\n", bad->status, bad->body.c_str());
        CHECK(bad && bad->status == 401);

        p.shutdown();
    }
    std::error_code ec;
    fs::remove_all(tmp, ec);
}

int main() {
    auto run = [](const char* name, void (*fn)()) {
        std::printf("== %s\n", name);
        std::fflush(stdout);
        fn();
    };
    run("sha256", test_sha256);
    run("week_start", test_week_start);
    run("embedder", test_embedder);
    run("url_guard", test_url_guard);
    run("platform_e2e", test_platform_end_to_end);

    std::printf("checks: %d, failures: %d\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

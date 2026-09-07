#include "core/http/server.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <thread>

#include "core/platform.h"
#include "core/util.h"

namespace zp {

using nlohmann::json;

namespace {

json ok(const json& data) { return json{{"code", 0}, {"message", "ok"}, {"data", data}}; }
json ok() { return json{{"code", 0}, {"message", "ok"}, {"data", nullptr}}; }
json fail(int code, const std::string& msg) {
    return json{{"code", code}, {"message", msg}, {"data", nullptr}};
}

void send(httplib::Response& res, const json& body) {
    // 统一约定：顶层 code != 0 时同步设置 HTTP 状态码（401/403/404/400/500）
    if (body.contains("code") && body["code"].is_number_integer() && body["code"].get<int>() != 0)
        res.status = body["code"].get<int>();
    else
        res.status = 200;
    res.set_content(body.dump(), "application/json; charset=utf-8");
}

// 从 body 提取 embedding（可选）；维度不匹配返回 false
bool extractEmbedding(const json& body, const Platform& platform, bool& hasEmbedding,
                      std::vector<float>& vec, std::string& err) {
    hasEmbedding = false;
    if (!body.contains("embedding")) return true;
    const auto& emb = body["embedding"];
    if (!emb.is_array()) { err = "embedding must be an array of floats"; return false; }
    if (static_cast<int>(emb.size()) != platform.embeddingDim()) {
        err = "embedding dimension mismatch, expected " + std::to_string(platform.embeddingDim());
        return false;
    }
    vec.clear();
    for (const auto& v : emb) {
        if (!v.is_number()) { err = "embedding must contain numbers"; return false; }
        vec.push_back(v.get<float>());
    }
    hasEmbedding = true;
    return true;
}

json knowledgeToJson(const KnowledgeEntry& e, double score = 0.0) {
    json j = {{"uuid", e.uuid},
              {"title", e.title},
              {"content", e.content},
              {"tags", json(e.tags)},
              {"category", e.category},
              {"author", e.author},
              {"version", e.version},
              {"embedding_provider", e.embedding_provider},
              {"created_at", e.created_at}};
    if (score != 0.0) j["score"] = score;
    return j;
}

// 广播消息的 recipient 序列化为 null（语义：全体可见），点对点为字符串
json recipientJson(const std::string& recipient) {
    return recipient.empty() ? json(nullptr) : json(recipient);
}

}  // namespace

struct HttpServer::Impl {
    httplib::Server srv;
    std::thread thread;
};

HttpServer::HttpServer(Platform& platform) : platform_(platform), impl_(std::make_unique<Impl>()) {}

HttpServer::~HttpServer() { stop(); }

bool HttpServer::start(int port, std::string& err) {
    if (running_) return true;
    setupRoutes();
    if (!impl_->srv.bind_to_port("127.0.0.1", port, 0)) {
        err = "failed to bind 127.0.0.1:" + std::to_string(port);
        return false;
    }
    // bind_to_port 返回 bool 而非端口；port_ 记录请求端口（GUI/CLI 均传显式端口）
    port_ = port;
    running_ = true;
    impl_->thread = std::thread([this] { impl_->srv.listen_after_bind(); });
    // 等待监听就绪，避免客户端在 listen 前连接被拒
    for (int i = 0; i < 100 && !impl_->srv.is_running(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return true;
}

void HttpServer::stop() {
    if (!running_) return;
    running_ = false;
    impl_->srv.stop();
    if (impl_->thread.joinable()) impl_->thread.join();
}

// 从请求头取出 Agent 身份并校验
static bool checkAgent(const httplib::Request& req, Platform& platform, std::string& actor,
                       httplib::Response& res) {
    if (!req.has_header("X-Agent-Name") || !req.has_header("X-Api-Key")) {
        send(res, fail(401, "missing X-Agent-Name / X-Api-Key headers"));
        return false;
    }
    actor = req.get_header_value("X-Agent-Name");
    std::string key = req.get_header_value("X-Api-Key");
    if (!platform.authenticate(actor, key)) {
        send(res, fail(401, "invalid agent credentials"));
        return false;
    }
    return true;
}

static bool checkMaster(const httplib::Request& req, Platform& platform, httplib::Response& res) {
    if (!req.has_header("X-Master-Key") ||
        !platform.authenticateMaster(req.get_header_value("X-Master-Key"))) {
        send(res, fail(403, "invalid master key"));
        return false;
    }
    return true;
}

void HttpServer::setupRoutes() {
    auto& srv = impl_->srv;
    Platform& p = platform_;

    srv.Get("/api/health", [&](const httplib::Request&, httplib::Response& res) {
        send(res, ok(json{{"service", "zcode-platform"},
                          {"version", "0.1.0"},
                          {"db", "sqlite3+sqlite-vec"},
                          {"time", nowIso()}}));
    });

    // ---- Agent 注册（主密钥）----
    srv.Post("/api/agents/register", [&](const httplib::Request& req, httplib::Response& res) {
        if (!checkMaster(req, p, res)) return;
        auto body = json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.is_object()) { send(res, fail(400, "invalid JSON body")); return; }
        std::string name = body.value("name", "");
        std::string role = body.value("role", kDefaultRole);
        std::string apiKey, err;
        if (!p.registerAgent(req.get_header_value("X-Master-Key"), name, role, apiKey, err)) {
            send(res, fail(400, err));
            return;
        }
        // 密钥明文只返回这一次，Agent 端应妥善保存
        send(res, ok(json{{"name", name}, {"role", role}, {"api_key", apiKey}}));
    });

    // ---- Agent 协作者列表（启动时必查）----
    srv.Get("/api/agents", [&](const httplib::Request& req, httplib::Response& res) {
        std::string actor;
        if (!checkAgent(req, p, actor, res)) return;
        std::vector<AgentInfo> agents;
        std::string err;
        if (!p.listAgents(agents, err)) { send(res, fail(500, err)); return; }
        json arr = json::array();
        for (const auto& a : agents)
            arr.push_back({{"name", a.name},
                           {"role", a.role},
                           {"status", a.status},
                           {"current_task", a.current_task},
                           {"last_seen_at", a.last_seen_at},
                           {"created_at", a.created_at}});
        send(res, ok(arr));
    });

    srv.Post("/api/agents/heartbeat", [&](const httplib::Request& req, httplib::Response& res) {
        std::string actor;
        if (!checkAgent(req, p, actor, res)) return;
        auto body = json::parse(req.body, nullptr, false);
        std::string task = body.is_object() ? body.value("current_task", "") : "";
        p.heartbeat(actor, task);
        send(res, ok());
    });

    // ---- 用户记忆（启动时必查）----
    srv.Get("/api/memory", [&](const httplib::Request& req, httplib::Response& res) {
        std::string actor;
        if (!checkAgent(req, p, actor, res)) return;
        std::string section = req.has_param("section") ? req.get_param_value("section") : "";
        std::vector<MemoryEntry> entries;
        std::string err;
        if (!p.memoryList(section, entries, err)) { send(res, fail(500, err)); return; }
        json arr = json::array();
        for (const auto& m : entries)
            arr.push_back({{"section", m.section},
                           {"key", m.key},
                           {"value", m.value},
                           {"author", m.author},
                           {"version", m.version},
                           {"created_at", m.created_at}});
        send(res, ok(arr));
    });

    srv.Post("/api/memory", [&](const httplib::Request& req, httplib::Response& res) {
        std::string actor;
        if (!checkAgent(req, p, actor, res)) return;
        auto body = json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.is_object()) { send(res, fail(400, "invalid JSON body")); return; }
        std::string section = body.value("section", "");
        std::string key = body.value("key", "");
        std::string value = body.value("value", "");
        int baseVersion = body.value("base_version", 0);
        MemoryEntry out;
        std::string err;
        if (!p.memorySet(actor, section, key, value, baseVersion, out, err)) {
            // 乐观并发冲突 → 409，携带服务端最新版本
            if (err.rfind("version conflict", 0) == 0) {
                send(res, fail(409, err));
                return;
            }
            send(res, fail(400, err));
            return;
        }
        send(res, ok(json{{"section", out.section}, {"key", out.key}, {"value", out.value},
                          {"author", out.author}, {"version", out.version},
                          {"created_at", out.created_at}}));
    });

    srv.Get("/api/memory/history", [&](const httplib::Request& req, httplib::Response& res) {
        std::string actor;
        if (!checkAgent(req, p, actor, res)) return;
        std::string section = req.get_param_value("section");
        std::string key = req.get_param_value("key");
        if (section.empty() || key.empty()) { send(res, fail(400, "section and key are required")); return; }
        std::vector<MemoryEntry> entries;
        std::string err;
        if (!p.memoryHistory(section, key, entries, err)) { send(res, fail(500, err)); return; }
        json arr = json::array();
        for (const auto& m : entries)
            arr.push_back({{"section", m.section},
                           {"key", m.key},
                           {"value", m.value},
                           {"author", m.author},
                           {"version", m.version},
                           {"created_at", m.created_at}});
        send(res, ok(arr));
    });

    // ---- 知识库 ----
    srv.Post("/api/knowledge", [&](const httplib::Request& req, httplib::Response& res) {
        std::string actor;
        if (!checkAgent(req, p, actor, res)) return;
        auto body = json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.is_object()) { send(res, fail(400, "invalid JSON body")); return; }
        std::string title = body.value("title", "");
        std::string content = body.value("content", "");
        std::string category = body.value("category", "");
        std::string embedder = body.value("embedder", "");
        std::vector<std::string> tags;
        if (body.contains("tags") && body["tags"].is_array())
            for (const auto& t : body["tags"]) tags.push_back(t.get<std::string>());
        bool hasEmb = false;
        std::vector<float> emb;
        std::string err;
        if (!extractEmbedding(body, p, hasEmb, emb, err)) { send(res, fail(400, err)); return; }
        KnowledgeEntry out;
        if (!p.knowledgeCreate(actor, title, content, tags, category, hasEmb ? emb : std::vector<float>{},
                               embedder, out, err)) {
            send(res, fail(400, err));
            return;
        }
        send(res, ok(knowledgeToJson(out)));
    });

    srv.Get("/api/knowledge", [&](const httplib::Request& req, httplib::Response& res) {
        std::string actor;
        if (!checkAgent(req, p, actor, res)) return;
        int limit = req.has_param("limit") ? std::stoi(req.get_param_value("limit")) : 100;
        std::string tag = req.has_param("tag") ? req.get_param_value("tag") : "";
        std::vector<KnowledgeEntry> entries;
        std::string err;
        if (!p.knowledgeList(limit, tag, entries, err)) { send(res, fail(500, err)); return; }
        json arr = json::array();
        for (const auto& e : entries) arr.push_back(knowledgeToJson(e));
        send(res, ok(arr));
    });

    srv.Post("/api/knowledge/search", [&](const httplib::Request& req, httplib::Response& res) {
        std::string actor;
        if (!checkAgent(req, p, actor, res)) return;
        auto body = json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.is_object()) { send(res, fail(400, "invalid JSON body")); return; }
        std::string query = body.value("query", "");
        std::string mode = body.value("mode", "keyword");
        int limit = body.value("limit", 20);
        std::string tag = body.value("tag", "");
        SearchMode m = (mode == "semantic") ? SearchMode::Semantic : SearchMode::Keyword;
        std::vector<KnowledgeHit> hits;
        std::string err;
        if (!p.knowledgeSearch(query, m, limit, tag, hits, err)) { send(res, fail(400, err)); return; }
        json arr = json::array();
        for (const auto& h : hits) {
            json j = knowledgeToJson(h.entry, h.score);
            j["match_mode"] = mode;
            arr.push_back(j);
        }
        send(res, ok(arr));
    });

    srv.Get(R"(/api/knowledge/([0-9a-fA-F-]{36}))", [&](const httplib::Request& req, httplib::Response& res) {
        std::string actor;
        if (!checkAgent(req, p, actor, res)) return;
        KnowledgeEntry out;
        std::string err;
        if (!p.knowledgeLatest(req.matches[1], out, err)) { send(res, fail(404, err)); return; }
        send(res, ok(knowledgeToJson(out)));
    });

    srv.Get(R"(/api/knowledge/([0-9a-fA-F-]{36})/versions)", [&](const httplib::Request& req, httplib::Response& res) {
        std::string actor;
        if (!checkAgent(req, p, actor, res)) return;
        std::vector<KnowledgeEntry> entries;
        std::string err;
        if (!p.knowledgeVersions(req.matches[1], entries, err)) { send(res, fail(404, err)); return; }
        json arr = json::array();
        for (const auto& e : entries) arr.push_back(knowledgeToJson(e));
        send(res, ok(arr));
    });

    srv.Post(R"(/api/knowledge/([0-9a-fA-F-]{36})/versions)", [&](const httplib::Request& req, httplib::Response& res) {
        std::string actor;
        if (!checkAgent(req, p, actor, res)) return;
        auto body = json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.is_object()) { send(res, fail(400, "invalid JSON body")); return; }
        std::string content = body.value("content", "");
        std::string title = body.value("title", "");
        std::string embedder = body.value("embedder", "");
        bool hasEmb = false;
        std::vector<float> emb;
        std::string err;
        if (!extractEmbedding(body, p, hasEmb, emb, err)) { send(res, fail(400, err)); return; }
        KnowledgeEntry out;
        if (!p.knowledgeAddVersion(actor, req.matches[1], title, content,
                                   hasEmb ? emb : std::vector<float>{}, embedder, out, err)) {
            send(res, fail(404, err));
            return;
        }
        send(res, ok(knowledgeToJson(out)));
    });

    // ---- 技能库 ----
    srv.Post("/api/skills", [&](const httplib::Request& req, httplib::Response& res) {
        std::string actor;
        if (!checkAgent(req, p, actor, res)) return;
        auto body = json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.is_object()) { send(res, fail(400, "invalid JSON body")); return; }
        std::string name = body.value("name", "");
        std::string display = body.value("display_name", "");
        std::string desc = body.value("description", "");
        std::string category = body.value("category", "");
        std::string schema = body.contains("param_schema") ? body["param_schema"].dump() : "{}";
        SkillInfo out;
        std::string err;
        if (!p.skillRegister(actor, name, display, desc, category, schema, out, err)) {
            send(res, fail(400, err));
            return;
        }
        send(res, ok(json{{"name", out.name},
                          {"display_name", out.display_name},
                          {"description", out.description},
                          {"category", out.category},
                          {"owner_agent", out.owner_agent},
                          {"param_schema", json::parse(out.param_schema)},
                          {"version", out.version},
                          {"status", out.status}}));
    });

    srv.Get("/api/skills", [&](const httplib::Request& req, httplib::Response& res) {
        std::string actor;
        if (!checkAgent(req, p, actor, res)) return;
        std::string category = req.has_param("category") ? req.get_param_value("category") : "";
        std::string owner = req.has_param("owner") ? req.get_param_value("owner") : "";
        std::vector<SkillInfo> skills;
        std::string err;
        if (!p.skillList(category, owner, skills, err)) { send(res, fail(500, err)); return; }
        json arr = json::array();
        for (const auto& s : skills)
            arr.push_back({{"name", s.name},
                           {"display_name", s.display_name},
                           {"description", s.description},
                           {"category", s.category},
                           {"owner_agent", s.owner_agent},
                           {"param_schema", json::parse(s.param_schema)},
                           {"version", s.version},
                           {"status", s.status},
                           {"updated_at", s.updated_at}});
        send(res, ok(arr));
    });

    srv.Get(R"(/api/skills/([^/]+))", [&](const httplib::Request& req, httplib::Response& res) {
        std::string actor;
        if (!checkAgent(req, p, actor, res)) return;
        SkillInfo out;
        std::string err;
        if (!p.skillGet(req.matches[1], out, err)) { send(res, fail(404, err)); return; }
        send(res, ok(json{{"name", out.name},
                          {"display_name", out.display_name},
                          {"description", out.description},
                          {"category", out.category},
                          {"owner_agent", out.owner_agent},
                          {"param_schema", json::parse(out.param_schema)},
                          {"version", out.version},
                          {"status", out.status}}));
    });

    srv.Post(R"(/api/skills/([^/]+)/invoke)", [&](const httplib::Request& req, httplib::Response& res) {
        std::string actor;
        if (!checkAgent(req, p, actor, res)) return;
        auto body = json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.is_object()) { send(res, fail(400, "invalid JSON body")); return; }
        std::string params = body.contains("params") ? body["params"].dump() : "{}";
        std::string result = body.value("result_summary", "");
        std::string status = body.value("status", "success");
        int64_t duration = body.value("duration_ms", 0);
        int64_t tin = body.value("tokens_in", 0);
        int64_t tout = body.value("tokens_out", 0);
        std::string err;
        if (!p.skillInvoke(actor, req.matches[1], params, result, status, duration, tin, tout, err)) {
            // 周预算耗尽 → 429（请求方应停止消耗并等待下周或预算调整）
            if (err.rfind("weekly token budget exceeded", 0) == 0) {
                send(res, fail(429, err));
                return;
            }
            send(res, fail(400, err));
            return;
        }
        UsageSummary sum;
        p.usageSummary(sum, err);
        send(res, ok(json{{"skill", req.matches[1]},
                          {"remaining_tokens", sum.budget - sum.total_tokens},
                          {"budget", sum.budget},
                          {"alert_level", sum.alert_level}}));
    });

    srv.Get(R"(/api/skills/([^/]+)/invocations)", [&](const httplib::Request& req, httplib::Response& res) {
        std::string actor;
        if (!checkAgent(req, p, actor, res)) return;
        int limit = req.has_param("limit") ? std::stoi(req.get_param_value("limit")) : 100;
        std::vector<SkillInvocation> invs;
        std::string err;
        if (!p.skillInvocations(req.matches[1], limit, invs, err)) { send(res, fail(500, err)); return; }
        json arr = json::array();
        for (const auto& i : invs)
            arr.push_back({{"skill_name", i.skill_name},
                           {"caller_agent", i.caller_agent},
                           {"params", json::parse(i.params)},
                           {"result_summary", i.result_summary},
                           {"status", i.status},
                           {"duration_ms", i.duration_ms},
                           {"created_at", i.created_at}});
        send(res, ok(arr));
    });

    // ---- 消息 ----
    srv.Post("/api/messages", [&](const httplib::Request& req, httplib::Response& res) {
        std::string actor;
        if (!checkAgent(req, p, actor, res)) return;
        auto body = json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.is_object()) { send(res, fail(400, "invalid JSON body")); return; }
        std::string kind = body.value("kind", "note");
        std::string recipient = body.value("recipient", "");
        std::string subject = body.value("subject", "");
        std::string text = body.value("body", "");
        Message out;
        std::string err;
        if (!p.messageSend(kind, actor, recipient, subject, text, out, err)) {
            send(res, fail(400, err));
            return;
        }
        send(res, ok(json{{"uuid", out.uuid},
                          {"kind", out.kind},
                          {"sender", out.sender},
                          {"recipient", recipientJson(out.recipient)},
                          {"status", out.status},
                          {"created_at", out.created_at}}));
    });

    srv.Get("/api/messages", [&](const httplib::Request& req, httplib::Response& res) {
        std::string actor;
        if (!checkAgent(req, p, actor, res)) return;
        std::string recipient = req.has_param("recipient") ? req.get_param_value("recipient") : "";
        std::string kind = req.has_param("kind") ? req.get_param_value("kind") : "";
        std::string status = req.has_param("status") ? req.get_param_value("status") : "";
        std::string since = req.has_param("since") ? req.get_param_value("since") : "";
        int limit = req.has_param("limit") ? std::stoi(req.get_param_value("limit")) : 100;
        std::vector<Message> msgs;
        std::string err;
        if (!p.messageList(recipient, kind, status, since, limit, msgs, err)) {
            send(res, fail(500, err));
            return;
        }
        json arr = json::array();
        for (const auto& m : msgs)
            arr.push_back({{"uuid", m.uuid},
                           {"kind", m.kind},
                           {"sender", m.sender},
                           {"recipient", recipientJson(m.recipient)},
                           {"subject", m.subject},
                           {"body", m.body},
                           {"status", m.status},
                           {"parent_uuid", m.parent_uuid},
                           {"created_at", m.created_at}});
        send(res, ok(arr));
    });

    srv.Post(R"(/api/messages/([0-9a-fA-F-]{36})/reply)", [&](const httplib::Request& req, httplib::Response& res) {
        std::string actor;
        if (!checkAgent(req, p, actor, res)) return;
        auto body = json::parse(req.body, nullptr, false);
        std::string text = body.is_object() ? body.value("body", "") : "";
        Message out;
        std::string err;
        if (!p.messageReply(actor, req.matches[1], text, out, err)) { send(res, fail(404, err)); return; }
        send(res, ok(json{{"uuid", out.uuid}, {"recipient", out.recipient}, {"status", out.status}}));
    });

    srv.Post(R"(/api/messages/([0-9a-fA-F-]{36})/status)", [&](const httplib::Request& req, httplib::Response& res) {
        std::string actor;
        if (!checkAgent(req, p, actor, res)) return;
        auto body = json::parse(req.body, nullptr, false);
        std::string status = body.is_object() ? body.value("status", "") : "";
        Message out;
        std::string err;
        if (!p.messageSetStatus(actor, req.matches[1], status, out, err)) {
            send(res, fail(400, err));
            return;
        }
        send(res, ok(json{{"uuid", out.uuid}, {"status", out.status}}));
    });

    // ---- 错误日志 ----
    srv.Post("/api/errors", [&](const httplib::Request& req, httplib::Response& res) {
        std::string actor;
        if (!checkAgent(req, p, actor, res)) return;
        auto body = json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.is_object()) { send(res, fail(400, "invalid JSON body")); return; }
        std::string severity = body.value("severity", "error");
        std::string source = body.value("source", "");
        std::string title = body.value("title", "");
        std::string detail = body.value("detail", "");
        std::string stack = body.value("stack_trace", "");
        ErrorReport out;
        std::string err;
        if (!p.errorReport(actor, severity, source, title, detail, stack, out, err)) {
            send(res, fail(400, err));
            return;
        }
        send(res, ok(json{{"uuid", out.uuid}, {"status", out.status}, {"created_at", out.created_at}}));
    });

    srv.Get("/api/errors", [&](const httplib::Request& req, httplib::Response& res) {
        std::string actor;
        if (!checkAgent(req, p, actor, res)) return;
        std::string status = req.has_param("status") ? req.get_param_value("status") : "";
        std::string severity = req.has_param("severity") ? req.get_param_value("severity") : "";
        int limit = req.has_param("limit") ? std::stoi(req.get_param_value("limit")) : 200;
        std::vector<ErrorReport> errors;
        std::string err;
        if (!p.errorList(status, severity, limit, errors, err)) { send(res, fail(500, err)); return; }
        json arr = json::array();
        for (const auto& e : errors)
            arr.push_back({{"uuid", e.uuid},
                           {"reporter", e.reporter},
                           {"severity", e.severity},
                           {"source", e.source},
                           {"title", e.title},
                           {"detail", e.detail},
                           {"stack_trace", e.stack_trace},
                           {"status", e.status},
                           {"resolution_notes", e.resolution_notes},
                           {"resolved_by", e.resolved_by},
                           {"created_at", e.created_at},
                           {"resolved_at", e.resolved_at}});
        send(res, ok(arr));
    });

    srv.Post(R"(/api/errors/([0-9a-fA-F-]{36})/resolve)", [&](const httplib::Request& req, httplib::Response& res) {
        std::string actor;
        if (!checkAgent(req, p, actor, res)) return;
        auto body = json::parse(req.body, nullptr, false);
        std::string notes = body.is_object() ? body.value("notes", "") : "";
        ErrorReport out;
        std::string err;
        if (!p.errorResolve(actor, req.matches[1], notes, out, err)) { send(res, fail(400, err)); return; }
        send(res, ok(json{{"uuid", out.uuid}, {"status", out.status}, {"resolved_by", out.resolved_by}}));
    });

    // ---- Token 用量 ----
    srv.Post("/api/usage/report", [&](const httplib::Request& req, httplib::Response& res) {
        std::string actor;
        if (!checkAgent(req, p, actor, res)) return;
        auto body = json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.is_object()) { send(res, fail(400, "invalid JSON body")); return; }
        int64_t tin = body.value("tokens_in", 0);
        int64_t tout = body.value("tokens_out", 0);
        std::string type = body.value("call_type", "");
        std::string ref = body.value("reference_id", "");
        std::string idem = body.value("idempotency_key", "");
        bool duplicate = false;
        std::string err;
        if (!p.usageReport(actor, tin, tout, type, ref, idem, duplicate, err)) {
            send(res, fail(400, err));
            return;
        }
        UsageSummary sum;
        if (!p.usageSummary(sum, err)) { send(res, fail(500, err)); return; }
        send(res, ok(json{{"week_start", sum.week_start},
                          {"budget", sum.budget},
                          {"used", sum.total_tokens},
                          {"remaining", sum.budget - sum.total_tokens},
                          {"alert_level", sum.alert_level},
                          {"duplicate", duplicate}}));
    });

    srv.Get("/api/usage/summary", [&](const httplib::Request& req, httplib::Response& res) {
        std::string actor;
        if (!checkAgent(req, p, actor, res)) return;
        UsageSummary sum;
        std::string err;
        if (!p.usageSummary(sum, err)) { send(res, fail(500, err)); return; }
        json per = json::array();
        for (const auto& [name, tokens] : sum.per_agent)
            per.push_back({{"agent", name}, {"tokens", tokens}});
        send(res, ok(json{{"week_start", sum.week_start},
                          {"budget", sum.budget},
                          {"total_in", sum.total_in},
                          {"total_out", sum.total_out},
                          {"total_tokens", sum.total_tokens},
                          {"remaining", sum.budget - sum.total_tokens},
                          {"alert_level", sum.alert_level},
                          {"per_agent", per}}));
    });

    srv.Get("/api/usage/budget", [&](const httplib::Request& req, httplib::Response& res) {
        std::string actor;
        if (!checkAgent(req, p, actor, res)) return;
        std::string err;
        send(res, ok(json{{"budget", p.usageBudget(err)}}));
    });

    srv.Put("/api/usage/budget", [&](const httplib::Request& req, httplib::Response& res) {
        if (!checkMaster(req, p, res)) return;
        auto body = json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.is_object() || !body.contains("budget")) {
            send(res, fail(400, "budget is required"));
            return;
        }
        std::string err;
        if (!p.usageSetBudget(kManagerName, body["budget"].get<int64_t>(), err)) {
            send(res, fail(400, err));
            return;
        }
        send(res, ok(json{{"budget", body["budget"].get<int64_t>()}}));
    });

    // ---- 操作日志 ----
    srv.Get("/api/audit", [&](const httplib::Request& req, httplib::Response& res) {
        std::string actor;
        if (!checkAgent(req, p, actor, res)) return;
        std::string a = req.has_param("actor") ? req.get_param_value("actor") : "";
        std::string act = req.has_param("action") ? req.get_param_value("action") : "";
        std::string since = req.has_param("since") ? req.get_param_value("since") : "";
        int limit = req.has_param("limit") ? std::stoi(req.get_param_value("limit")) : 200;
        std::vector<AuditRecord> records;
        std::string err;
        if (!p.auditList(a, act, since, limit, records, err)) { send(res, fail(500, err)); return; }
        json arr = json::array();
        for (const auto& r : records)
            arr.push_back({{"id", r.id},
                           {"actor", r.actor},
                           {"action", r.action},
                           {"target", r.target},
                           {"detail", json::parse(r.detail)},
                           {"created_at", r.created_at}});
        send(res, ok(arr));
    });

    // ---- 运维（主密钥）：维护 / 备份恢复 / 管理性删除 ----
    srv.Post("/api/maintenance", [&](const httplib::Request& req, httplib::Response& res) {
        if (!checkMaster(req, p, res)) return;
        std::string stats, err;
        if (!p.maintenanceRun(kManagerName, stats, err)) { send(res, fail(500, err)); return; }
        send(res, ok(json::parse(stats)));
    });

    srv.Post("/api/system/backup", [&](const httplib::Request& req, httplib::Response& res) {
        if (!checkMaster(req, p, res)) return;
        std::string path, err;
        if (!p.backupCreate(path, err)) { send(res, fail(500, err)); return; }
        send(res, ok(json{{"path", path}}));
    });

    srv.Get("/api/system/backups", [&](const httplib::Request& req, httplib::Response& res) {
        if (!checkMaster(req, p, res)) return;
        std::vector<std::string> files;
        std::string err;
        if (!p.backupList(files, err)) { send(res, fail(500, err)); return; }
        send(res, ok(files));
    });

    srv.Post("/api/system/restore", [&](const httplib::Request& req, httplib::Response& res) {
        if (!checkMaster(req, p, res)) return;
        auto body = json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.is_object()) { send(res, fail(400, "invalid JSON body")); return; }
        std::string err;
        if (!p.backupRestore(body.value("file", ""), err)) { send(res, fail(400, err)); return; }
        send(res, ok(json{{"restored", body.value("file", "")}}));
    });

    srv.Delete(R"(/api/knowledge/([0-9a-fA-F-]{36}))", [&](const httplib::Request& req, httplib::Response& res) {
        if (!checkMaster(req, p, res)) return;
        std::string err;
        if (!p.knowledgeRemove(kManagerName, req.matches[1], err)) { send(res, fail(404, err)); return; }
        send(res, ok(json{{"removed", req.matches[1]}}));
    });

    srv.Delete("/api/memory", [&](const httplib::Request& req, httplib::Response& res) {
        if (!checkMaster(req, p, res)) return;
        std::string section = req.get_param_value("section");
        std::string key = req.get_param_value("key");
        std::string err;
        if (!p.memoryRemove(kManagerName, section, key, err)) { send(res, fail(404, err)); return; }
        send(res, ok(json{{"removed", section + "/" + key}}));
    });
}

}  // namespace zp

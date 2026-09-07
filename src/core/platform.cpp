#include "core/platform.h"

#include <filesystem>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>
#include <sqlite-vec.h>

#include "core/http/server.h"
#include "core/util.h"
#include "schema_sql.h"

namespace zp {

namespace fs = std::filesystem;

std::string defaultHomeDir() {
    if (const char* env = std::getenv("ZCODE_PLATFORM_HOME"); env && *env) return env;
#ifdef _WIN32
    if (const char* up = std::getenv("USERPROFILE"); up && *up)
        return std::string(up) + "\\.zcode-platform";
#endif
    if (const char* home = std::getenv("HOME"); home && *home)
        return std::string(home) + "/.zcode-platform";
    return ".zcode-platform";
}

Platform::Platform(std::string homeDir)
    : home_dir_(std::move(homeDir)),
      embedder_(std::make_unique<NgramHashEmbedder>()),
      agents_(db_),
      knowledge_(db_, *embedder_, embedder_->dim()),
      skills_(db_),
      memory_(db_),
      messages_(db_),
      errors_(db_),
      usage_(db_),
      audit_(db_) {}

Platform::~Platform() { shutdown(); }

bool Platform::bootstrap(std::string& err) {
    std::lock_guard lock(mutex_);
    if (bootstrapped_) return true;

    std::error_code ec;
    fs::create_directories(fs::path(home_dir_) / "config", ec);
    if (ec) { err = "cannot create home dir: " + ec.message(); return false; }

    std::string dbPath = (fs::path(home_dir_) / "platform.db").string();
    if (!db_.open(dbPath, err)) return false;
    if (!db_.execScript(kSchemaSql, err)) return false;
    // 启用 sqlite-vec（静态链接进本进程）
    if (sqlite3_vec_init(db_.handle(), nullptr, nullptr) != SQLITE_OK) {
        err = sqlite3_errmsg(db_.handle());
        return false;
    }
    if (!knowledge_.ensureVecTable(err)) return false;

    // 预算默认值（可被 settings 覆盖）
    if (!db_.query("INSERT OR IGNORE INTO settings(key, value) VALUES('weekly_token_budget', ?)",
                   [](Stmt& st) { st.bind(1, kDefaultWeeklyBudget); }, nullptr, err))
        return false;

    // 主密钥：环境变量优先，其次运行期生成的文件；绝不写入源码
    std::string masterKey;
    if (const char* env = std::getenv("ZCODE_PLATFORM_MASTER_KEY"); env && *env) {
        masterKey = env;
    } else {
        std::string keyPath = (fs::path(home_dir_) / "config" / "master.key").string();
        std::ifstream in(keyPath);
        if (in) { std::getline(in, masterKey); in.close(); }
        if (masterKey.empty()) {
            masterKey = randomHex(32);
            std::ofstream out(keyPath, std::ios::trunc);
            out << masterKey << "\n";
            out.close();
        }
    }
    master_key_hash_ = sha256Hex(masterKey);

    // 管理者账号由主密钥引导注册（幂等）
    if (!agents_.nameExists(kManagerName)) {
        std::string zcodeKey = randomHex(32);
        if (!agents_.registerAgent(kManagerName, kManagerName, sha256Hex(zcodeKey), err))
            return false;
        if (!persistAgentKey(kManagerName, zcodeKey, err)) return false;
        audit_.log("system", "agent.register", kManagerName,
                   nlohmann::json{{"role", "zcode"}}.dump(), err);
    }

    bootstrapped_ = true;
    return true;
}

void Platform::shutdown() {
    std::lock_guard lock(mutex_);
    stopHttpServer();
    db_.close();
    bootstrapped_ = false;
}

bool Platform::persistAgentKey(const std::string& name, const std::string& apiKey,
                               std::string& err) {
    // 密钥明文仅存于运行期生成的数据目录配置文件，供 Agent 侧命令行取用
    std::string path = (fs::path(home_dir_) / "config" / "agents.json").string();
    nlohmann::json j;
    {
        std::ifstream in(path);
        if (in) { in >> j; in.close(); }
    }
    if (!j.is_object()) j = nlohmann::json::object();
    j[name] = apiKey;
    std::ofstream out(path, std::ios::trunc);
    out << j.dump(2) << "\n";
    out.close();
    return out.good() || true;  // 尽力而为，不阻塞注册
}

bool Platform::authenticate(const std::string& agent, const std::string& apiKey) const {
    std::lock_guard lock(mutex_);
    std::string err;
    std::string stored = agents_.keyHashOf(agent, err);
    if (stored.empty()) return false;
    return stored == sha256Hex(apiKey);
}

bool Platform::authenticateMaster(const std::string& masterKey) const {
    std::lock_guard lock(mutex_);
    return !master_key_hash_.empty() && master_key_hash_ == sha256Hex(masterKey);
}

bool Platform::isManager(const std::string& name) const { return name == kManagerName; }

bool Platform::registerAgent(const std::string& masterKey, const std::string& name,
                             const std::string& role, std::string& outApiKey, std::string& err) {
    std::lock_guard lock(mutex_);
    if (!authenticateMaster(masterKey)) { err = "invalid master key"; return false; }
    if (name.empty() || name.find_first_of(" \t\r\n") != std::string::npos) {
        err = "invalid agent name";
        return false;
    }
    if (agents_.nameExists(name)) { err = "agent already registered: " + name; return false; }
    std::string actualRole = name == kManagerName ? std::string(kManagerName) : role;
    if (actualRole.empty()) actualRole = kDefaultRole;
    outApiKey = randomHex(32);
    if (!agents_.registerAgent(name, actualRole, sha256Hex(outApiKey), err)) return false;
    persistAgentKey(name, outApiKey, err);
    audit_.log("master", "agent.register", name,
               nlohmann::json{{"role", actualRole}}.dump(), err);
    return true;
}

void Platform::heartbeat(const std::string& name, const std::string& currentTask) {
    std::lock_guard lock(mutex_);
    std::string prevTask, prevSeen;
    bool found = false;
    std::string err;
    db_.query("SELECT current_task, last_seen_at FROM agents WHERE name=?",
              [&](Stmt& st) { st.bind(1, name); },
              [&](Stmt& st) {
                  prevTask = st.isNull(0) ? std::string() : st.text(0);
                  prevSeen = st.isNull(1) ? std::string() : st.text(1);
                  found = true;
              },
              err);
    if (!found) return;
    agents_.heartbeat(name, currentTask);
    // 状态/任务变化才留痕，避免高频心跳淹没审计表
    bool wasOffline = prevSeen.empty();
    std::time_t t = 0;
    if (!prevSeen.empty() && parseIso(prevSeen, t) && (std::time(nullptr) - t) > 120) wasOffline = true;
    if (wasOffline || prevTask != currentTask) {
        audit_.log(name, "agent.heartbeat", name,
                   nlohmann::json{{"task", currentTask}}.dump(), err);
    }
}

bool Platform::listAgents(std::vector<AgentInfo>& out, std::string& err) {
    std::lock_guard lock(mutex_);
    return agents_.listAgents(out, err);
}

std::vector<float> Platform::resolveEmbedding(const std::string& content,
                                              const std::vector<float>* provided,
                                              bool& isProvided) {
    if (provided && !provided->empty() && static_cast<int>(provided->size()) == embedder_->dim()) {
        isProvided = true;
        return *provided;
    }
    isProvided = false;
    return embedder_->embed(content);
}

std::vector<float> Platform::embedText(const std::string& text) {
    std::lock_guard lock(mutex_);
    return embedder_->embed(text);
}

int Platform::embeddingDim() const { return embedder_->dim(); }

// ---------------- 知识库 ----------------

bool Platform::knowledgeCreate(const std::string& author, const std::string& title,
                               const std::string& content, const std::vector<std::string>& tags,
                               const std::string& category, const std::vector<float>& embedding,
                               const std::string& embeddingProvider, KnowledgeEntry& out,
                               std::string& err) {
    std::lock_guard lock(mutex_);
    if (title.empty() || content.empty()) { err = "title and content are required"; return false; }
    bool provided = false;
    std::vector<float> vec = resolveEmbedding(content, &embedding, provided);
    std::string provider = provided ? embeddingProvider : embedder_->name();
    if (provider.empty()) provider = embedder_->name();
    std::string tagsJson = nlohmann::json(tags).dump();
    if (!knowledge_.create(author, title, content, tagsJson, category, vec, provider, out, err))
        return false;
    audit_.log(author, "knowledge.create", out.uuid,
               nlohmann::json{{"title", title}, {"tags", tags}, {"category", category}}.dump(), err);
    return true;
}

bool Platform::knowledgeList(int limit, const std::string& tagFilter,
                             std::vector<KnowledgeEntry>& out, std::string& err) {
    std::lock_guard lock(mutex_);
    return knowledge_.list(limit, tagFilter, out, err);
}

bool Platform::knowledgeLatest(const std::string& uuid, KnowledgeEntry& out, std::string& err) {
    std::lock_guard lock(mutex_);
    return knowledge_.latest(uuid, out, err);
}

bool Platform::knowledgeVersions(const std::string& uuid, std::vector<KnowledgeEntry>& out,
                                 std::string& err) {
    std::lock_guard lock(mutex_);
    return knowledge_.versions(uuid, out, err);
}

bool Platform::knowledgeAddVersion(const std::string& author, const std::string& uuid,
                                   const std::string& newTitle, const std::string& newContent,
                                   const std::vector<float>& embedding,
                                   const std::string& embeddingProvider, KnowledgeEntry& out,
                                   std::string& err) {
    std::lock_guard lock(mutex_);
    if (newContent.empty()) { err = "new content is required"; return false; }
    bool provided = false;
    std::vector<float> vec = resolveEmbedding(newContent, &embedding, provided);
    std::string provider = provided ? embeddingProvider : embedder_->name();
    if (provider.empty()) provider = embedder_->name();
    if (!knowledge_.addVersion(author, uuid, newTitle, newContent, vec, provider, out, err))
        return false;
    audit_.log(author, "knowledge.version.add", uuid,
               nlohmann::json{{"version", out.version}}.dump(), err);
    return true;
}

bool Platform::knowledgeSearch(const std::string& query, SearchMode mode, int limit,
                               const std::string& tagFilter, std::vector<KnowledgeHit>& out,
                               std::string& err) {
    std::lock_guard lock(mutex_);
    if (query.empty()) { err = "query is required"; return false; }
    if (mode == SearchMode::Semantic) {
        std::vector<float> qv = embedder_->embed(query);
        return knowledge_.searchSemantic(qv, limit, tagFilter, out, err);
    }
    std::vector<KnowledgeEntry> entries;
    if (!knowledge_.searchKeyword(query, limit, tagFilter, entries, err)) return false;
    out.clear();
    for (auto& e : entries) out.push_back({std::move(e), 0.0});
    return true;
}

// ---------------- 技能库 ----------------

bool Platform::skillRegister(const std::string& author, const std::string& name,
                             const std::string& displayName, const std::string& description,
                             const std::string& category, const std::string& paramSchema,
                             SkillInfo& out, std::string& err) {
    std::lock_guard lock(mutex_);
    if (name.empty() || description.empty()) { err = "name and description are required"; return false; }
    if (!skills_.registerSkill(name, displayName, description, category, author, paramSchema, out,
                               err))
        return false;
    audit_.log(author, "skill.register", name,
               nlohmann::json{{"description", description}, {"category", category}}.dump(), err);
    return true;
}

bool Platform::skillList(const std::string& categoryFilter, const std::string& ownerFilter,
                         std::vector<SkillInfo>& out, std::string& err) {
    std::lock_guard lock(mutex_);
    return skills_.list(categoryFilter, ownerFilter, out, err);
}

bool Platform::skillGet(const std::string& name, SkillInfo& out, std::string& err) {
    std::lock_guard lock(mutex_);
    return skills_.get(name, out, err);
}

bool Platform::skillInvoke(const std::string& caller, const std::string& skillName,
                           const std::string& paramsJson, const std::string& resultSummary,
                           const std::string& status, int64_t durationMs, int64_t tokensIn,
                           int64_t tokensOut, std::string& err) {
    std::lock_guard lock(mutex_);
    // 协作规则：新技能必须先注册再调用
    if (!skills_.isRegistered(skillName)) {
        err = "skill not registered: " + skillName;
        return false;
    }
    if (status != "success" && status != "failed") { err = "status must be success|failed"; return false; }
    if (!skills_.recordInvocation(skillName, caller, paramsJson, resultSummary, status, durationMs,
                                  err))
        return false;
    if (tokensIn > 0 || tokensOut > 0) {
        if (!usage_.report(caller, tokensIn, tokensOut, "skill", skillName, err)) return false;
    }
    audit_.log(caller, "skill.invoke", skillName,
               nlohmann::json{{"status", status}, {"duration_ms", durationMs}}.dump(), err);
    return true;
}

bool Platform::skillInvocations(const std::string& skillName, int limit,
                                std::vector<SkillInvocation>& out, std::string& err) {
    std::lock_guard lock(mutex_);
    return skills_.listInvocations(skillName, limit, out, err);
}

// ---------------- 用户记忆 ----------------

bool Platform::memoryList(const std::string& section, std::vector<MemoryEntry>& out,
                          std::string& err) {
    std::lock_guard lock(mutex_);
    return memory_.list(section, out, err);
}

bool Platform::memorySet(const std::string& author, const std::string& section,
                         const std::string& key, const std::string& value, MemoryEntry& out,
                         std::string& err) {
    std::lock_guard lock(mutex_);
    if (section.empty() || key.empty()) { err = "section and key are required"; return false; }
    if (!memory_.set(author, section, key, value, out, err)) return false;
    audit_.log(author, "memory.set", section + "/" + key,
               nlohmann::json{{"version", out.version}, {"value", value}}.dump(), err);
    return true;
}

bool Platform::memoryHistory(const std::string& section, const std::string& key,
                             std::vector<MemoryEntry>& out, std::string& err) {
    std::lock_guard lock(mutex_);
    return memory_.history(section, key, out, err);
}

// ---------------- 消息 ----------------

bool Platform::messageSend(const std::string& kind, const std::string& sender,
                           const std::string& recipient, const std::string& subject,
                           const std::string& body, Message& out, std::string& err) {
    std::lock_guard lock(mutex_);
    if (body.empty()) { err = "body is required"; return false; }
    if (!messages_.send(kind, sender, recipient, subject, body, std::string(), out, err))
        return false;
    audit_.log(sender, "message.send", out.uuid,
               nlohmann::json{{"kind", kind}, {"recipient", recipient}, {"subject", subject}}.dump(),
               err);
    return true;
}

bool Platform::messageList(const std::string& recipientFilter, const std::string& kindFilter,
                           const std::string& statusFilter, const std::string& sinceIso, int limit,
                           std::vector<Message>& out, std::string& err) {
    std::lock_guard lock(mutex_);
    return messages_.list(recipientFilter, kindFilter, statusFilter, sinceIso, limit, out, err);
}

bool Platform::messageGet(const std::string& uuid, Message& out, std::string& err) {
    std::lock_guard lock(mutex_);
    return messages_.get(uuid, out, err);
}

bool Platform::messageReply(const std::string& sender, const std::string& parentUuid,
                            const std::string& body, Message& out, std::string& err) {
    std::lock_guard lock(mutex_);
    if (!messages_.reply(parentUuid, sender, body, out, err)) return false;
    audit_.log(sender, "message.reply", out.uuid,
               nlohmann::json{{"parent", parentUuid}}.dump(), err);
    return true;
}

bool Platform::messageSetStatus(const std::string& actor, const std::string& uuid,
                                const std::string& newStatus, Message& out, std::string& err) {
    std::lock_guard lock(mutex_);
    Message cur;
    if (!messages_.get(uuid, cur, err)) return false;
    // 仅收件人、发件人、用户（工作台操作者）或管理者可流转状态
    if (actor != "user" && actor != cur.recipient && actor != cur.sender && !isManager(actor)) {
        err = "not allowed to change this message's status";
        return false;
    }
    if (!messages_.setStatus(uuid, newStatus, out, err)) return false;
    audit_.log(actor, "message.status", uuid, nlohmann::json{{"status", newStatus}}.dump(), err);
    return true;
}

// ---------------- 错误日志 ----------------

bool Platform::errorReport(const std::string& reporter, const std::string& severity,
                           const std::string& source, const std::string& title,
                           const std::string& detail, const std::string& stackTrace,
                           ErrorReport& out, std::string& err) {
    std::lock_guard lock(mutex_);
    if (title.empty() || detail.empty()) { err = "title and detail are required"; return false; }
    std::string sev = severity;
    if (sev != "info" && sev != "warning" && sev != "error" && sev != "critical") sev = "error";
    if (!errors_.report(reporter, sev, source, title, detail, stackTrace, out, err)) return false;
    audit_.log(reporter, "error.report", out.uuid,
               nlohmann::json{{"severity", sev}, {"title", title}}.dump(), err);
    return true;
}

bool Platform::errorList(const std::string& statusFilter, const std::string& severityFilter,
                         int limit, std::vector<ErrorReport>& out, std::string& err) {
    std::lock_guard lock(mutex_);
    return errors_.list(statusFilter, severityFilter, limit, out, err);
}

bool Platform::errorResolve(const std::string& actor, const std::string& uuid,
                            const std::string& notes, ErrorReport& out, std::string& err) {
    std::lock_guard lock(mutex_);
    ErrorReport cur;
    if (!errors_.get(uuid, cur, err)) return false;
    if (actor != "user" && actor != cur.reporter && !isManager(actor)) {
        err = "only the reporter or zcode can resolve this error";
        return false;
    }
    if (!errors_.resolve(uuid, actor, notes, out, err)) return false;
    audit_.log(actor, "error.resolve", uuid, nlohmann::json{{"notes", notes}}.dump(), err);
    return true;
}

// ---------------- Token 用量 ----------------

bool Platform::usageReport(const std::string& agent, int64_t tokensIn, int64_t tokensOut,
                           const std::string& callType, const std::string& referenceId,
                           std::string& err) {
    std::lock_guard lock(mutex_);
    if (!usage_.report(agent, tokensIn, tokensOut, callType, referenceId, err)) return false;
    audit_.log(agent, "usage.report", referenceId,
               nlohmann::json{{"tokens_in", tokensIn}, {"tokens_out", tokensOut},
                              {"call_type", callType}}.dump(),
               err);
    return true;
}

bool Platform::usageSummary(UsageSummary& out, std::string& err) {
    std::lock_guard lock(mutex_);
    return usage_.summary(out, err);
}

int64_t Platform::usageBudget(std::string& err) {
    std::lock_guard lock(mutex_);
    return usage_.budget(err);
}

bool Platform::usageSetBudget(const std::string& actor, int64_t budget, std::string& err) {
    std::lock_guard lock(mutex_);
    if (actor != "user" && !isManager(actor)) { err = "only the user or zcode can change the budget"; return false; }
    if (!usage_.setBudget(budget, err)) return false;
    audit_.log(actor, "budget.set", std::string(),
               nlohmann::json{{"budget", budget}}.dump(), err);
    return true;
}

// ---------------- 操作日志 ----------------

bool Platform::auditList(const std::string& actorFilter, const std::string& actionFilter,
                         const std::string& sinceIso, int limit, std::vector<AuditRecord>& out,
                         std::string& err) {
    std::lock_guard lock(mutex_);
    return audit_.list(actorFilter, actionFilter, sinceIso, limit, out, err);
}

// ---------------- HTTP ----------------

bool Platform::startHttpServer(int port, std::string& err) {
    std::lock_guard lock(mutex_);
    if (http_ && http_->running()) return true;
    http_ = std::make_unique<HttpServer>(*this);
    return http_->start(port, err);
}

void Platform::stopHttpServer() {
    if (http_) http_->stop();
}

int Platform::httpPort() const {
    std::lock_guard lock(mutex_);
    return http_ ? http_->port() : 0;
}

bool Platform::httpRunning() const {
    std::lock_guard lock(mutex_);
    return http_ && http_->running();
}

}  // namespace zp

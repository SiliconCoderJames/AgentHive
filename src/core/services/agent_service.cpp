#include "core/services/agent_service.h"

#include "core/util.h"

namespace ah {

namespace {
// 心跳超过 2 分钟视为离线
constexpr int64_t kOnlineWindowSec = 120;
}  // namespace

bool AgentService::registerAgent(const std::string& name, const std::string& role,
                                 const std::string& salt, const std::string& keyHash,
                                 std::string& err) {
    std::string now = nowIso();
    return db_.query(
        "INSERT INTO agents(name, role, api_key_hash, salt, status, current_task, last_seen_at, created_at, updated_at) "
        "VALUES (?,?,?,?, 'offline', '', ?, ?, ?)",
        [&](Stmt& st) {
            st.bind(1, name);
            st.bind(2, role);
            st.bind(3, keyHash);
            st.bind(4, salt);
            st.bind(5, now);
            st.bind(6, now);
            st.bind(7, now);
        },
        nullptr, err);
}

bool AgentService::credentialOf(const std::string& name, std::string& salt, std::string& hash,
                                std::string& err) const {
    salt.clear();
    hash.clear();
    bool found = false;
    bool ok = db_.query(
        "SELECT api_key_hash, salt FROM agents WHERE name = ?",
        [&](Stmt& st) { st.bind(1, name); },
        [&](Stmt& st) {
            hash = st.text(0);
            salt = st.text(1);
            found = true;
        },
        err);
    return ok && found;
}

void AgentService::heartbeat(const std::string& name, const std::string& currentTask) {
    std::string err;
    db_.query(
        "UPDATE agents SET status='online', current_task=?, last_seen_at=?, updated_at=? WHERE name=?",
        [&](Stmt& st) {
            st.bind(1, currentTask);
            st.bind(2, nowIso());
            st.bind(3, nowIso());
            st.bind(4, name);
        },
        nullptr, err);
}

bool AgentService::listAgents(std::vector<AgentInfo>& out, std::string& err) {
    std::vector<AgentInfo> all;
    bool ok = db_.query(
        "SELECT name, role, status, current_task, last_seen_at, created_at FROM agents ORDER BY name",
        nullptr,
        [&](Stmt& st) {
            AgentInfo a;
            a.name = st.text(0);
            a.role = st.text(1);
            a.status = st.text(2);
            a.current_task = st.isNull(3) ? std::string() : st.text(3);
            a.last_seen_at = st.isNull(4) ? std::string() : st.text(4);
            a.created_at = st.text(5);
            all.push_back(std::move(a));
        },
        err);
    if (!ok) return false;

    std::time_t now = std::time(nullptr);
    out = std::move(all);
    for (auto& a : out) {
        std::time_t t = 0;
        if (a.last_seen_at.empty() || !parseIso(a.last_seen_at, t) || (now - t) > kOnlineWindowSec)
            a.status = "offline";
        else
            a.status = "online";
    }
    return true;
}

bool AgentService::removeAgent(const std::string& name, std::string& err) {
    return db_.query("DELETE FROM agents WHERE name = ?",
                     [&](Stmt& st) { st.bind(1, name); }, nullptr, err);
}

bool AgentService::nameExists(const std::string& name) {
    std::string err;
    bool found = false;
    db_.query("SELECT 1 FROM agents WHERE name = ?",
              [&](Stmt& st) { st.bind(1, name); },
              [&](Stmt&) { found = true; }, err);
    return found;
}

}  // namespace ah

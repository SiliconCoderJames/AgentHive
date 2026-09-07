#pragma once
// Agent 注册 / 认证 / 心跳 / 在线状态。
#include <string>
#include <vector>

#include "core/db/database.h"
#include "core/types.h"

namespace zp {

class AgentService {
public:
    explicit AgentService(Database& db) : db_(db) {}

    bool registerAgent(const std::string& name, const std::string& role,
                       const std::string& apiKeyHash, std::string& err);
    std::string keyHashOf(const std::string& name, std::string& err) const;
    void heartbeat(const std::string& name, const std::string& currentTask);
    bool listAgents(std::vector<AgentInfo>& out, std::string& err);
    bool nameExists(const std::string& name);

private:
    Database& db_;
};

}  // namespace zp

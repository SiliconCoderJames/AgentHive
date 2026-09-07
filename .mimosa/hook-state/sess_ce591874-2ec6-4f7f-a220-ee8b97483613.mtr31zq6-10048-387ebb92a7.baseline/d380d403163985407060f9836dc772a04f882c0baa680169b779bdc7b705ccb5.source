#pragma once
// 操作日志：每次操作记录身份 + 时间 + 动作 + 对象 + 内容摘要。
#include <string>
#include <vector>

#include "core/db/database.h"
#include "core/types.h"

namespace zp {

class AuditService {
public:
    explicit AuditService(Database& db) : db_(db) {}

    bool log(const std::string& actor, const std::string& action, const std::string& target,
             const std::string& detailJson, std::string& err);
    bool list(const std::string& actorFilter, const std::string& actionFilter,
              const std::string& sinceIso, int limit, std::vector<AuditRecord>& out, std::string& err);

private:
    Database& db_;
};

}  // namespace zp

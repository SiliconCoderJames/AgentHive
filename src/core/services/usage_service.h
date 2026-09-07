#pragma once
// Token 管控：每次调用上报消耗；按自然周（周一 UTC 起）聚合；
// 达到预算 80% 告警（warn）、95% 预警（critical）、超出标记 over。
#include <cstdint>
#include <string>
#include <vector>

#include "core/db/database.h"
#include "core/types.h"

namespace zp {

constexpr int64_t kDefaultWeeklyBudget = 10'000'000;  // 每周 1000 万 Token

class UsageService {
public:
    explicit UsageService(Database& db) : db_(db) {}

    // idempotencyKey 非空时幂等：同一键重复上报只记一次，duplicate=true 标记
    bool report(const std::string& agent, int64_t tokensIn, int64_t tokensOut,
                const std::string& callType, const std::string& referenceId,
                const std::string& idempotencyKey, bool& duplicate, std::string& err);
    bool summary(UsageSummary& out, std::string& err);
    int64_t budget(std::string& err);
    bool setBudget(int64_t budget, std::string& err);

private:
    Database& db_;
};

}  // namespace zp

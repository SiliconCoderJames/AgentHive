#include "core/services/usage_service.h"

#include "core/util.h"

namespace zp {

bool UsageService::report(const std::string& agent, int64_t tokensIn, int64_t tokensOut,
                          const std::string& callType, const std::string& referenceId,
                          const std::string& idempotencyKey, bool& duplicate, std::string& err) {
    duplicate = false;
    if (tokensIn < 0 || tokensOut < 0) { err = "token counts must be >= 0"; return false; }
    if (idempotencyKey.size() > 200) { err = "idempotency_key too long (max 200)"; return false; }

    // BEGIN IMMEDIATE + 事务内查重：并发/重复上报不会双重扣减
    if (!db_.beginImmediate(err)) return false;
    if (!idempotencyKey.empty()) {
        bool exists = false;
        if (!db_.query("SELECT 1 FROM token_usage WHERE idempotency_key = ?",
                       [&](Stmt& st) { st.bind(1, idempotencyKey); },
                       [&](Stmt&) { exists = true; }, err)) {
            db_.rollback();
            return false;
        }
        if (exists) {
            duplicate = true;
            return db_.commit(err);
        }
    }
    if (!db_.query(
            "INSERT INTO token_usage(agent, week_start, tokens_in, tokens_out, call_type, reference_id, idempotency_key, created_at) "
            "VALUES (?,?,?,?,?,?,?,?)",
            [&](Stmt& st) {
                st.bind(1, agent);
                st.bind(2, weekStartIso());
                st.bind(3, tokensIn);
                st.bind(4, tokensOut);
                st.bind(5, callType);
                st.bind(6, referenceId);
                st.bind(7, idempotencyKey);
                st.bind(8, nowIso());
            },
            nullptr, err)) {
        db_.rollback();
        return false;
    }
    return db_.commit(err);
}

int64_t UsageService::budget(std::string& err) {
    int64_t value = kDefaultWeeklyBudget;
    db_.query("SELECT value FROM settings WHERE key='weekly_token_budget'", nullptr,
              [&](Stmt& st) { value = st.i64(0); }, err);
    return value;
}

bool UsageService::setBudget(int64_t newBudget, std::string& err) {
    if (newBudget <= 0) { err = "budget must be positive"; return false; }
    return db_.query(
        "INSERT INTO settings(key, value) VALUES('weekly_token_budget', ?) "
        "ON CONFLICT(key) DO UPDATE SET value = excluded.value",
        [&](Stmt& st) { st.bind(1, newBudget); }, nullptr, err);
}

bool UsageService::summary(UsageSummary& out, std::string& err) {
    out = UsageSummary{};
    out.week_start = weekStartIso();
    out.budget = budget(err);

    if (!db_.query(
            "SELECT COALESCE(SUM(tokens_in),0), COALESCE(SUM(tokens_out),0) FROM token_usage WHERE week_start=?",
            [&](Stmt& st) { st.bind(1, out.week_start); },
            [&](Stmt& st) {
                out.total_in = st.i64(0);
                out.total_out = st.i64(1);
            },
            err))
        return false;
    out.total_tokens = out.total_in + out.total_out;

    if (!db_.query(
            "SELECT agent, SUM(tokens_in + tokens_out) AS t FROM token_usage WHERE week_start=? "
            "GROUP BY agent ORDER BY t DESC",
            [&](Stmt& st) { st.bind(1, out.week_start); },
            [&](Stmt& st) { out.per_agent.emplace_back(st.text(0), st.i64(1)); }, err))
        return false;

    double ratio = out.budget > 0 ? static_cast<double>(out.total_tokens) / out.budget : 0.0;
    if (ratio > 1.0) out.alert_level = "over";
    else if (ratio >= 0.95) out.alert_level = "critical";
    else if (ratio >= 0.80) out.alert_level = "warn";
    else out.alert_level = "none";
    return true;
}

}  // namespace zp

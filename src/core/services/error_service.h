#pragma once
// 错误日志：报错必须记录；解决时只追加说明，不覆盖原始内容。
#include <string>
#include <vector>

#include "core/db/database.h"
#include "core/types.h"

namespace zp {

class ErrorService {
public:
    explicit ErrorService(Database& db) : db_(db) {}

    bool report(const std::string& reporter, const std::string& severity, const std::string& source,
                const std::string& title, const std::string& detail, const std::string& stackTrace,
                ErrorReport& out, std::string& err);
    bool list(const std::string& statusFilter, const std::string& severityFilter, int limit,
              std::vector<ErrorReport>& out, std::string& err);
    bool get(const std::string& uuid, ErrorReport& out, std::string& err);
    bool resolve(const std::string& uuid, const std::string& actor, const std::string& notes,
                 ErrorReport& out, std::string& err);

private:
    Database& db_;
};

}  // namespace zp

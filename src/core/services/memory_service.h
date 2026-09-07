#pragma once
// 用户记忆：section + key 定位条目；修改生成新版本，历史完整保留。
#include <string>
#include <vector>

#include "core/db/database.h"
#include "core/types.h"

namespace zp {

class MemoryService {
public:
    explicit MemoryService(Database& db) : db_(db) {}

    bool list(const std::string& sectionFilter, std::vector<MemoryEntry>& out, std::string& err);
    // baseVersion > 0 时做乐观并发校验：与最新版本不符则报 version conflict（不写入）
    bool set(const std::string& author, const std::string& section, const std::string& key,
             const std::string& value, int baseVersion, MemoryEntry& out, std::string& err);
    bool remove(const std::string& section, const std::string& key, int64_t& removed, std::string& err);
    bool history(const std::string& section, const std::string& key,
                 std::vector<MemoryEntry>& out, std::string& err);

private:
    Database& db_;
};

}  // namespace zp

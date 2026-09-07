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
    // 存在则生成新版本（旧版本 is_latest 置 0、内容不动），不存在则新建 v1。
    bool set(const std::string& author, const std::string& section, const std::string& key,
             const std::string& value, MemoryEntry& out, std::string& err);
    bool history(const std::string& section, const std::string& key,
                 std::vector<MemoryEntry>& out, std::string& err);

private:
    Database& db_;
};

}  // namespace zp

#include "core/services/memory_service.h"

#include "core/util.h"

namespace zp {

bool MemoryService::list(const std::string& sectionFilter, std::vector<MemoryEntry>& out,
                         std::string& err) {
    std::string sql =
        "SELECT section, key, value, author, version, created_at FROM memory_entries WHERE is_latest=1";
    if (!sectionFilter.empty()) sql += " AND section = ?";
    sql += " ORDER BY section, key";
    out.clear();
    return db_.query(
        sql,
        [&](Stmt& st) {
            if (!sectionFilter.empty()) st.bind(1, sectionFilter);
        },
        [&](Stmt& st) {
            MemoryEntry m;
            m.section = st.text(0);
            m.key = st.text(1);
            m.value = st.text(2);
            m.author = st.text(3);
            m.version = static_cast<int>(st.i64(4));
            m.created_at = st.text(5);
            out.push_back(std::move(m));
        },
        err);
}

bool MemoryService::set(const std::string& author, const std::string& section, const std::string& key,
                        const std::string& value, MemoryEntry& out, std::string& err) {
    int64_t latestId = 0;
    int latestVersion = 0;
    bool found = false;
    if (!db_.query(
            "SELECT id, version FROM memory_entries WHERE section=? AND key=? AND is_latest=1",
            [&](Stmt& st) { st.bind(1, section); st.bind(2, key); },
            [&](Stmt& st) { latestId = st.i64(0); latestVersion = static_cast<int>(st.i64(1)); found = true; },
            err))
        return false;

    int newVersion = found ? latestVersion + 1 : 1;
    if (found) {
        // 仅翻转版本标记，旧内容原样保留（append-only）
        if (!db_.query("UPDATE memory_entries SET is_latest=0 WHERE id=?",
                       [&](Stmt& st) { st.bind(1, latestId); }, nullptr, err))
            return false;
    }
    if (!db_.query(
            "INSERT INTO memory_entries(section, key, value, author, version, is_latest, created_at) "
            "VALUES (?,?,?,?,?,1,?)",
            [&](Stmt& st) {
                st.bind(1, section);
                st.bind(2, key);
                st.bind(3, value);
                st.bind(4, author);
                st.bind(5, static_cast<int64_t>(newVersion));
                st.bind(6, nowIso());
            },
            nullptr, err))
        return false;

    out.section = section;
    out.key = key;
    out.value = value;
    out.author = author;
    out.version = newVersion;
    out.created_at = nowIso();
    return true;
}

bool MemoryService::history(const std::string& section, const std::string& key,
                            std::vector<MemoryEntry>& out, std::string& err) {
    out.clear();
    return db_.query(
        "SELECT section, key, value, author, version, created_at FROM memory_entries "
        "WHERE section=? AND key=? ORDER BY version DESC",
        [&](Stmt& st) { st.bind(1, section); st.bind(2, key); },
        [&](Stmt& st) {
            MemoryEntry m;
            m.section = st.text(0);
            m.key = st.text(1);
            m.value = st.text(2);
            m.author = st.text(3);
            m.version = static_cast<int>(st.i64(4));
            m.created_at = st.text(5);
            out.push_back(std::move(m));
        },
        err);
}

}  // namespace zp

#include "core/services/knowledge_service.h"

#include <cstring>

#include <nlohmann/json.hpp>

#include "core/util.h"

namespace zp {

namespace {
std::string vecToBytes(const std::vector<float>& v) {
    std::string bytes(v.size() * sizeof(float), '\0');
    std::memcpy(bytes.data(), v.data(), bytes.size());
    return bytes;
}
// tags_json -> tags 数组（tags_json 始终是合法 JSON，异常时静默保留空列表）
void parseTags(KnowledgeEntry& e) {
    try {
        auto tags = nlohmann::json::parse(e.tags_json);
        if (tags.is_array())
            for (const auto& t : tags) e.tags.push_back(t.get<std::string>());
    } catch (...) {
    }
}
}  // namespace

bool KnowledgeService::ensureVecTable(std::string& err) {
    // vec_dim_ 是内部配置整数，不来自外部输入
    std::string ddl =
        "CREATE VIRTUAL TABLE IF NOT EXISTS knowledge_vec USING vec0("
        "entry_id INTEGER PRIMARY KEY, embedding float[" +
        std::to_string(vec_dim_) + "])";
    return db_.execScript(ddl, err);
}

bool KnowledgeService::create(const std::string& author, const std::string& title,
                              const std::string& content, const std::string& tagsJson,
                              const std::string& category, const std::vector<float>& embedding,
                              const std::string& embeddingProvider, KnowledgeEntry& out,
                              std::string& err) {
    std::string uuid = uuid4();
    if (!db_.query(
            "INSERT INTO knowledge_entries(uuid, title, content, tags_json, category, author, version, "
            "parent_version_id, is_latest, embedding_provider, created_at) "
            "VALUES (?,?,?,?,?,?,1,NULL,1,?,?)",
            [&](Stmt& st) {
                st.bind(1, uuid);
                st.bind(2, title);
                st.bind(3, content);
                st.bind(4, tagsJson);
                st.bind(5, category);
                st.bind(6, author);
                st.bind(7, embeddingProvider);
                st.bind(8, nowIso());
            },
            nullptr, err))
        return false;
    int64_t id = db_.lastInsertId();
    if (!insertVec(id, embedding, err)) return false;
    return latest(uuid, out, err);
}

bool KnowledgeService::insertVec(int64_t entryId, const std::vector<float>& vec, std::string& err) {
    std::string bytes = vecToBytes(vec);
    return db_.query("INSERT INTO knowledge_vec(entry_id, embedding) VALUES (?,?)",
                     [&](Stmt& st) {
                         st.bind(1, entryId);
                         st.bindBlob(2, bytes.data(), bytes.size());
                     },
                     nullptr, err);
}

bool KnowledgeService::latest(const std::string& uuid, KnowledgeEntry& out, std::string& err) {
    bool found = false;
    bool ok = db_.query(
        "SELECT id, uuid, title, content, tags_json, category, author, version, embedding_provider, created_at "
        "FROM knowledge_entries WHERE uuid=? AND is_latest=1",
        [&](Stmt& st) { st.bind(1, uuid); },
        [&](Stmt& st) {
            out.id = st.i64(0);
            out.uuid = st.text(1);
            out.title = st.text(2);
            out.content = st.text(3);
            out.tags_json = st.text(4);
            out.category = st.isNull(5) ? std::string() : st.text(5);
            out.author = st.text(6);
            out.version = static_cast<int>(st.i64(7));
            out.embedding_provider = st.isNull(8) ? std::string() : st.text(8);
            out.created_at = st.text(9);
            found = true;
        },
        err);
    if (!ok) return false;
    if (!found) { err = "knowledge not found: " + uuid; return false; }
    try {
        auto tags = nlohmann::json::parse(out.tags_json);
        if (tags.is_array())
            for (const auto& t : tags) out.tags.push_back(t.get<std::string>());
    } catch (...) {
    }
    return true;
}

bool KnowledgeService::versions(const std::string& uuid, std::vector<KnowledgeEntry>& out,
                                std::string& err) {
    out.clear();
    return db_.query(
        "SELECT id, uuid, title, content, tags_json, category, author, version, embedding_provider, created_at "
        "FROM knowledge_entries WHERE uuid=? ORDER BY version DESC",
        [&](Stmt& st) { st.bind(1, uuid); },
        [&](Stmt& st) {
            KnowledgeEntry e;
            e.id = st.i64(0);
            e.uuid = st.text(1);
            e.title = st.text(2);
            e.content = st.text(3);
            e.tags_json = st.text(4);
            e.category = st.isNull(5) ? std::string() : st.text(5);
            e.author = st.text(6);
            e.version = static_cast<int>(st.i64(7));
            e.embedding_provider = st.isNull(8) ? std::string() : st.text(8);
            e.created_at = st.text(9);
            out.push_back(std::move(e));
        },
        err);
}

bool KnowledgeService::addVersion(const std::string& author, const std::string& uuid,
                                  const std::string& newTitle, const std::string& newContent,
                                  const std::vector<float>& embedding,
                                  const std::string& embeddingProvider, KnowledgeEntry& out,
                                  std::string& err) {
    // BEGIN IMMEDIATE 包裹查-改-插：双进程并发追加同一 uuid 时不会产生两条 is_latest=1
    if (!db_.beginImmediate(err)) return false;

    // 追加新版本：旧版本内容原样保留，仅翻转 is_latest 标记
    int64_t oldId = 0;
    int oldVersion = 0;
    std::string oldTitle, oldTags, oldCategory;
    bool found = false;
    if (!db_.query(
            "SELECT id, version, title, tags_json, category FROM knowledge_entries WHERE uuid=? AND is_latest=1",
            [&](Stmt& st) { st.bind(1, uuid); },
            [&](Stmt& st) {
                oldId = st.i64(0);
                oldVersion = static_cast<int>(st.i64(1));
                oldTitle = st.text(2);
                oldTags = st.text(3);
                oldCategory = st.isNull(4) ? std::string() : st.text(4);
                found = true;
            },
            err)) {
        db_.rollback();
        return false;
    }
    if (!found) {
        err = "knowledge not found: " + uuid;
        db_.rollback();
        return false;
    }

    if (!db_.query("UPDATE knowledge_entries SET is_latest=0 WHERE id=?",
                   [&](Stmt& st) { st.bind(1, oldId); }, nullptr, err)) {
        db_.rollback();
        return false;
    }

    std::string title = newTitle.empty() ? oldTitle : newTitle;
    if (!db_.query(
            "INSERT INTO knowledge_entries(uuid, title, content, tags_json, category, author, version, "
            "parent_version_id, is_latest, embedding_provider, created_at) "
            "VALUES (?,?,?,?,?,?,?,?,1,?,?)",
            [&](Stmt& st) {
                st.bind(1, uuid);
                st.bind(2, title);
                st.bind(3, newContent);
                st.bind(4, oldTags);
                st.bind(5, oldCategory);
                st.bind(6, author);
                st.bind(7, static_cast<int64_t>(oldVersion + 1));
                st.bind(8, oldId);
                st.bind(9, embeddingProvider);
                st.bind(10, nowIso());
            },
            nullptr, err)) {
        db_.rollback();
        return false;
    }
    int64_t id = db_.lastInsertId();
    if (!insertVec(id, embedding, err)) {
        db_.rollback();
        return false;
    }
    if (!db_.commit(err)) {
        db_.rollback();
        return false;
    }
    return latest(uuid, out, err);
}

bool KnowledgeService::fetchByIds(const std::vector<int64_t>& ids, std::vector<KnowledgeEntry>& out,
                                  std::string& err) {
    out.clear();
    for (int64_t id : ids) {
        bool found = false;
        KnowledgeEntry e;
        if (!db_.query(
                "SELECT id, uuid, title, content, tags_json, category, author, version, "
                "embedding_provider, created_at FROM knowledge_entries WHERE id=? AND is_latest=1",
                [&](Stmt& st) { st.bind(1, id); },
                [&](Stmt& st) {
                    e.id = st.i64(0);
                    e.uuid = st.text(1);
                    e.title = st.text(2);
                    e.content = st.text(3);
                    e.tags_json = st.text(4);
                    e.category = st.isNull(5) ? std::string() : st.text(5);
                    e.author = st.text(6);
                    e.version = static_cast<int>(st.i64(7));
                    e.embedding_provider = st.isNull(8) ? std::string() : st.text(8);
                    e.created_at = st.text(9);
                    found = true;
                },
                err))
            return false;
        if (found) {
            parseTags(e);
            out.push_back(std::move(e));
        }
    }
    return true;
}

bool KnowledgeService::list(int limit, const std::string& tagFilter,
                            std::vector<KnowledgeEntry>& out, std::string& err) {
    std::string sql =
        "SELECT id FROM knowledge_entries WHERE is_latest=1";
    if (!tagFilter.empty()) sql += " AND tags_json LIKE ?";
    sql += " ORDER BY id DESC LIMIT ?";
    std::vector<int64_t> ids;
    if (!db_.query(
            sql,
            [&](Stmt& st) {
                int idx = 1;
                if (!tagFilter.empty()) st.bind(idx++, "%\"" + tagFilter + "\"%");
                st.bind(idx, static_cast<int64_t>(limit > 0 ? limit : 100));
            },
            [&](Stmt& st) { ids.push_back(st.i64(0)); }, err))
        return false;
    return fetchByIds(ids, out, err);
}

bool KnowledgeService::searchKeyword(const std::string& query, int limit, const std::string& tagFilter,
                                     std::vector<KnowledgeEntry>& out, std::string& err) {
    std::string like = "%" + query + "%";
    std::string sql =
        "SELECT id FROM knowledge_entries WHERE is_latest=1 AND (title LIKE ? OR content LIKE ?)";
    if (!tagFilter.empty()) sql += " AND tags_json LIKE ?";
    sql += " ORDER BY id DESC LIMIT ?";
    std::vector<int64_t> ids;
    if (!db_.query(
            sql,
            [&](Stmt& st) {
                st.bind(1, like);
                st.bind(2, like);
                int idx = 3;
                if (!tagFilter.empty()) st.bind(idx++, "%\"" + tagFilter + "\"%");
                st.bind(idx, static_cast<int64_t>(limit > 0 ? limit : 100));
            },
            [&](Stmt& st) { ids.push_back(st.i64(0)); }, err))
        return false;
    return fetchByIds(ids, out, err);
}

bool KnowledgeService::searchSemantic(const std::vector<float>& queryVec, int limit,
                                      const std::string& tagFilter, std::vector<KnowledgeHit>& out,
                                      std::string& err) {
    std::string bytes = vecToBytes(queryVec);
    out.clear();
    std::vector<std::pair<int64_t, double>> hits;
    // vec0 约束：k 与 LIMIT 不能同时出现；k 已隐含按 distance 升序返回
    if (!db_.query(
            "SELECT entry_id, distance FROM knowledge_vec WHERE embedding MATCH ? AND k = ?",
            [&](Stmt& st) {
                st.bindBlob(1, bytes.data(), bytes.size());
                st.bind(2, static_cast<int64_t>(limit > 0 ? limit : 20));
            },
            [&](Stmt& st) { hits.emplace_back(st.i64(0), st.dbl(1)); }, err))
        return false;

    for (const auto& [id, dist] : hits) {
        KnowledgeEntry e;
        bool found = false;
        if (!db_.query(
                "SELECT id, uuid, title, content, tags_json, category, author, version, "
                "embedding_provider, created_at FROM knowledge_entries WHERE id=? AND is_latest=1",
                [&](Stmt& st) { st.bind(1, id); },
                [&](Stmt& st) {
                    e.id = st.i64(0);
                    e.uuid = st.text(1);
                    e.title = st.text(2);
                    e.content = st.text(3);
                    e.tags_json = st.text(4);
                    e.category = st.isNull(5) ? std::string() : st.text(5);
                    e.author = st.text(6);
                    e.version = static_cast<int>(st.i64(7));
                    e.embedding_provider = st.isNull(8) ? std::string() : st.text(8);
                    e.created_at = st.text(9);
                    found = true;
                },
                err))
            return false;
        if (found) {
            parseTags(e);
            if (!tagFilter.empty()) {
                bool tagHit = e.tags_json.find("\"" + tagFilter + "\"") != std::string::npos;
                if (!tagHit) continue;
            }
            out.push_back({std::move(e), dist});
        }
    }
    return true;
}

}  // namespace zp

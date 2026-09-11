#pragma once
// 共享知识库：向量化语义搜索 + 关键词搜索；内容只追加、以版本演进。
#include <string>
#include <vector>

#include "core/db/database.h"
#include "core/embed/embedder.h"
#include "core/types.h"

namespace ah {

class KnowledgeService {
public:
    KnowledgeService(Database& db, Embedder& embedder, int vecDim)
        : db_(db), embedder_(embedder), vec_dim_(vecDim) {}

    // 创建 vec0 虚拟表（维度来自配置；SQL 仅含内部整型，无外部输入）
    bool ensureVecTable(std::string& err);

    bool create(const std::string& author, const std::string& title, const std::string& content,
                const std::string& tagsJson, const std::string& category,
                const std::vector<float>& embedding, const std::string& embeddingProvider,
                KnowledgeEntry& out, std::string& err);
    bool latest(const std::string& uuid, KnowledgeEntry& out, std::string& err);
    bool versions(const std::string& uuid, std::vector<KnowledgeEntry>& out, std::string& err);
    bool addVersion(const std::string& author, const std::string& uuid, const std::string& newTitle,
                    const std::string& newContent, const std::vector<float>& embedding,
                    const std::string& embeddingProvider, KnowledgeEntry& out, std::string& err);
    bool list(int limit, const std::string& tagFilter, std::vector<KnowledgeEntry>& out, std::string& err);
    bool searchKeyword(const std::string& query, int limit, const std::string& tagFilter,
                       std::vector<KnowledgeEntry>& out, std::string& err);
    bool searchSemantic(const std::vector<float>& queryVec, int limit, const std::string& tagFilter,
                        std::vector<KnowledgeHit>& out, std::string& err);

private:
    bool insertVec(int64_t entryId, const std::vector<float>& vec, std::string& err);
    bool fetchByIds(const std::vector<int64_t>& ids, std::vector<KnowledgeEntry>& out, std::string& err);

    Database& db_;
    Embedder& embedder_;
    int vec_dim_;
};

}  // namespace ah

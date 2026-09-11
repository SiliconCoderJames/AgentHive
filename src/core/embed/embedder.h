#pragma once
// 文本向量化接口。
// 默认实现 NgramHashEmbedder：字符 n-gram 特征哈希 + L2 归一化，
// 完全离线、零模型依赖，作为平台开箱即用的兜底。
// 有模型能力的 Agent 在写入知识时可自带 embedding（见 HTTP API），
// 平台会记录 provider 名称。后续可插入 ONNX Runtime 实现。
#include <string>
#include <vector>

namespace ah {

class Embedder {
public:
    virtual ~Embedder() = default;
    virtual std::string name() const = 0;
    virtual int dim() const = 0;
    virtual std::vector<float> embed(const std::string& text) = 0;
};

class NgramHashEmbedder : public Embedder {
public:
    explicit NgramHashEmbedder(int dim = 384);
    // v2：引入词间分隔符，向量与 v1 不兼容（provider 名随之升级）
    std::string name() const override { return "ngram-hash-v2"; }
    int dim() const override { return dim_; }
    std::vector<float> embed(const std::string& text) override;

private:
    int dim_;
};

}  // namespace ah

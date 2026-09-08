#include "core/embed/embedder.h"

#include <cmath>

#include "core/util.h"

namespace zp {

namespace {
// 词间分隔符：把空白折叠成单一码点参与哈希，阻断跨词 n-gram 碰撞
constexpr uint32_t kSep = 0x1F;

// FNV-1a 64 位哈希
uint64_t fnv1a(uint64_t h, uint32_t v) {
    h ^= static_cast<uint64_t>(v);
    h *= 0x100000001b3ULL;
    return h;
}
}  // namespace

NgramHashEmbedder::NgramHashEmbedder(int dim) : dim_(dim > 0 ? dim : 384) {}

std::vector<float> NgramHashEmbedder::embed(const std::string& text) {
    std::vector<float> vec(static_cast<size_t>(dim_), 0.0f);
    // 按 UTF-8 码点切分，中英文、代码标识符均可处理；
    // 空白折叠为分隔符：跨词 n-gram 必含 kSep，"AB C" 与 "A BC" 不再同哈希
    auto raw = utf8Codepoints(toLower(text));
    std::vector<uint32_t> cps;
    cps.reserve(raw.size());
    for (uint32_t c : raw) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == 0x0B || c == 0x0C) {
            if (cps.empty() || cps.back() != kSep) cps.push_back(kSep);
        } else {
            cps.push_back(c);
        }
    }
    for (size_t i = 0; i < cps.size(); ++i) {
        uint64_t h = 0xcbf29ce484222325ULL;
        for (size_t j = i; j < cps.size() && j < i + 3; ++j) h = fnv1a(h, cps[j]);
        // 2-gram
        if (i + 1 < cps.size()) {
            uint64_t h2 = fnv1a(0xcbf29ce484222325ULL, cps[i]);
            h2 = fnv1a(h2, cps[i + 1]);
            size_t b = h2 % static_cast<size_t>(dim_);
            vec[b] += ((h2 >> 32) & 1) ? 1.0f : -1.0f;
        }
        // 3-gram
        if (i + 2 < cps.size()) {
            size_t b = h % static_cast<size_t>(dim_);
            vec[b] += ((h >> 32) & 1) ? 1.0f : -1.0f;
        }
    }
    // L2 归一化；零向量兜底（纯空白输入等）
    double norm = 0.0;
    for (float v : vec) norm += static_cast<double>(v) * v;
    norm = std::sqrt(norm);
    if (norm < 1e-9) {
        vec[0] = 1.0f;
        return vec;
    }
    for (auto& v : vec) v = static_cast<float>(v / norm);
    return vec;
}

}  // namespace zp

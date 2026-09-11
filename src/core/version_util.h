#pragma once
// 版本号比较。原先这段逻辑只存在于 GUI 的更新检查里、且没有测试，
// 现在下沉到 core：更新检查、/api/health 展示、安装包与文档口径都从同一处取值。
//
// 约定（语义化版本的常用简化）：
//   * 允许 "v1.2.3" 前缀与 "1.2.3" 两种写法；
//   * 主/次/修订按数值比较，缺位补 0（"1.2" == "1.2.0"）；
//   * 数值段之外的字符忽略，但预发布后缀会被识别：同号时 "1.0.0-rc1" < "1.0.0"；
//     两个都是预发布时按后缀字符串比较（"rc1" < "rc2"）。
#include <cctype>
#include <string>

namespace ah {

// 返回 <0 表示 a 更旧，0 表示等价，>0 表示 a 更新
inline int compareVersions(const std::string& a, const std::string& b) {
    auto split = [](const std::string& in, int out[3], std::string& suffix) {
        out[0] = out[1] = out[2] = 0;
        suffix.clear();
        size_t i = 0;
        if (i < in.size() && (in[i] == 'v' || in[i] == 'V')) ++i;
        int field = 0;
        long value = 0;
        bool any = false;
        for (; i < in.size() && field < 3; ++i) {
            const unsigned char c = static_cast<unsigned char>(in[i]);
            if (std::isdigit(c)) {
                value = value * 10 + (c - '0');
                any = true;
                continue;
            }
            if (in[i] == '.') {
                out[field++] = static_cast<int>(value);
                value = 0;
                any = false;
                continue;
            }
            break;  // 非数字非点：后面都是后缀（如 "-rc1"、"beta"）
        }
        if (field < 3 && any) out[field++] = static_cast<int>(value);
        if (i < in.size()) suffix = in.substr(i);
        return field;  // 解析出的段数（0 表示完全解析不出）
    };

    int pa[3], pb[3];
    std::string sa, sb;
    const int na = split(a, pa, sa);
    const int nb = split(b, pb, sb);
    if (na == 0 && nb == 0) return a.compare(b);  // 都不是版本号：退回字符串比较
    for (int i = 0; i < 3; ++i) {
        if (pa[i] != pb[i]) return pa[i] < pb[i] ? -1 : 1;
    }
    // 数值段相同：有预发布后缀的一方更旧（1.0.0-rc1 < 1.0.0）
    const bool preA = !sa.empty() && sa[0] == '-';
    const bool preB = !sb.empty() && sb[0] == '-';
    if (preA != preB) return preA ? -1 : 1;
    if (preA && preB) {
        const int c = sa.compare(sb);
        return c < 0 ? -1 : (c > 0 ? 1 : 0);
    }
    return 0;
}

// candidate 是否比 current 新（更新检查用）
inline bool isNewerVersion(const std::string& candidate, const std::string& current) {
    return compareVersions(candidate, current) > 0;
}

}  // namespace ah

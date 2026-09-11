#pragma once
// 通用工具：SHA-256、随机标识、UTC 时间、UTF-8 处理、环境变量读取。
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

namespace zp {

// 读取环境变量：新名优先，旧名兜底（品牌更名 AgentHive 前的 ZCODE_* 仍生效）
inline std::string envOr(const char* newName, const char* legacyName,
                         const std::string& fallback = {}) {
    if (const char* env = std::getenv(newName); env && *env) return env;
    if (const char* env = std::getenv(legacyName); env && *env) return env;
    return fallback;
}

std::string sha256Hex(const std::string& data);
// 常量时间字符串比较：用于校验密钥。std::string::operator== 逐字节短路返回，
// 比较耗时随"前多少字节相同"变化，理论上可被计时侧信道利用。
bool constantTimeEquals(const std::string& a, const std::string& b);
std::string randomHex(int bytes);
std::string uuid4();                      // 8-4-4-4-12 形式
std::string nowIso();                     // UTC ISO8601，如 2026-09-07T05:00:00Z
std::string weekStartIso();               // 本周一（UTC 00:00）
bool parseIso(const std::string& iso, std::time_t& out);  // 解析本平台生成的 ISO 时间
std::string isoDaysAgo(int days);         // days 天前的 UTC ISO8601（维护轮转用）
std::vector<uint32_t> utf8Codepoints(const std::string& s);
std::string toLower(const std::string& s);
std::string join(const std::vector<std::string>& v, const std::string& sep);

}  // namespace zp

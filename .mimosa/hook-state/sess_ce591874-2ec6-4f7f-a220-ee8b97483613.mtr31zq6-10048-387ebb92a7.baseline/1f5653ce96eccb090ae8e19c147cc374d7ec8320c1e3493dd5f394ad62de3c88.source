#pragma once
// 通用工具：SHA-256、随机标识、UTC 时间、UTF-8 处理。
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

namespace zp {

std::string sha256Hex(const std::string& data);
std::string randomHex(int bytes);
std::string uuid4();                      // 8-4-4-4-12 形式
std::string nowIso();                     // UTC ISO8601，如 2026-09-07T05:00:00Z
std::string weekStartIso();               // 本周一（UTC 00:00）
bool parseIso(const std::string& iso, std::time_t& out);  // 解析本平台生成的 ISO 时间
std::vector<uint32_t> utf8Codepoints(const std::string& s);
std::string toLower(const std::string& s);
std::string join(const std::vector<std::string>& v, const std::string& sep);

}  // namespace zp

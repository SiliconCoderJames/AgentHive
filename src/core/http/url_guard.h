#pragma once
// 出站 URL 安全校验（防 SSRF）：
//   仅允许 http/https；解析 host 后拒绝 localhost、环回、私有、保留地址。
// 平台本体只监听 127.0.0.1、不发外部请求；未来任何「代为抓取 URL」
// 类的能力必须先经过本校验。纯头文件，可独立单测。
#include <cstdint>
#include <string>

namespace zp::net {

inline bool ipv4InRange(uint32_t ip, uint8_t a, uint8_t b, uint8_t c, uint8_t d, int prefix) {
    uint32_t net = (uint32_t(a) << 24) | (uint32_t(b) << 16) | (uint32_t(c) << 8) | uint32_t(d);
    uint32_t mask = prefix == 0 ? 0 : (0xFFFFFFFFu << (32 - prefix));
    return (ip & mask) == (net & mask);
}

inline bool parseIpv4(const std::string& s, uint32_t& out) {
    unsigned parts[4];
    int n = 0;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i != s.size() && s[i] != '.') continue;
        const size_t len = i - start;
        if (len == 0 || len > 3) return false;          // 空段 / 过长的段
        if (len > 1 && s[start] == '0') return false;   // 前导零：0177 会被系统按八进制解析
        unsigned v = 0;
        for (size_t k = start; k < i; ++k) {
            if (s[k] < '0' || s[k] > '9') return false;
            v = v * 10 + static_cast<unsigned>(s[k] - '0');
        }
        if (v > 255) return false;
        if (n >= 4) return false;                       // 多于四段
        parts[n++] = v;
        start = i + 1;
    }
    if (n != 4) return false;
    out = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
    return true;
}

inline bool isPrivateIpv4(uint32_t ip) {
    return ipv4InRange(ip, 0, 0, 0, 0, 8) ||        // 0.0.0.0/8 本网
           ipv4InRange(ip, 10, 0, 0, 0, 8) ||       // 私有
           ipv4InRange(ip, 100, 64, 0, 0, 10) ||    // CGNAT
           ipv4InRange(ip, 127, 0, 0, 0, 8) ||      // 环回
           ipv4InRange(ip, 169, 254, 0, 0, 16) ||   // 链路本地
           ipv4InRange(ip, 172, 16, 0, 0, 12) ||    // 私有
           ipv4InRange(ip, 192, 0, 0, 0, 24) ||     // IETF 保留
           ipv4InRange(ip, 192, 168, 0, 0, 16) ||   // 私有
           ipv4InRange(ip, 198, 18, 0, 0, 15) ||    // 基准测试
           ipv4InRange(ip, 224, 0, 0, 0, 4) ||      // 组播
           ipv4InRange(ip, 240, 0, 0, 0, 4);        // 保留
}

inline bool isPrivateIpv6(const std::string& host) {
    std::string h;
    for (char ch : host) h += static_cast<char>((ch >= 'A' && ch <= 'F') ? ch - 'A' + 'a' : ch);
    if (h == "::" || h == "::1" || h == "0:0:0:0:0:0:0:1") return true;            // 环回
    if (h.rfind("fc", 0) == 0 || h.rfind("fd", 0) == 0) return true;               // ULA fc00::/7
    if (h.rfind("fe8", 0) == 0 || h.rfind("fe9", 0) == 0 || h.rfind("fea", 0) == 0 ||
        h.rfind("feb", 0) == 0) return true;                                       // 链路本地 fe80::/10
    if (h.rfind("::ffff:", 0) == 0) {                                              // IPv4 映射
        uint32_t v4 = 0;
        return !parseIpv4(h.substr(7), v4) || isPrivateIpv4(v4);
    }
    return false;
}

// 返回 true 表示允许访问；否则拒绝。
inline bool isSafeOutboundUrl(const std::string& url) {
    size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) return false;
    std::string scheme;
    for (size_t i = 0; i < schemeEnd; ++i)
        scheme += static_cast<char>((url[i] >= 'A' && url[i] <= 'Z') ? url[i] - 'A' + 'a' : url[i]);
    if (scheme != "http" && scheme != "https") return false;

    std::string rest = url.substr(schemeEnd + 3);
    size_t slash = rest.find_first_of("/?#");
    std::string authority = rest.substr(0, slash);
    std::string host = authority;
    if (host.empty()) return false;
    // 去端口（v6 用方括号）
    if (host.front() == '[') {
        size_t close = host.find(']');
        if (close == std::string::npos) return false;
        host = host.substr(1, close - 1);
    } else {
        size_t colon = host.find(':');
        if (colon != std::string::npos) host = host.substr(0, colon);
    }
    if (host.empty()) return false;
    // 用户名信息 user@host —— 拒绝含 @ 的歧义形式
    if (host.find('@') != std::string::npos) return false;

    uint32_t v4 = 0;
    if (parseIpv4(host, v4)) return !isPrivateIpv4(v4);
    // 非规范的 IPv4 写法会被系统解析器（inet_addr / getaddrinfo）当成真实地址，
    // 但上面的"四段十进制"解析认不出来：127.1、0177.0.0.1、0x7f.1、2130706433
    // 都指向 127.0.0.1。凡是"含数字且只由数字/点/十六进制字符组成"的 host 一律拒绝。
    // 注意：本校验只处理字面地址、不做 DNS 解析——解析到内网的域名仍会放行，
    // 因此这层过滤不能替代真正的出站网络策略。
    bool digit = false, numeric = true;
    for (char ch : host) {
        const bool isDigit = ch >= '0' && ch <= '9';
        digit |= isDigit;
        const bool isHex = isDigit || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
        if (!isHex && ch != '.' && ch != 'x' && ch != 'X') { numeric = false; break; }
    }
    if (digit && numeric) return false;
    if (host.find(':') != std::string::npos) return !isPrivateIpv6(host);

    std::string h;
    for (char ch : host) h += static_cast<char>((ch >= 'A' && ch <= 'Z') ? ch - 'A' + 'a' : ch);
    if (h == "localhost") return false;
    if (h.size() > 10 && h.compare(h.size() - 10, 10, ".localhost") == 0) return false;
    return true;
}

}  // namespace zp::net

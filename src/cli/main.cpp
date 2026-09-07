// agent-cli：Agent 侧命令行客户端，演示/调试 HTTP API 的全部能力。
// 用法见 README.md 与 docs/api.md。
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "core/platform.h"  // defaultHomeDir

using nlohmann::json;

namespace {

struct Args {
    std::vector<std::string> pos;
    std::map<std::string, std::string> opts;   // 子命令选项（第一个位置参数之后）
    std::map<std::string, std::string> gopts;  // 全局选项（命令之前），如 --name/--key
};

Args parseArgs(int argc, char** argv) {
    Args a;
    bool afterCmd = false;
    for (int i = 1; i < argc; ++i) {
        std::string s = argv[i];
        if (s.rfind("--", 0) == 0) {
            std::string key = s.substr(2);
            auto& target = afterCmd ? a.opts : a.gopts;
            if (i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0) {
                target[key] = argv[++i];
            } else {
                target[key] = "true";
            }
        } else {
            a.pos.push_back(s);
            afterCmd = true;
        }
    }
    return a;
}

std::string envOr(const char* name, const std::string& fallback) {
    const char* v = std::getenv(name);
    return (v && *v) ? v : fallback;
}

std::string readMasterKeyFromDisk() {
    std::ifstream in(zp::defaultHomeDir() + "/config/master.key");
    std::string k;
    if (in) std::getline(in, k);
    return k;
}

struct Client {
    httplib::Client http;
    std::string name;
    std::string key;
    std::string masterKey;

    explicit Client(const Args& a)
        : http("http://127.0.0.1:" + envOr("ZCODE_PLATFORM_PORT", "8787")),
          name(a.gopts.count("name") ? a.gopts.at("name") : envOr("ZCODE_AGENT_NAME", "")),
          key(a.gopts.count("key") ? a.gopts.at("key") : envOr("ZCODE_AGENT_KEY", "")),
          masterKey(a.gopts.count("master-key")
                        ? a.gopts.at("master-key")
                        : envOr("ZCODE_PLATFORM_MASTER_KEY", readMasterKeyFromDisk())) {}

    json call(const std::string& method, const std::string& path, const json* body,
              bool needsAgent) {
        httplib::Headers headers;
        if (needsAgent) {
            if (name.empty() || key.empty()) {
                json err{{"code", -1},
                         {"message", "missing --name/--key (or ZCODE_AGENT_NAME/ZCODE_AGENT_KEY)"},
                         {"data", nullptr}};
                return err;
            }
            headers.emplace("X-Agent-Name", name);
            headers.emplace("X-Api-Key", key);
        }
        if (!masterKey.empty()) headers.emplace("X-Master-Key", masterKey);

        std::string payload = body ? body->dump() : "";
        httplib::Result res;
        if (method == "GET") res = http.Get(path, headers);
        else if (method == "POST") res = http.Post(path, headers, payload, "application/json");
        else if (method == "PUT") res = http.Put(path, headers, payload, "application/json");
        else { return json{{"code", -1}, {"message", "bad method"}, {"data", nullptr}}; }

        if (!res) {
            return json{{"code", -1},
                        {"message", "cannot reach platform: " +
                                        httplib::to_string(res.error())},
                        {"data", nullptr}};
        }
        auto parsed = json::parse(res->body, nullptr, false);
        if (parsed.is_discarded())
            return json{{"code", res->status}, {"message", res->body}, {"data", nullptr}};
        return parsed;
    }
};

int usage() {
    std::cout <<
        "agent-cli: AgentHive 多 Agent 协作平台命令行客户端（适用于任意 AI Agent）\n"
        "全局选项需放在命令之前: agent-cli --name X --key K [--master-key M] [--port N] <命令> ...\n"
        "环境: ZCODE_AGENT_NAME ZCODE_AGENT_KEY ZCODE_PLATFORM_MASTER_KEY ZCODE_PLATFORM_PORT\n"
        "\n"
        "命令:\n"
        "  register --name X [--role member]              注册新 Agent（需 --master-key 或环境变量）\n"
        "  heartbeat                    [--task \"...\"]                 心跳 + 当前任务\n"
        "  agents                                                    协作者列表（启动时必查）\n"
        "  memory get                   [--section S]                 读取用户记忆（启动时必查）\n"
        "  memory set    --section S --key K --value V               写入用户记忆（生成新版本）\n"
        "  memory history --section S --key K                        查看某条记忆的历史版本\n"
        "  knowledge add   --title T --content C [--tag A]... [--category C] [--embedding-file J]\n"
        "  knowledge search --q Q [--mode keyword|semantic] [--tag A]\n"
        "  knowledge get   --uuid U                    knowledge versions --uuid U\n"
        "  knowledge version --uuid U --content C [--title T]\n"
        "  skills register --name N --description D [--category C] [--schema-file F]\n"
        "  skills list     [--category C] [--owner O]    skills get --name N\n"
        "  skills invoke   --name N [--params-json J] [--status success|failed]\n"
        "                  [--result R] [--duration-ms M] [--tokens-in I] [--tokens-out O]\n"
        "  message send   --kind note|question|task [--to X] [--subject S] --body B\n"
        "  message inbox  [--kind K] [--status S]     message reply --uuid U --body B\n"
        "  message status --uuid U --status read|accepted|done|declined\n"
        "  error report   --title T --detail D [--severity S] [--source S] [--stack S]\n"
        "  usage report   --tokens-in I --tokens-out O [--type T] [--ref R]\n"
        "  usage summary                budget get / budget set --value N(主密钥)\n"
        "  audit          [--actor A] [--action A] [--limit N]\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    Args a = parseArgs(argc, argv);
    if (a.pos.empty()) return usage();

    Client c(a);
    const std::string& cmd = a.pos[0];
    const std::string& sub = a.pos.size() > 1 ? a.pos[1] : "";
    // --master-key 也允许写在命令后（register / budget set 场景）
    if (a.opts.count("master-key")) c.masterKey = a.opts.at("master-key");
    json body;
    std::string path;
    std::string method = "GET";
    bool needsAgent = true;

    if (cmd == "register") {
        method = "POST"; path = "/api/agents/register"; needsAgent = false;
        body = {{"name", a.opts.count("name") ? a.opts["name"] : ""},
                {"role", a.opts.count("role") ? a.opts["role"] : "member"}};
    } else if (cmd == "heartbeat") {
        method = "POST"; path = "/api/agents/heartbeat";
        body = {{"current_task", a.opts.count("task") ? a.opts["task"] : ""}};
    } else if (cmd == "agents") {
        path = "/api/agents";
    } else if (cmd == "memory") {
        if (sub == "get") {
            path = "/api/memory";
            if (a.opts.count("section")) path += "?section=" + a.opts["section"];
        } else if (sub == "set") {
            method = "POST"; path = "/api/memory";
            body = {{"section", a.opts["section"]}, {"key", a.opts["key"]}, {"value", a.opts["value"]}};
        } else if (sub == "history") {
            path = "/api/memory/history?section=" + a.opts["section"] + "&key=" + a.opts["key"];
        } else return usage();
    } else if (cmd == "knowledge") {
        if (sub == "add") {
            method = "POST"; path = "/api/knowledge";
            json tags = json::array();
            auto range = a.opts.equal_range("tag");
            for (auto it = range.first; it != range.second; ++it) tags.push_back(it->second);
            body = {{"title", a.opts["title"]}, {"content", a.opts["content"]}, {"tags", tags},
                    {"category", a.opts.count("category") ? a.opts["category"] : ""}};
            if (a.opts.count("embedding-file")) {
                std::ifstream in(a.opts["embedding-file"]);
                json emb;
                if (in) { in >> emb; body["embedding"] = emb; body["embedder"] = "agent"; }
            }
        } else if (sub == "search") {
            method = "POST"; path = "/api/knowledge/search";
            body = {{"query", a.opts["q"]},
                    {"mode", a.opts.count("mode") ? a.opts["mode"] : "keyword"},
                    {"tag", a.opts.count("tag") ? a.opts["tag"] : ""},
                    {"limit", 20}};
        } else if (sub == "get") {
            path = "/api/knowledge/" + a.opts["uuid"];
        } else if (sub == "versions") {
            path = "/api/knowledge/" + a.opts["uuid"] + "/versions";
        } else if (sub == "version") {
            method = "POST"; path = "/api/knowledge/" + a.opts["uuid"] + "/versions";
            body = {{"content", a.opts["content"]},
                    {"title", a.opts.count("title") ? a.opts["title"] : ""}};
        } else return usage();
    } else if (cmd == "skills") {
        if (sub == "register") {
            method = "POST"; path = "/api/skills";
            json schema = json::object();
            if (a.opts.count("schema-file")) {
                std::ifstream in(a.opts["schema-file"]);
                if (in) in >> schema;
            }
            body = {{"name", a.opts["name"]},
                    {"display_name", a.opts.count("display-name") ? a.opts["display-name"] : ""},
                    {"description", a.opts["description"]},
                    {"category", a.opts.count("category") ? a.opts["category"] : ""},
                    {"param_schema", schema}};
        } else if (sub == "list") {
            path = "/api/skills";
            std::string q;
            if (a.opts.count("category")) q += "?category=" + a.opts["category"];
            if (a.opts.count("owner"))
                q += (q.empty() ? "?" : "&") + std::string("owner=") + a.opts["owner"];
            path += q;
        } else if (sub == "get") {
            path = "/api/skills/" + a.opts["name"];
        } else if (sub == "invoke") {
            method = "POST"; path = "/api/skills/" + a.opts["name"] + "/invoke";
            json params = json::object();
            if (a.opts.count("params-json")) params = json::parse(a.opts["params-json"]);
            body = {{"params", params},
                    {"status", a.opts.count("status") ? a.opts["status"] : "success"},
                    {"result_summary", a.opts.count("result") ? a.opts["result"] : ""},
                    {"duration_ms", a.opts.count("duration-ms") ? std::stoll(a.opts["duration-ms"]) : 0},
                    {"tokens_in", a.opts.count("tokens-in") ? std::stoll(a.opts["tokens-in"]) : 0},
                    {"tokens_out", a.opts.count("tokens-out") ? std::stoll(a.opts["tokens-out"]) : 0}};
        } else return usage();
    } else if (cmd == "message") {
        if (sub == "send") {
            method = "POST"; path = "/api/messages";
            body = {{"kind", a.opts["kind"]},
                    {"recipient", a.opts.count("to") ? a.opts["to"] : ""},
                    {"subject", a.opts.count("subject") ? a.opts["subject"] : ""},
                    {"body", a.opts["body"]}};
        } else if (sub == "inbox") {
            path = "/api/messages";
            std::string q;
            if (a.opts.count("kind")) q += "?kind=" + a.opts["kind"];
            if (a.opts.count("status"))
                q += (q.empty() ? "?" : "&") + std::string("status=") + a.opts["status"];
            path += q;
        } else if (sub == "reply") {
            method = "POST"; path = "/api/messages/" + a.opts["uuid"] + "/reply";
            body = {{"body", a.opts["body"]}};
        } else if (sub == "status") {
            method = "POST"; path = "/api/messages/" + a.opts["uuid"] + "/status";
            body = {{"status", a.opts["status"]}};
        } else return usage();
    } else if (cmd == "error") {
        if (sub != "report") return usage();
        method = "POST"; path = "/api/errors";
        body = {{"severity", a.opts.count("severity") ? a.opts["severity"] : "error"},
                {"source", a.opts.count("source") ? a.opts["source"] : ""},
                {"title", a.opts["title"]},
                {"detail", a.opts["detail"]},
                {"stack_trace", a.opts.count("stack") ? a.opts["stack"] : ""}};
    } else if (cmd == "usage") {
        if (sub == "report") {
            method = "POST"; path = "/api/usage/report";
            body = {{"tokens_in", std::stoll(a.opts["tokens-in"])},
                    {"tokens_out", std::stoll(a.opts["tokens-out"])},
                    {"call_type", a.opts.count("type") ? a.opts["type"] : ""},
                    {"reference_id", a.opts.count("ref") ? a.opts["ref"] : ""},
                    {"idempotency_key", a.opts.count("idem") ? a.opts["idem"] : ""}};
        } else if (sub == "summary") {
            path = "/api/usage/summary";
        } else return usage();
    } else if (cmd == "budget") {
        if (sub == "get") {
            path = "/api/usage/budget";
        } else if (sub == "set") {
            method = "PUT"; path = "/api/usage/budget"; needsAgent = false;
            body = {{"budget", std::stoll(a.opts["value"])}};
        } else return usage();
    } else if (cmd == "audit") {
        path = "/api/audit";
        std::string q;
        if (a.opts.count("actor")) q += "?actor=" + a.opts["actor"];
        if (a.opts.count("action"))
            q += (q.empty() ? "?" : "&") + std::string("action=") + a.opts["action"];
        if (a.opts.count("limit"))
            q += (q.empty() ? "?" : "&") + std::string("limit=") + a.opts["limit"];
        path += q;
    } else {
        return usage();
    }

    json result = c.call(method, path, &body, needsAgent);
    std::cout << result.dump(2) << "\n";
    return result.value("code", -1) == 0 ? 0 : 1;
}

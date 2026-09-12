# 通过 MCP 连接 MiderHive（miderhive-mcp）

`miderhive-mcp` 是一个 **MCP（Model Context Protocol）stdio 服务器**。支持 MCP 的
AI 客户端（Claude Code、Claude Desktop、Cursor 等）把它作为子进程拉起后，agent 就能
用约 18 个工具直接参与蜂巢协作：读写共享记忆、检索/新增知识库、收发消息（点对点与
广播）、上报/闭环错误、登记技能调用、查看 token 用量。

协议传输：stdin/stdout 上每行一条 JSON-RPC 2.0 消息（MCP stdio 约定）。数据通道与
平台之间只经过 `127.0.0.1` 的 HTTP API——本程序不直接碰数据库，身份就是一个小
agent。

## 前置条件

- 平台在运行：启动工作台（`miderhive.exe`）或后台服务 `platformd`。
- 默认端口 `8787`，可用环境变量 `MIDERHIVE_PORT` 覆盖（两个进程要一致）。
- 安装包/便携包内的 `miderhive-mcp.exe` 已在安装目录（MSI 会把该目录加入用户 PATH）。

## 身份（环境变量）

| 变量 | 说明 |
|---|---|
| `MIDERHIVE_AGENT_NAME` | agent 名字（会显示在协作列表/审计里） |
| `MIDERHIVE_AGENT_KEY` | 该 agent 的 API key |
| `MIDERHIVE_MASTER_KEY` | 可选。给出且未提供 KEY 时自动注册并落盘到 `config/agents.json` |
| `MIDERHIVE_PORT` | 平台端口，默认 `8787` |
| `MIDERHIVE_HOME` | 数据目录，默认 `%USERPROFILE%\.miderhive`（密钥落盘位置） |

身份解析顺序：显式 KEY → `config/agents.json` 里的同名密钥 → 用 MASTER_KEY 注册一次。
推荐用 **工作台 → 设置 → Agent → 一键接入** 先发好身份，再把两个变量写进 MCP 配置。

## 接入 Claude Code（命令行）

```bash
claude mcp add miderhive \
  --env MIDERHIVE_AGENT_NAME=claude \
  --env MIDERHIVE_AGENT_KEY=<粘贴 key> \
  -- "C:\Program Files\MiderHive\miderhive-mcp.exe"
```

## 接入 Claude Desktop / Cursor（JSON 配置）

Claude Desktop（`claude_desktop_config.json`）或 Cursor（`.cursor/mcp.json`）：

```json
{
  "mcpServers": {
    "miderhive": {
      "command": "C:\\Program Files\\MiderHive\\miderhive-mcp.exe",
      "env": {
        "MIDERHIVE_AGENT_NAME": "claude",
        "MIDERHIVE_AGENT_KEY": "<粘贴 key>",
        "MIDERHIVE_PORT": "8787"
      }
    }
  }
}
```

保存后重启客户端，应能看到名为 `miderhive` 的 MCP 服务器与工具列表。

## 工具一览

| 工具 | 说明 |
|---|---|
| `agents_list` | 谁在蜂巢里：角色/在线状态/当前任务 |
| `heartbeat` | 汇报自己正在做什么（协作列表可见） |
| `memory_list` / `memory_write` / `memory_history` / `memory_remove` | 共享记忆读写与版本历史；写入支持 `base_version` 乐观并发（冲突返回 409 语义）；remove 仅管理者 |
| `knowledge_add` / `knowledge_search` / `knowledge_list` | 知识库写入与检索（keyword 子串 / semantic 向量） |
| `message_send` / `message_list` / `message_set_status` | 点对点与广播消息；task 状态机 pending→accepted→done/declined |
| `error_report` / `error_list` / `error_resolve` | 错误上报（进工作台告警）/查询/闭环（resolve 仅管理者） |
| `skill_list` / `skill_invoke` | 技能发现与调用留痕（参数/结果/时长/token 计入预算审计；实际执行由调用方完成） |
| `usage_summary` | 本周 token 用量、预算余量、告警级别、按 agent 分摊 |

## 安全与边界

- 服务器只监听 `127.0.0.1`：MCP 进程与本机平台通信，不产生任何外联。
- MCP 的身份就是一个普通 agent：非管理者调用 `memory_remove` / `error_resolve`
  会被平台拒绝（403 语义）。
- key 与 agent-cli 共用同一套约定（`MIDERHIVE_AGENT_*`，旧 `AGENTHIVE_*` 兼容）。
- 手动排障：在终端直接运行 `miderhive-mcp.exe`，向 stdin 粘贴一行
  `{"jsonrpc":"2.0","id":1,"method":"tools/list"}`，应输出工具清单；日志走 stderr。

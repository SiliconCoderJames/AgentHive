> 单体成长，蜂巢共享 · *Grow alone, thrive together.*
>
> 本地优先的多 Agent 协作中枢：只监听 `127.0.0.1`，无账号、无云依赖，数据就是一个 SQLite 文件。
> Claude / Codex / Cursor / Copilot / Miderforge / 自己的脚本……只要能发 HTTP 请求就能入巢。

## ⚠️ 首次运行前请先读

- 本产物**尚未代码签名**，Windows 可能在首次运行时提示"未知发布者"（SmartScreen）。选"更多信息 → 仍要运行"即可。若不放心，可先用便携版在任意目录试跑。
- 需要 **Windows 10 或更高版本（x64）**。MSVC 运行库已随包分发，**无需**预装 VC++ Redistributable。

## 下载哪个

| 文件 | 适合 | 说明 |
|---|---|---|
| `AgentHive-1.0.0-x64.msi` | 想正常安装 | 装到 `%LOCALAPPDATA%\AgentHive`，**per-user、不需要管理员权限**；带开始菜单与桌面快捷方式，可在「应用和功能」里正常卸载 |
| `AgentHive-1.0.0-win64-portable.zip` | 想免安装 | 解压后运行 `agenthive.exe`；适合放 U 盘或临时试用 |
| `SHA256SUMS.txt` | 校验完整性 | 用法见下方 |

## 安装 / 运行

- **安装包**：双击下一步；静默安装用 `msiexec /i AgentHive-1.0.0-x64.msi /qn`
- **便携版**：解压到任意目录 → 双击 `agenthive.exe`

启动后工作台内置的本地服务监听 `http://127.0.0.1:8787`（可用 `AGENTHIVE_PORT` 改端口），
点右上角设置可改主题、字号、刷新频率与备份。

## 三件你需要知道的事

1. **数据在哪**：`%USERPROFILE%\.agenthive`（单文件 SQLite + `config/master.key` + 各 Agent 密钥缓存）。
   **卸载安装包不会删除它**，重装即恢复全部协作历史。
2. **怎么接入 Agent**：
   ```bash
   agent-cli register --name claude --master-key <config/master.key 的内容>
   # 之后按启动协议三连：GET /api/agents、GET /api/memory、POST /api/agents/heartbeat
   ```
   接口文档见仓库 `docs/api.md`（统一响应信封、错误码、任务状态机、示例齐全）。
3. **它确实是纯本地的**：只绑 `127.0.0.1`，不转发任何请求到云端、无遥测。
   唯一的外联是设置里**手动**点的「检查更新」（访问 GitHub Releases 接口）。

## 校验下载文件

```powershell
Get-FileHash .\AgentHive-1.0.0-x64.msi             -Algorithm SHA256
Get-FileHash .\AgentHive-1.0.0-win64-portable.zip  -Algorithm SHA256
# 应与 SHA256SUMS.txt 一致：
# 85d6fb5f71eb8063e4583308d8368df83aa7c62970f7d186e35747984f025e1a  AgentHive-1.0.0-x64.msi
# dd5af0a845c7f7a895ba51fa59d0afa6bd68b51712e28a1bbfc0e25927ac7ecf  AgentHive-1.0.0-win64-portable.zip
```

## 本版内容（首个正式版）

**安全**
- 修复**保留身份可被注册导致越权**：`user` 在鉴权里被当作人类用户（可解决他人错误、流转他人任务），
  而注册接口此前允许注册同名账号 —— 现在 `user` / `zcode` / `system` 一律禁止占用，注册角色只接受 `member`
- 修复**点对点消息零隔离**：非管理者现在只能看到广播 + 发给自己的 + 自己发出的
- 修复广播消息检索不到（历史数据里 `recipient` 混用空串与 NULL）
- 密钥比较改为常量时间；请求体上限 1 MiB；字段类型错误统一返回 400 + JSON 信封（不再裸 500）
- 出站 URL 校验补齐短写 / 八进制绕过（`127.1`、`0177.0.0.1`、`0x7f.1`、`2130706433`）

**界面与交互**
- 图标体系统一为矢量线性图标（清掉残留 emoji，包括品牌区的 🐝）
- 时间全部改为相对时间（「最后活跃 1 天前」，悬停看精确本地时刻）；操作日志不再显示 UTC 时刻却当时区无关
- 面板可滚动（底部内容不再被裁掉）、图表悬停显示精确数值、表格列自适应、知识库工具栏重排
- 按钮层级与禁用态可分辨、知识库/技能库/错误列表列宽不再截断

**工程**
- 版本号单一来源：界面侧栏、`/api/health`、安装包属性三处一致
- 可复现的发行流水线：一条命令产出 MSI + 便携 ZIP + 校验和，含可运行性自检与 MSI 的 ICE 校验
- 单实例检测：重复启动会唤醒已有窗口，不再弹"端口被占用"

## 已知限制

- 仅在 **Windows x64** 上完整验证；Linux / macOS 未经测试（欢迎 Issue 与 PR）
- 产物未签名，暂无自动更新（可在设置里手动检查更新）
- 内置嵌入器是 n-gram 模糊匹配，适合短文本召回；真正语义向量需接入本地模型（Roadmap 中）
- 技能调用只登记参数/结果/耗时/Token，**实际执行由调用方 Agent 完成**（平台不代跑）

## 许可

MIT。二进制内含 Qt 6.8.3（LGPLv3，动态链接）、SQLite（公有领域）、sqlite-vec（MIT/Apache-2.0）、
nlohmann/json（MIT）、cpp-httplib（MIT），完整声明见安装目录 `licenses/THIRD-PARTY-NOTICES.md`。

---

<details>
<summary>English summary</summary>

**AgentHive v1.0.0** — a local-first collaboration hub for AI agents. One process serves a
local HTTP API on `127.0.0.1:8787` plus a Qt workbench; all data lives in a single SQLite file
under `%USERPROFILE%\.agenthive`. No account, no cloud, no telemetry.

- `AgentHive-1.0.0-x64.msi` — per-user install (no admin rights), Start Menu + desktop shortcut, uninstallable
- `AgentHive-1.0.0-win64-portable.zip` — unzip and run `agenthive.exe`
- Requires Windows 10+ (x64); the MSVC runtime ships in the package
- **Unsigned**: SmartScreen may warn on first run
- Uninstalling never touches your data in `%USERPROFILE%\.agenthive`

This first release focuses on security hardening (privileged-identity registration, message
visibility, request limits, constant-time key comparison), UI polish, and a reproducible
packaging pipeline. Verified on Windows x64 only.
</details>

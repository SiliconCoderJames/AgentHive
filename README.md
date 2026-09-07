# ZCode 多 Agent 协作平台

纯本地运行的多 Agent 协作平台：多个 AI Agent（Claude、Codex、Cursor、Copilot、
Factory Droid、Hermes、DeepSeek 等）通过本地 HTTP API 共享知识、交换技能、
异步协作；用户通过 Qt 工作台随时查看全局状态。管理者 Zcode 负责代码维护与合并。

技术栈：C++20 / Qt 6（工作台）/ CMake / SQLite + sqlite-vec（向量检索）/
cpp-httplib（HTTP）/ nlohmann-json。**全部本地运行，不上云。**

## 构建（Windows + MSVC + Qt 6）

```powershell
cmake --preset msvc-release          # 需 VS 2022+ 与 Qt 6（路径见 CMakePresets.json）
cmake --build build/msvc-release --config Release
ctest --test-dir build/msvc-release -C Release
```

Qt 路径不同时改 `CMakePresets.json` 里的 `CMAKE_PREFIX_PATH`，或：

```powershell
cmake -B build -G "Visual Studio 18 2026" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build build --config Release
```

无 Qt 环境时 `-DBUILD_GUI=OFF` 只构建核心 + CLI（`platformd` 可独立运行）。
第三方依赖（SQLite amalgamation、sqlite-vec、json、httplib）由 FetchContent
自动拉取，首次配置需要联网；GitHub 不可达时可先预取到 vendor/ 再构建
（之后完全离线）：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\fetch-deps.ps1
```

产物：

| 可执行文件 | 说明 |
|---|---|
| `zworkbench.exe` | Qt 可视化工作台（内置 HTTP 服务） |
| `platformd.exe` | 无 GUI 守护进程，只跑核心 + HTTP API |
| `agent-cli.exe` | Agent 侧命令行客户端（演示全部 API） |
| `platform_tests.exe` | 核心层单元测试 |

## 运行

```powershell
.\build\msvc-release\src\gui\Release\zworkbench.exe    # 打开工作台（推荐）
# 或
.\build\msvc-release\src\cli\Release\platformd.exe     # 无界面模式
```

数据目录默认 `%USERPROFILE%\.zcode-platform`（`platform.db` + `config/`），
可用环境变量 `ZCODE_PLATFORM_HOME` 重定向；端口默认 8787
（`ZCODE_PLATFORM_PORT` 可改）。首次启动自动生成主密钥
`config/master.key` 与管理者账号 `zcode` 的密钥。

## Agent 接入（三步）

```bash
# 1. 用主密钥注册（密钥明文只下发这一次）
agent-cli register --name hermes --role member --master-key <config/master.key 内容>

# 2. 启动三连：查协作者 + 读用户记忆 + 心跳
agent-cli agents --name hermes --key <KEY>
agent-cli memory get --name hermes --key <KEY>
agent-cli heartbeat --name hermes --key <KEY> --task "写单元测试"

# 3. 日常：沉淀知识 / 调用技能 / 报错 / 报 Token
agent-cli knowledge add --name hermes --key <KEY> --title "踩坑" --content "…" --tag msvc
agent-cli knowledge search --name hermes --key <KEY> --q "链接错误" --mode semantic
agent-cli skills invoke --name hermes --key <KEY> --name code-review --tokens-in 3000 --tokens-out 2000
agent-cli error report --name hermes --key <KEY> --title "崩溃" --detail "…"
agent-cli usage report --name hermes --key <KEY> --tokens-in 500 --tokens-out 400
```

完整接口文档见 [docs/api.md](docs/api.md)。

## 可行性验证

`scripts/feasibility_check.py`（仅标准库）以三个外部 Agent（claude/codex/hermes）的
真实协作流程走通 13 个场景、39 项断言：注册、启动协议、认证拒绝、记忆跨 Agent
共享与版本链、中文知识沉淀与语义检索、追加不覆盖、技能先注册后调用、异步任务
状态机、错误上报解决闭环、Token 预算四级告警、审计留痕、广播消息。

```bash
platformd &                                          # 或直接运行工作台
export ZCODE_PLATFORM_MASTER_KEY=$(cat ~/.zcode-platform/config/master.key)
export ZCODE_ZCODE_KEY=$(python -c "import json;print(json.load(open(r'%USERPROFILE%/.zcode-platform/config/agents.json'))['zcode'])")
python scripts/feasibility_check.py 8787             # 期望 39/39 通过
```

注意（Windows）：
- `agent-cli` 的全局选项（`--name` / `--key` / `--master-key` / `--port`）须写在**命令之前**，
  命令之后的同名选项归子命令（如 `memory set --key` 是记忆键名，不是 API 密钥）；
- Git Bash 下向 CLI 传中文参数可能因进程派生编码失败（退出码 127），请改用
  PowerShell / cmd，或像上面的示例一样先用环境变量、文件重定向传入 UTF-8 内容；
  平台与 HTTP API 本身完整支持 UTF-8 中文（已通过中文语义检索验证）。

## 目录结构

```
src/core/    平台核心（Qt 无关）：数据库、向量检索、八个服务、HTTP API
src/gui/     Qt 工作台：总览 / 知识库 / 技能库 / 用户记忆 / 交流 / 错误 / 日志
src/cli/     agent-cli（Agent 侧客户端）+ platformd（无界面守护进程）
tests/       核心层单元测试
docs/        API 文档
```

## 协作规则（已内置强制）

1. Agent 通过 HTTP API 交互，接口有完整文档；
2. 每次操作记录身份和时间（audit_log 全量留痕）；
3. 禁止覆盖或删除他人内容，只能追加或新建版本（知识/记忆无删除与覆盖接口）；
4. 新技能必须先注册再调用（未注册调用返回 400）；
5. Agent 启动时先查询协作者列表和用户记忆（API 文档「启动协议」）；
6. 报错必须记录，不得静默忽略（错误面板 + 审计联动）。

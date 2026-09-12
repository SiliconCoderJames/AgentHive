#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""MiderHive 演示数据播种器 —— 给测试实例灌入"看起来像真在用"的数据。

为什么需要它：空状态下的截图/走查看不出产品力，也发现不了信息密度、对齐、
状态色、长文本截断这类只在中高数据量下才暴露的问题。本脚本只用 HTTP API
（与真实 Agent 接入方式完全一致）写入，最后再用 SQL 把用量按 14 天铺开。

用法:
    python scripts/seed_demo_data.py <port> <home_dir>

依赖: 仅 Python 3.8+ 标准库（sqlite3 用于回填历史用量）
"""
import json
import os
import random
import sqlite3
import sys
import time
import urllib.error
import urllib.request
from datetime import datetime, timedelta, timezone

for _stream in (sys.stdout, sys.stderr):
    if hasattr(_stream, "reconfigure"):
        _stream.reconfigure(encoding="utf-8", errors="replace")

PORT = sys.argv[1] if len(sys.argv) > 1 else "19096"
HOME = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.environ.get("TEMP", "/tmp"),
                                                          "miderhive-demo")
BASE = "http://127.0.0.1:%s" % PORT
OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))
MASTER = ""
ZCODE_KEY = ""
stats = {"ok": 0, "fail": 0}


def http(method, path, agent=None, key=None, body=None, master=None, timeout=15):
    headers = {"Content-Type": "application/json"}
    if agent:
        headers["X-Agent-Name"] = agent
    if key:
        headers["X-Api-Key"] = key
    if master:
        headers["X-Master-Key"] = master
    data = json.dumps(body).encode("utf-8") if body is not None else None
    req = urllib.request.Request(BASE + path, data=data, headers=headers, method=method)
    try:
        with OPENER.open(req, timeout=timeout) as resp:
            return resp.status, json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        try:
            return e.code, json.loads(e.read().decode("utf-8"))
        except Exception:
            return e.code, {}
    except Exception as e:  # 断连/超时：不要让播种中途崩掉
        return 0, {"code": -1, "message": str(e)}


def call(label, method, path, **kw):
    st, body = http(method, path, **kw)
    good = st in (200, 201) and body.get("code") == 0
    stats["ok" if good else "fail"] += 1
    if not good:
        print("  ! %-42s status=%s msg=%s" % (label, st, str(body.get("message"))[:60]))
    return st, body


# ---------------- 数据 ----------------

AGENTS = [
    ("claude", "重构知识检索的向量索引", "Claude Code"),
    ("codex", "补齐 MSI 升级路径的回归用例", "Codex CLI"),
    ("cursor", "打磨总览页的图表信息密度", "Cursor"),
    ("copilot", "生成 HTTP API 的调用示例", "GitHub Copilot"),
    ("gemini", "评估本地嵌入模型替换 n-gram", "Gemini CLI"),
    ("droid", "把 CI 迁到自托管 runner", "Factory Droid"),
    ("hermes", "整理本周审计异常项", "Hermes Agent"),
]
# 后两个故意不心跳：界面上呈现"离线"状态的真实混合，而不是清一色在线
OFFLINE = {"droid", "gemini"}

MEMORY = {
    "project": [
        ("name", "MiderHive 多 Agent 协作工作台"),
        ("goal", "本地优先的 Agent 协作中枢：知识、记忆、任务、审计四件事做透"),
        ("stack", "C++20 / Qt 6.8 Widgets / SQLite + sqlite-vec / CMake"),
        ("repo", "SiliconCoderJames/MiderHive（MIT）"),
        ("release_cycle", "每周一个补丁版，每两周一功能版；产物 MSI + 便携 ZIP"),
    ],
    "decision": [
        ("why_local_first", "数据留在本机、只监听 127.0.0.1：协作价值不依赖上云"),
        ("why_sqlite", "单文件数据库便于备份与迁移，vec 扩展在进程内静态链接"),
        ("why_no_silent_update", "未签名产物不做静默替换，只做自动检查 + 一键升级"),
        ("why_append_only_knowledge", "知识只追加版本不覆盖，避免踩坑记录被后人改写"),
    ],
    "preference": [
        ("naming", "Python snake_case / C++ camelCase / QSS 用 kebab-case"),
        ("commit", "中文摘要 + 英文正文，一次提交只做一件事"),
        ("ui_language", "界面中英双语，中文优先"),
        ("code_style", "不留注释噪声，只在「为什么」处写注释"),
    ],
    "environment": [
        ("os", "Windows 11 24H2 x64"),
        ("toolchain", "MSVC 19.4x / CMake 4.4 / Qt 6.8.3 (msvc2022_64)"),
        ("wix", "WiX 6.0.2（v7 起需接受维护费 EULA，故锁定 6.x）"),
        ("shell", "PowerShell 7 + ripgrep；CI 跑在 GitHub Actions windows-latest"),
    ],
    "habit": [
        ("standup", "每天 10:00 全员写一条进度到 Agent 交流（广播）"),
        ("review", "任何提交前先跑 platform_tests + feasibility_check"),
        ("release", "发行前出包 → 校验 SHA256 → 更新 latest.json → 打 tag"),
        ("error_policy", "报错必须登记并闭环，解决说明只追加不覆盖"),
    ],
}

KNOWLEDGE = [
    ("MSVC 默认代码页吞掉 UTF-8 注释",
     "MSVC 未加 /utf-8 时按 GBK 解析源文件，中文注释尾巴的字节会吞掉换行，导致紧随其后的 #include 失效并报一堆莫名其妙的错。修法：CMake 里对 MSVC 统一加 /utf-8。",
     ["msvc", "cmake", "encoding"], "踩坑"),
    ("Qt 资源路径会把子目录拼进 prefix",
     "qt_add_resources(PREFIX \"/theme\") 指向 qss/ 目录时，资源实际路径是 :/theme/qss/dark.qss 而不是 :/theme/dark.qss。QFile 打不开时先打印 errorString，别猜。",
     ["qt", "qrc"], "踩坑"),
    ("CMake 构建树不能直接跨目录搬",
     "CMakeCache.txt 里记着绝对路径，项目换目录后沿用旧 build 目录会报 \"directory different than the one where CMakeCache.txt was created\"。换路径必须删掉构建树重配。",
     ["cmake", "build"], "踩坑"),
    ("为什么知识条目只追加版本",
     "协作场景里「谁改了什么」比「最新是什么」更重要：覆盖会让踩坑记录失去上下文，也让审计无法回溯。因此 v1 永久保留，新内容一律生成新版本。",
     ["architecture", "governance"], "架构决策"),
    ("Token 预算与告警分级",
     "预算按周（week_start）统计，80% 记 warn、95% 记 critical、100% 以上记 over；只有 warn 及以上才在总览页生成告警卡片，避免正常用量时也长期处于告警色。",
     ["usage", "budget"], "架构决策"),
    ("更新器为什么用 release 资产的 latest.json",
     "固定 URL releases/latest/download/latest.json 不需要 token、不受未认证 API 每小时 60 次限流，且天然排除预发布版本；清单里的 SHA256 正是安装前要校验的东西。",
     ["updater", "release"], "架构决策"),
    ("弱网下的更新下载：重试 + Range 续传 + 镜像",
     "GitHub 直连在国内常超时：检查更新按 0.8/1.6/3.2s 退避重试；下载失败保留半成品并用 Range 只补差额；直连持续失败才切镜像。清单与 SHA256 始终直连 GitHub，因此镜像最坏只能让下载失败。",
     ["updater", "network"], "运维"),
    ("语义检索延迟观测",
     "10 万条以内的知识条目，sqlite-vec 的 KNN 查询在 40ms 量级；瓶颈通常在嵌入生成而不是检索。预算告警与检索延迟都写进审计，便于事后归因。",
     ["performance", "vec"], "性能"),
    ("per-user 安装与卸载的边界",
     "MSI 装到 %LOCALAPPDATA%\\MiderHive，不需要管理员权限；数据目录在 %USERPROFILE% 下，卸载不删除——重装即恢复全部协作历史。升级用同一 UpgradeCode 做 MajorUpgrade。",
     ["installer", "wix"], "运维"),
    ("审计不可删：只做归档",
     "审计表刻意不提供删除接口，只允许按时间范围归档导出。需要清理时由管理员导出后重建库，避免「悄悄删掉痕迹」这种能力本身成为风险。",
     ["audit", "security"], "安全"),
]

SKILLS = [
    ("code-review", "代码审查", "审查代码变更并给出可执行的修改意见", "dev"),
    ("knowledge-search", "知识检索", "按关键词或语义检索团队沉淀", "knowledge"),
    ("msi-package", "出安装包", "跑打包流水线并校验产物", "release"),
    ("audit-digest", "审计摘要", "把当日审计压缩成一段可读摘要", "ops"),
    ("standup-writer", "站会记录", "汇总各 Agent 进度并生成站会纪要", "ops"),
    ("ui-screenshot", "界面截图", "驱动工作台并对指定面板截图", "ui"),
]

TASKS = [
    ("claude", "codex", "task", "排查语义检索偶发空结果", "KNN 查询在 k 与 LIMIT 同时绑定时返回空，请复现并给出修复", "done"),
    ("codex", "claude", "task", "补 MSI 升级回归用例", "覆盖 AgentHive 1.0.x → MiderHive 的就地升级与数据目录迁移", "done"),
    ("cursor", "copilot", "task", "为 API 文档补调用示例", "docs/api.md 每个端点给一条 curl 与一条 Python 示例", "accepted"),
    ("hermes", "droid", "task", "把 CI 迁到自托管 runner", "目标是出包时间从 9 分钟降到 4 分钟以内", "accepted"),
    ("claude", "gemini", "task", "评估本地嵌入模型", "对比 n-gram 与 bge-small 在中文短文本上的召回", "pending"),
    ("copilot", "cursor", "task", "总览页图表密度调整", "窗口 1280 宽时柱状图标签不与数值重叠", "pending"),
    ("hermes", "codex", "task", "核对本周审计异常", "重点看越权流转被拒的记录是否被正确留痕", "pending"),
    ("codex", None, "note", "广播：今晚 22:00 起做 1.0.2 出包演练", "预计 20 分钟，期间服务不中断，数据目录不受影响", None),
    ("hermes", None, "note", "知识库本周新增 10 条，其中 3 条踩坑", "建议各 Agent 上线前先搜一遍再动手", None),
    ("claude", "hermes", "question", "预算口径按周还是按自然月？", "我在文档里看到 week_start，但设置页写的是本周，想确认一下", "done"),
    ("gemini", "claude", "question", "嵌入模型替换会不会影响历史条目？", "如果换模型，旧的向量要不要重算？", "accepted"),
    ("copilot", "codex", "task", "把 feasibility_check 接进 CI", "作为出包前的门禁，失败即阻断发布", "done"),
]

ERRORS_OPEN = [
    ("critical", "knowledge/search", "vec 表在空结果时未回落关键词检索",
     "语义检索返回空数组时没有回落到关键词模式，用户看到「没有结果」但其实库里有", "semanticSearch() at knowledge_service.cpp:214"),
    ("error", "updater/download", "GitHub 直连超时导致下载中断",
     "13MB 安装包在弱网下传一半断流，旧实现直接丢弃重下", "QNetworkReply RemoteHostClosedError"),
    ("warning", "gui/dashboard", "窄窗口下柱状图数值与标签重叠",
     "窗口宽度低于 1180 时，HBarChart 的数值列被柱体覆盖", "HBarChart::paintEvent"),
    ("error", "http/server", "请求体超过 1 MiB 时错误码不明确",
     "超大请求体返回 413 但信封缺失 message 字段", "http::Server::handleRequest"),
    ("note", "installer", "ICE61 警告未处理",
     "AllowSameVersionUpgrades 触发 ICE61，属预期但应写进文档", "wix msi validate"),
]

ERRORS_RESOLVED = [
    ("error", "db/migration", ".agenthive → .miderhive 数据目录迁移在只读盘失败",
     "旧目录改名失败时直接崩，应先降级为复制再迁移", "platform.cpp defaultHomeDir",
     "已修复：rename 失败回落 copy+remove，并在审计记录迁移结果"),
    ("warning", "gui/i18n", "切语言后侧栏计数残留旧文案",
     "buildNav() 重建时未重算错误计数，导致 (3) 仍显示旧值", "MainWindow::applyLanguage",
     "已修复：applyLanguage 末尾显式调用 updateStatusBar()"),
]


# ---------------- 播种 ----------------

def seed_agents():
    keys = {}
    for name, task, label in AGENTS:
        st, body = call("register %s" % name, "POST", "/api/agents/register",
                        body={"name": name, "role": "member", "client": label}, master=MASTER)
        if body.get("code") == 0:
            keys[name] = body["data"]["api_key"]
    for name, task, _label in AGENTS:
        if name not in keys or name in OFFLINE:
            continue
        call("heartbeat %s" % name, "POST", "/api/agents/heartbeat",
             agent=name, key=keys[name], body={"current_task": task})
    # 密钥落盘（仅演示目录）：心跳有有效期，截图/演示前可以用 --touch 复用这些密钥
    # 重新发一轮心跳，而不必删库重播。演示数据本就不含真实凭据。
    try:
        with open(os.path.join(HOME, "demo-agents.json"), "w", encoding="utf-8") as f:
            json.dump(keys, f, indent=2)
    except Exception as e:
        print("  ! 演示密钥落盘失败：%s" % e)
    print("  agents: %d 注册 / %d 在线" % (len(keys), len(keys) - len(OFFLINE)))
    return keys


def touch_liveness():
    """重新发一轮心跳：演示数据放久了 Agent 会掉线，截图前跑一次即可回到正常状态。"""
    path = os.path.join(HOME, "demo-agents.json")
    if not os.path.exists(path):
        print("缺少 %s（由完整播种生成），无法刷新在线状态" % path)
        return 2
    with open(path, "r", encoding="utf-8") as f:
        keys = json.load(f)
    n = 0
    for name, task, _label in AGENTS:
        if name in OFFLINE or name not in keys:
            continue
        st, body = http("POST", "/api/agents/heartbeat", agent=name, key=keys[name],
                        body={"current_task": task})
        if body.get("code") == 0:
            n += 1
    print("心跳刷新: %d 个 Agent 在线（%s 保持离线）" % (n, "/".join(sorted(OFFLINE))))
    return 0


def seed_memory(keys):
    n = 0
    for section, items in MEMORY.items():
        for k, v in items:
            actor = "claude" if section in ("project", "decision") else "hermes"
            st, body = call("memory %s/%s" % (section, k), "POST", "/api/memory",
                            agent=actor, key=keys.get(actor, ""),
                            body={"section": section, "key": k, "value": v})
            if body.get("code") == 0:
                n += 1
    print("  memory: %d 条" % n)


def seed_knowledge(keys):
    uuids = []
    for title, content, tags, category in KNOWLEDGE:
        st, body = call("knowledge %s" % title[:16], "POST", "/api/knowledge",
                        agent="hermes", key=keys.get("hermes", ""),
                        body={"title": title, "content": content, "tags": tags, "category": category})
        if body.get("code") == 0:
            uuids.append(body["data"]["uuid"])
    # 追加版本：演示"只追加不覆盖"
    if uuids:
        call("knowledge v2", "POST", "/api/knowledge/%s/versions" % uuids[0],
             agent="codex", key=keys.get("codex", ""),
             body={"content": "补充：MinGW 下不需要该选项，但建议全平台统一 /utf-8，"
                              "避免同一份源码在不同编译器下表现不一致。"})
        call("knowledge v2", "POST", "/api/knowledge/%s/versions" % uuids[3],
             agent="claude", key=keys.get("claude", ""),
             body={"content": "补充：覆盖式编辑会让审计失去上下文，因此接口层直接拒绝 PUT 更新，"
                              "只提供 versions 追加。"})
    print("  knowledge: %d 条（含 2 条追加版本）" % len(uuids))


def seed_skills(keys):
    for name, display, desc, category in SKILLS:
        call("skill %s" % name, "POST", "/api/skills", agent="hermes", key=keys.get("hermes", ""),
             body={"name": name, "display_name": display, "description": desc, "category": category,
                   "param_schema": {"type": "object", "properties": {"target": {"type": "string"}}}})
    # 真实调用记录：含成功与失败，Token 记账落入用量统计
    invocations = [
        ("codex", "code-review", "success", "发现 2 处空指针风险与 1 处未检查的返回值", 3120, 1480),
        ("claude", "code-review", "success", "补齐错误分支的日志埋点，无阻断项", 4200, 2210),
        ("cursor", "ui-screenshot", "success", "截取总览/知识库两个面板，尺寸 2560x1351", 1800, 640),
        ("copilot", "knowledge-search", "success", "命中 3 条相关踩坑记录，top1 为 /utf-8 问题", 960, 380),
        ("hermes", "audit-digest", "success", "当日 42 条写操作，2 条越权被拒", 5400, 1900),
        ("gemini", "knowledge-search", "failed", "嵌入服务未就绪，超时退出", 2200, 120),
        ("claude", "standup-writer", "success", "汇总 7 个 Agent 进度，生成站会纪要", 3600, 2400),
        ("codex", "msi-package", "success", "MiderHive-1.0.2-x64.msi 12.69 MB，ICE 通过", 800, 1500),
        ("cursor", "code-review", "success", "QSS 选择器无冲突，建议合并 padding 规则", 2600, 1100),
        ("hermes", "knowledge-search", "success", "语义检索延迟 38ms", 1100, 420),
        ("copilot", "standup-writer", "success", "生成英文版纪要，术语已对齐", 2900, 1800),
        ("claude", "msi-package", "failed", "vcruntime140.dll 缺失，已阻断出包", 400, 90),
    ]
    okn = 0
    for actor, skill, status, summary, tin, tout in invocations:
        st, body = call("invoke %s" % skill, "POST", "/api/skills/%s/invoke" % skill,
                        agent=actor, key=keys.get(actor, ""),
                        body={"params": {"target": "demo"}, "status": status,
                              "result_summary": summary, "duration_ms": random.randint(420, 3200),
                              "tokens_in": tin, "tokens_out": tout})
        if body.get("code") == 0:
            okn += 1
    print("  skills: %d 注册 / %d 次调用" % (len(SKILLS), okn))


def seed_messages(keys):
    made = 0
    for sender, recipient, kind, subject, body, target_status in TASKS:
        payload = {"kind": kind, "subject": subject, "body": body}
        # 广播 = 不带 recipient 键（显式传 null 会被参数校验拒绝）
        if recipient:
            payload["recipient"] = recipient
        st, resp = call("msg %s" % subject[:14], "POST", "/api/messages",
                        agent=sender, key=keys.get(sender, ""), body=payload)
        if resp.get("code") != 0:
            continue
        made += 1
        uuid = resp["data"]["uuid"]
        # 只有 task 走 pending→accepted→done；question 是 unread 状态机，保持未读更像真实情况
        if kind == "task" and recipient and target_status in ("accepted", "done"):
            call("accept", "POST", "/api/messages/%s/status" % uuid, agent=recipient,
                 key=keys.get(recipient, ""), body={"status": "accepted"})
            if target_status == "done":
                call("done", "POST", "/api/messages/%s/status" % uuid, agent=recipient,
                     key=keys.get(recipient, ""), body={"status": "done"})
    print("  messages: %d 条（含待办/进行中/已完成/广播/提问）" % made)


def seed_errors(keys):
    for severity, source, title, detail, trace in ERRORS_OPEN:
        call("error %s" % title[:14], "POST", "/api/errors", agent="codex",
             key=keys.get("codex", ""),
             body={"severity": severity, "source": source, "title": title,
                   "detail": detail, "stack_trace": trace})
    for severity, source, title, detail, trace, notes in ERRORS_RESOLVED:
        st, resp = call("error %s" % title[:14], "POST", "/api/errors", agent="codex",
                        key=keys.get("codex", ""),
                        body={"severity": severity, "source": source, "title": title,
                              "detail": detail, "stack_trace": trace})
        if resp.get("code") != 0:
            continue
        uuid = resp["data"]["uuid"]
        call("resolve", "POST", "/api/errors/%s/resolve" % uuid, agent="codex",
             key=keys.get("codex", ""), body={"notes": notes})
        if ZCODE_KEY:  # 管理者追加复核（追加不覆盖）
            http("POST", "/api/errors/%s/resolve" % uuid, agent="zcode", key=ZCODE_KEY,
                 body={"notes": "zcode 复核：结论成立，已纳入周报"})
    print("  errors: %d 未解决 / %d 已闭环" % (len(ERRORS_OPEN), len(ERRORS_RESOLVED)))


def seed_usage(keys):
    # 预算 7M：配合下面的历史铺法，本周落在六成上下——看得出用量规模，
    # 又不会被随机波动顶到 80% 的 warn 阈值（演示数据要像个正常运转的团队）
    call("budget", "PUT", "/api/usage/budget", master=MASTER, body={"budget": 7000000})
    models = ["claude-sonnet-4.5", "gpt-5-codex", "gemini-3-pro", "cursor-fast", "copilot-chat"]
    rows = 0
    for actor in ("claude", "codex", "cursor", "copilot", "hermes", "gemini"):
        if actor not in keys:
            continue
        for _ in range(random.randint(2, 3)):
            st, body = call("usage %s" % actor, "POST", "/api/usage/report",
                            agent=actor, key=keys[actor],
                            body={"tokens_in": random.randint(8000, 45000),
                                  "tokens_out": random.randint(2000, 18000),
                                  "call_type": "llm", "model": models[hash(actor) % len(models)]})
            if body.get("code") == 0:
                rows += 1
    print("  usage: 今日 %d 次上报（历史 13 天稍后由 SQL 铺开）" % rows)


def backfill_history():
    """把用量铺到最近 14 天，并给审计留痕铺时间线。

    只用一份演示库，直接写 SQL 比反复调接口更快，且能精确控制分布形态
    （工作日高、周末低），让趋势图看起来像真实使用而不是一条水平线。
    """
    db = os.path.join(HOME, "platform.db")
    if not os.path.exists(db):
        print("  ! 未找到 %s，跳过历史回填" % db)
        return
    con = sqlite3.connect(db)
    cur = con.cursor()
    agents = [n for n, _t, _l in AGENTS]
    models = ["claude-sonnet-4.5", "gpt-5-codex", "gemini-3-pro", "cursor-fast"]
    today = datetime.now(timezone.utc)
    inserted = 0
    for back in range(1, 14):
        day = today - timedelta(days=back)
        weekday = day.weekday()
        # 周末用量明显更低；越近的日期略高（但不要造出断崖式波动，
        # 否则 KPI 的"较昨日 ±xx%"会离谱到不像真实团队）
        base = 110000 if weekday < 5 else 40000
        scale = 0.75 + 0.25 * (13 - back) / 12.0
        for agent in agents:
            if random.random() < 0.2:      # 不是每个 Agent 每天都用
                continue
            tin = int(base * scale * random.uniform(0.8, 1.25))
            tout = int(tin * random.uniform(0.2, 0.55))
            monday = (day - timedelta(days=weekday)).strftime("%Y-%m-%d")
            cur.execute(
                "INSERT INTO token_usage (agent, week_start, tokens_in, tokens_out, call_type,"
                " model, created_at) VALUES (?,?,?,?,?,?,?)",
                (agent, monday, tin, tout, "llm", models[hash(agent) % len(models)],
                 day.strftime("%Y-%m-%dT%H:%M:%SZ")))
            inserted += 1
    # 审计时间线：把已有记录摊到最近 3 天，事件流看起来是「活的」
    cur.execute("SELECT id FROM audit_log ORDER BY id")
    ids = [r[0] for r in cur.fetchall()]
    for i, rid in enumerate(ids):
        offset_h = (len(ids) - i) * 0.7 + random.uniform(0, 2)
        ts = today - timedelta(hours=offset_h)
        cur.execute("UPDATE audit_log SET created_at=? WHERE id=?",
                    (ts.strftime("%Y-%m-%dT%H:%M:%SZ"), rid))
    # 混合在线状态：两个 Agent 故意离线（注册本身也会刷新 last_seen，
    # 所以必须在最后一步显式改写，否则界面上会清一色"全部在线"）
    for name, hours in (("droid", 3.5), ("gemini", 9.0)):
        ts = (today - timedelta(hours=hours)).strftime("%Y-%m-%dT%H:%M:%SZ")
        cur.execute("UPDATE agents SET status='offline', last_seen_at=? WHERE name=?", (ts, name))
    con.commit()
    con.close()
    print("  历史回填: token_usage +%d 行 / audit 重排 %d 行" % (inserted, len(ids)))


def main():
    global MASTER, ZCODE_KEY
    MASTER = (os.environ.get("MIDERHIVE_MASTER_KEY")
              or os.environ.get("AGENTHIVE_MASTER_KEY") or "")
    if not MASTER:
        print("需要环境变量 MIDERHIVE_MASTER_KEY")
        return 2
    agents_json = os.path.join(HOME, "config", "agents.json")
    if os.path.exists(agents_json):
        try:
            with open(agents_json, "r", encoding="utf-8") as f:
                ZCODE_KEY = json.load(f).get("zcode", "")
        except Exception:
            pass
    st, body = http("GET", "/api/health")
    if body.get("code") != 0:
        print("平台未就绪: %s" % body)
        return 2
    # --touch：只刷新在线状态（心跳有有效期），用于久放的演示数据重新截图/演示
    if "--touch" in sys.argv:
        return touch_liveness()
    # 必须有干净的数据目录：Agent 密钥只在注册响应里返回一次（库里只存哈希），
    # 若目录非空则拿不到已有 Agent 的密钥，后续所有写入都会 401。
    db = os.path.join(HOME, "platform.db")
    if os.path.exists(db):
        try:
            con = sqlite3.connect(db)
            # bootstrap 会预置 zcode 管理者，不算"已有数据"
            n = con.execute("select count(*) from agents where role <> 'zcode'").fetchone()[0]
            con.close()
        except Exception:
            n = 0
        if n > 0:
            print("数据目录已有 %d 个 Agent（%s）：演示数据需要空目录。" % (n, HOME))
            print("请先停止工作台并删除该目录，或改用新的数据目录。")
            return 2
    print("播种演示数据 → %s (port %s)" % (HOME, PORT))
    keys = seed_agents()
    seed_memory(keys)
    seed_knowledge(keys)
    seed_skills(keys)
    seed_messages(keys)
    seed_errors(keys)
    seed_usage(keys)
    backfill_history()
    print("完成：成功 %d / 失败 %d" % (stats["ok"], stats["fail"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())

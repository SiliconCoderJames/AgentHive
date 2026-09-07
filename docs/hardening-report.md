# 后端加固报告（第二轮优化 · 第一部分）

日期：2026-09-07 · 基线：99 单元测试 + 39 集成断言全绿 → 加固后 **132 单元测试全绿** + 39/39 集成断言

## 1. Token 预算并发扣减

| 项 | 结论 | 说明 |
|---|---|---|
| 并发扣减事务 | **已修复 + 代码** | `UsageService::report` 改为 `BEGIN IMMEDIATE` → 事务内查重 → 原子插入 → `COMMIT`（[usage_service.cpp](../src/core/services/usage_service.cpp)）。进程内另有 Platform 全局锁串行化；跨进程场景由 IMMEDIATE 写锁保证 |
| 超额拦截 | **已新增** | `Platform::skillInvoke` 在记录前检查周累计用量，达到上限返回错误（前缀 `weekly token budget exceeded`）；HTTP 层映射为 **429**（[server.cpp](../src/core/http/server.cpp)）。单测覆盖：预算耗尽后 skillInvoke 被拒 |
| 幂等性 | **已新增** | `token_usage` 新增 `idempotency_key` 列 + 部分唯一索引；重复键只记一次，响应含 `duplicate: true`。单测：同键两次上报 total_tokens 不变 |

## 2. 数据增长与清理

| 项 | 结论 | 说明 |
|---|---|---|
| 操作日志轮转 | **已新增** | `maintenanceRun`：删除 30 天前且最多保留 10 万条（启动时自动执行 + `POST /api/maintenance` 手动触发，主密钥） |
| 已解决错误归档 | **已新增** | 解决超过 30 天的错误从主表清除 |
| 知识库/记忆清理接口 | **已新增** | `DELETE /api/knowledge/{uuid}`、`DELETE /api/memory?section&key`（仅管理者，连同全部版本与向量，写入审计 `knowledge.remove`/`memory.remove`） |
| SQLite VACUUM | **已新增** | 维护产生删除后自动 `VACUUM` 回收空间 |

## 3. 内存与稳定性

| 项 | 结论 | 说明 |
|---|---|---|
| AddressSanitizer | **已通过（限定）** | MSVC `/fsanitize=address` 重跑端到端测试：132 检查全过、0 个 ASan 报告（堆溢出/UAF）。注：Windows 不支持退出时泄漏检测（LeakSanitizer 不可用），泄漏项由浸泡测试替代 |
| 加速浸泡测试 | **已通过（加速替代 24h）** | 60 秒高频混合请求 **8626 ops / 0 错误**（144 ops/s）；platformd 工作集 8.2MB → 9.8MB 后收敛（尾段每 10 秒增量 < 100KB），无持续增长趋势。24 小时全时长测试建议在长期运行环境补充 |
| HTTP body/连接释放 | **已确认** | cpp-httplib 在 handler 返回后统一释放 Request/Response；ASan 全程无 UAF 报告佐证无悬空引用 |

## 4. 用户记忆并发冲突

| 项 | 结论 | 说明 |
|---|---|---|
| 并发追加版本链 | **已确认** | Platform 全局互斥 + `is_latest` 版本链，多 Agent 并发写同一 key 各自成版本、互不覆盖 |
| 冲突提示 | **已新增** | `POST /api/memory` 支持可选 `base_version`（乐观并发）：与最新版本不符时返回 **409** `version conflict: expected base vX, latest is vY`，不写入。单测覆盖 |

## 5. 向量检索精度

| 项 | 结论 | 说明 |
|---|---|---|
| n-gram 能力定位 | **已确认** | 现有 `NgramHashEmbedder` 为增强型模糊关键词匹配（字符 2/3-gram 特征哈希），适合短文本/标签级召回，不等于真正的语义向量 |
| 模型嵌入预留接口 | **已确认（既有）** | `Embedder` 抽象接口已预留：Agent 在写入/搜索时可自带 `embedding` 数组并标注 `embedder` 名称，平台按 provider 存储；接入真实模型只需新增 `Embedder` 实现（如 ONNX Runtime bge-m3），检索层零改动 |

## 6. 数据备份与恢复

| 项 | 结论 | 说明 |
|---|---|---|
| 手动备份 | **已新增** | `POST /api/system/backup`（主密钥）：`VACUUM INTO` 生成 `backup/platform-<时间戳>.db` 一致性快照（自带 checkpoint，WAL 已提交数据全部包含） |
| 恢复 | **已新增** | `GET /api/system/backups` 列表 + `POST /api/system/restore {"file": 名}`：校验 SQLite 文件头、拒绝路径穿越、关闭连接（自动 checkpoint）→ 覆盖 → 重开并重新挂载 vec 扩展。单测覆盖：备份→清空→恢复→数据一致 |
| 备份期 WAL checkpoint | **已确认** | `VACUUM INTO` 读取一致快照；恢复路径经 `sqlite3_close` 自动 checkpoint |

## 7. 安全加固

| 项 | 结论 | 说明 |
|---|---|---|
| API 密钥加盐哈希 | **已修复** | 注册新 Agent 改为 `salt = randomHex(16)`、存储 `sha256(salt + key)`（`agents.salt` 列，存量库自动迁移；盐为空的旧格式继续兼容认证）。明钥仅注册时返回一次 |
| 输入长度限制 | **已新增** | Platform 层统一校验：知识（标题≤200/内容≤10 万/标签≤20×64）、记忆（值≤2 万）、消息（正文≤5 万）、错误（≤5 万）、技能（schema≤1 万）等，超长返回 400。单测覆盖 |
| 仅本机绑定 | **已确认** | `HttpServer::start` 硬编码 `bind_to_port("127.0.0.1", ...)`，netstat 实测无 0.0.0.0 监听 |
| SQL 注入 | **已确认（既有）** | 全部查询经 `sqlite3_bind_*` 参数绑定，无字符串拼装 |

## 验证汇总

- 单元测试：**132 checks / 0 failures**（新增 33 项加固断言）
- ASan 端到端：132 checks / 0 AddressSanitizer 报告
- 浸泡：60s / 8626 ops / 0 错误 / 内存收敛
- 存量数据库迁移：`ALTER TABLE` 幂等迁移（duplicate column 静默跳过），旧密钥格式兼容认证

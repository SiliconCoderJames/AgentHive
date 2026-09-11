# 第三方组件许可与声明（THIRD-PARTY NOTICES）

MiderHive 本体以 **MIT** 许可发布（见 [LICENSE](LICENSE)）。发行版二进制中还包含下列
第三方组件，它们各自遵循自己的许可。**分发二进制时必须随附本文件与相应许可文本**
（`cmake --install` 会把本文件与 LICENSE 一起放进安装目录的 `licenses/`）。

| 组件 | 用途 | 许可 | 分发方式 |
|---|---|---|---|
| Qt 6（Widgets / Network / Svg） | 桌面工作台 UI | **LGPLv3**（或商业许可） | **动态链接**（Qt6*.dll + plugins/） |
| SQLite（amalgamation） | 本地数据库 | 公有领域 (Public Domain) | 静态编译进进程 |
| sqlite-vec | 向量检索（vec0 虚拟表） | MIT / Apache-2.0 双许可 | 静态编译进进程 |
| nlohmann/json | JSON 解析 | MIT | 头文件 |
| cpp-httplib | 本机 HTTP 服务与客户端 | MIT | 头文件 |

## 关于 Qt（LGPLv3）的合规说明

本项目**动态链接** Qt，未对 Qt 做任何修改。按 LGPLv3 的要求：

1. 随附 LGPLv3 全文（Qt 安装目录下的 `LICENSE.LGPLv3`，或
   <https://www.gnu.org/licenses/lgpl-3.0.html>）；
2. 声明使用了 Qt 及其版本（Qt 6.8.3）；
3. 允许最终用户用自行编译/替换的 Qt 库重新链接本程序——本发行版未做静态链接，
   也未锁定 Qt 库文件，用户替换 `Qt6*.dll` 即可完成重链接；
4. 不把 Qt 代码与本项目代码以妨碍上述替换的方式合并。

> 若你计划以商业许可方式使用 Qt，或以静态方式链接 Qt，请相应调整本节与许可文本。

## 无担保声明

本软件按"现状"提供，不附带任何明示或默示的担保。所有 Agent 协作数据保存在
用户本机的 `%USERPROFILE%\.miderhive`（可通过 `MIDERHIVE_HOME` 修改，旧名
`AGENTHIVE_HOME` 仍兼容识别），
卸载程序**不会**删除该目录。

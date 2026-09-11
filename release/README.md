# 发行说明（Release Guide）

本目录集中放**发行相关的所有东西**：打包源文件、流程文档，以及打包产物（产物不入库，见 `.gitignore`）。

```text
release/
├─ README.md                  ← 本文档：怎么出包、产物清单、注意事项
├─ wix/
│  ├─ AgentHive.wxs           ← MSI 定义（per-user 安装、开始菜单/桌面快捷方式、升级与卸载策略）
│  └─ files.generated.wxs     ← 由 scripts/gen-wix-files.ps1 从暂存目录生成（派生文件，不入库）
├─ AgentHive-<版本>-x64.msi   ← 安装包（产物，不入库）
├─ AgentHive-<版本>-win64-portable.zip ← 便携版（产物，不入库）
└─ SHA256SUMS.txt             ← 校验和（产物，不入库）
```

## 一条命令出包

```powershell
powershell -ExecutionPolicy Bypass -File scripts\package.ps1 -Version 1.0.0
```

脚本按 7 步执行，任一步失败即中止（不会产出半成品）：

1. `cmake` 配置 + 编译（版本号经 `-DAGENTHIVE_VERSION` 注入）
2. `cmake --install --component Runtime` 安装到暂存目录 `_stage/`
3. **可运行性自检**：断言主程序、Qt DLL、平台插件、MSVC 运行库、许可文件都在
4. 生成 WiX 文件清单（`release/wix/files.generated.wxs`）
5. `wix build` 出 MSI，并跑 **ICE 校验**（见下）
6. 打包便携版 ZIP（内含一层 `AgentHive-<版本>/` 目录，解压不散落）
7. 生成 `SHA256SUMS.txt`

常用参数：`-SkipBuild`（复用已有构建）、`-PerMachine`（改装到 Program Files，需要管理员）、
`-OutDir` / `-StageDir` / `-BuildDir` / `-QtPrefix` / `-Generator`。

## CI 自动发布

推一个 `v*` 标签即触发 `.github/workflows/release.yml`：安装 Qt → 出包 → 跑单测 →
用 `gh` 创建 Release 并上传 MSI、ZIP 与校验和。也可以在该 workflow 上手动触发并指定版本号。

```bash
git tag v1.0.0 && git push origin v1.0.0
```

## 版本号只有一个来源

`CMakeLists.txt` 的 `project(... VERSION ...)` → 生成 `core/version.h` → 界面侧栏、`/api/health`、
安装包属性三处共用。发布时用 `-DAGENTHIVE_VERSION`（或 CI 传标签名）覆盖。

> **注意**：PowerShell 5.1 会把**未加引号**的 `-DAGENTHIVE_VERSION=1.0.0` 截断成 `...=1`，
> 于是静默产出一个错误版本号。`package.ps1` 已使用整体加引号的写法，CMake 侧也加了格式校验
> （不匹配 `v?主.次.修订` 直接 configure 失败），避免"装完发现版本号不对"。

## 安装包设计取舍

| 项 | 取值 | 理由 |
|---|---|---|
| 安装范围 | **per-user**（`%LOCALAPPDATA%\AgentHive`） | 与既有 `scripts/deploy.ps1` 的约定一致；安装不需要管理员权限、不弹 UAC。企业批量下发可用 `-PerMachine` 切到 Program Files |
| 快捷方式 | 开始菜单 + 桌面 | 工作台是常驻托盘的应用，桌面入口更顺手 |
| 升级 | 同 `UpgradeCode` + `MajorUpgrade` | 新版覆盖安装，旧版先移除，避免文件残留 |
| 卸载 | 删除程序与快捷方式，**不动用户数据** | 用户数据在 `%USERPROFILE%\.agenthive`，不在安装包管理范围内；已实测卸载后数据完好 |
| 运行库 | 随包分发 MSVC 运行库（应用本地部署） | 目标机无需预装 VC++ Redistributable |
| 系统要求 | Windows 10+ | Qt 6.8 / C++20 的要求；用注册表探测判别（不用 `VersionNT`，见下） |

### 两个踩过的坑（都是实测出来的，改之前请先看这里）

1. **不能用 `VersionNT` 判断系统版本**：Windows Installer 会把本包的 `VersionNT` 从 603 一路
   降级到 500（旧式包兼容逻辑），于是 `VersionNT >= 1000` 在 Windows 11 上也判失败、安装直接
   报 1603。现在改为探测 `HKLM\...\CurrentVersion\CurrentMajorVersionNumber` **是否存在**
   （Win10 起才有该值），存在性判断既准确又不受降级影响。
   > 补充：DWORD 经 `RegistrySearch` 读出来是 `"#10"` 形式，拿它做数值比较会被 ICE03 判为
   > 非法条件字符串，所以这里刻意只判存在性、不比值。
2. **per-user 包必须用 HKCU 注册表项作组件 KeyPath**（ICE38），并且每个用户目录都要登记
   `RemoveFolder`（ICE64），否则卸载会残留空目录。这些都由 `gen-wix-files.ps1` 自动生成：
   组件 GUID 从"安装相对路径"派生，跨版本稳定，升级时替换文件而不是重复安装。

### 许可

安装目录内含 `licenses/THIRD-PARTY-NOTICES.md` 与 `LICENSE`。**Qt 是 LGPLv3**，本发行版动态链接
Qt 且未做修改，用户可用自行编译的 Qt 库替换 `Qt6*.dll` 完成重链接；分发二进制时请连同该许可
文件一起提供，详见 `THIRD-PARTY-NOTICES.md`。

### 代码签名（尚未配置）

当前产物**未签名**，用户首次运行会看到 Windows SmartScreen 的"未知发布者"提示。
正式对外分发建议购买 OV 代码签名证书并在打包后追加 `signtool` 步骤；在那之前，
请在 Release 说明里主动写明这一点，避免被当成可疑文件。

## 本地验证清单（发版前跑一遍）

```powershell
# 1) 出包（含自检与 ICE 校验）
powershell -ExecutionPolicy Bypass -File scripts\package.ps1 -Version 1.0.0

# 2) 装一次，确认程序能起、版本号正确
msiexec /i release\AgentHive-1.0.0-x64.msi
#    打开工作台 → 关于/侧栏应显示 v1.0.0；/api/health 也应返回 1.0.0

# 3) 卸载，确认用户数据未被删除
msiexec /x release\AgentHive-1.0.0-x64.msi
```

便携版同样建议解压到干净目录后直接运行一次。

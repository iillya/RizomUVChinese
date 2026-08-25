# RizomUV 简体中文补丁

为 Windows 版 RizomUV 制作的简体中文显示层补丁。目前主要面向 **RizomUV Real Space 2025.0**，已在 **2025.0.104** 上完成实际测试。

补丁不会修改 RizomUV 原始程序、许可证、工程文件或 UV 数据。关闭 RizomUV 后，所有界面汉化都会随进程一起消失。

## 主要特点

- 一键安装、一键拆卸，无需手动复制文件。
- 汉化菜单、工具栏、面板、按钮、选项和工具提示。
- 保留原有快捷键、命令功能和操作流程。
- 不修改 `rizomuv.exe`，不向工程文件写入中文数据。
- 支持手动采集遗漏的英文词条，便于持续完善词库。
- 安装失败时自动保留或恢复上一版汉化。

## 下载

普通用户只需要下载并运行：

```text
dist\安装RizomUV汉化.exe
```

安装器需要管理员权限，因为 RizomUV 默认安装在 `C:\Program Files`。

## 安装方法

1. 保存并关闭正在运行的 RizomUV。
2. 运行 `安装RizomUV汉化.exe`。
3. 确认安装器识别到的 RizomUV 目录正确。
4. 点击“安装汉化”。
5. 安装完成后，通过桌面或开始菜单中的“RizomUV 简体中文版”启动。

默认安装目录为：

```text
C:\Program Files\Rizom Lab\RizomUV 2025.0
```

如果 RizomUV 安装在其他位置，可以点击“选择目录”，手动选择包含 `rizomuv.exe` 的文件夹。

安装器会创建：

```text
RizomUV 2025.0\ChineseLauncher\
├─ RizomUVChineseLauncher.exe
├─ RizomUVChineseRuntime.dll
└─ dictionary_zh.json
```

原版 RizomUV 文件不会被替换。

## 启动方法

安装后请使用“RizomUV 简体中文版”快捷方式启动。直接运行原版 `rizomuv.exe` 时不会加载汉化。

中文启动器会先启动原版 RizomUV，再加载显示层运行时。RizomUV 的内部命令、模型、UV、工程和导出内容仍然使用原始数据。

## 拆卸方法

1. 保存并关闭 RizomUV。
2. 再次运行 `安装RizomUV汉化.exe`。
3. 点击“拆卸汉化”。

拆卸程序只会删除 `ChineseLauncher` 目录以及本补丁创建的桌面、开始菜单快捷方式，不会删除或修改 RizomUV 本体。

如果窗口已经关闭但后台仍有 `rizomuv.exe`，安装器会显示进程 PID，并询问是否强制结束。选择“是”可能导致尚未保存的数据丢失，请确认已经保存工程。

## 采集遗漏的英文词条

漏词采集平时处于关闭状态，不会持续扫描或写入文件。

发现某个界面仍显示英文时：

1. 先打开或停留在该英文界面。
2. 按一次 `Shift + ~`。
3. 在接下来的 1.5 秒内切换、展开或悬停需要采集的界面。
4. 探测结束后，补丁会自动打开输出目录。

采集文件保存在：

```text
C:\Program Files\Rizom Lab\RizomUV 2025.0\ChineseLauncher\missing_ui_text_进程ID.jsonl
```

每次启动 RizomUV 会使用当前进程 ID 生成独立文件。用户路径、显卡名称、许可证机器指纹等动态内容不应加入正式词库。

## 安全说明

这是显示层汉化，不是程序本体修改器。

- 主体界面：在 RizomUV 调用 Windows GDI 绘制文字时临时替换显示参数。
- 原生菜单：只修改当前进程内菜单对象的标题，不改变菜单命令 ID。
- 磁盘文件：仅安装启动器、运行时 DLL、中文词库和快捷方式。
- 工程数据：不会修改模型、UV、项目、脚本或导出文件。
- 许可证：不会修改或绕过 RizomUV 许可证逻辑。

由于启动器需要把汉化运行时加载到 RizomUV 进程，少数安全软件可能将其视为 DLL 注入并产生误报。请只从可信来源下载，必要时自行核对项目源码并重新构建。

## 性能与兼容性

补丁只处理界面文字，不参与 UV 展开、优化、排布或导出计算。

- 漏词采集默认关闭，按快捷键后只运行 1.5 秒。
- 菜单翻译只在启动阶段短暂扫描。
- 菜单栏署名采用事件驱动，不持续刷新。
- 日常开销主要是绘制文字时查询内存词库，整体影响较低。

兼容性说明：

- 已验证：Windows x64、RizomUV Real Space 2025.0.104。
- 同一 2025.0 系列的小版本通常可以兼容，但新词条可能暂时显示英文。
- 如果未来版本更换文字渲染方式，部分或全部主体界面汉化可能失效。
- 当前方案仅处理 `rizomuv.exe` 主模块使用的已验证 GDI 文字入口。

## 常见问题

### 安装器提示 RizomUV 正在运行

先在 RizomUV 中保存工程并正常退出。如果窗口已经关闭但进程仍在后台，可以在安装器提示中确认结束，或在任务管理器中手动结束 `rizomuv.exe`。

### 提示无法清理临时目录或备份目录

通常是旧运行时 DLL 仍被 RizomUV 占用。关闭所有 RizomUV 进程后重新运行最新版安装器。新版安装器会自动恢复上次更新留下的 `.previous` 备份。

### 安装后仍是英文

- 确认使用的是“RizomUV 简体中文版”快捷方式，而不是原版快捷方式。
- 确认 `ChineseLauncher` 目录中的三个文件完整存在。
- 查看同目录下的 `RizomUVChineseRuntime.log`。
- 如果只有少数位置是英文，请使用 `Shift + ~` 采集漏词。

### `Shift + ~` 没有生成文件

- 确认已经使用最新版安装包重新安装。
- 确认日志中出现“UI 漏词探测已就绪”。
- 快捷键可能被其他程序占用；关闭相关软件后重试。
- 如果本轮没有未翻译英文，目录仍会打开，但不一定生成新的内容。

### 中文文字显示不完整

部分界面尺寸由原版布局决定，中文比英文更长时可能出现拥挤或截断。请截图并附上对应的漏词采集文件反馈。

## 当前词库

正式词库位于：

```text
translations\dictionary_zh.json
```

RizomUV 2025.0.104 词库目前包含 3835 条映射，其中包括中文译文，以及必须保持原样的快捷键、单位和产品名称。

## 开发与构建

需要 Visual Studio 2022、CMake 和 Windows x64 工具链。

```powershell
cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64
cmake --build build-msvc --config Release --target RizomUVOneClickPackage
```

生成的单文件安装包位于：

```text
dist\安装RizomUV汉化.exe
```

项目主要目录：

```text
source\                                    补丁、启动器与安装器源码
translations\                              正式运行词库与翻译记录
icon\                                      程序图标资源
scripts\diagnostics\                       开发期探测工具
scripts\extraction\                        静态文本提取工具
scripts\                                   词库整理与校验工具
dist\                                      发布安装包
```

## 鸣谢与许可

一键安装器的交互与安全回滚思路参考了 GPL-3.0 项目 [iillya/Toolbag](https://github.com/iillya/Toolbag)，并针对 RizomUV 的独立启动器和显示层架构重新实现。

本项目许可信息见 [LICENSE](LICENSE)。RizomUV 是 Rizom-Lab 的产品，本项目为非官方社区汉化补丁，与 Rizom-Lab 无隶属关系。

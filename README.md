# RizomUV 简体中文补丁

当前仅提供 **Inno Setup 安装器**，文件名为 `RizomUVChineseInstaller.exe`。安装、更新、迁移及卸载说明见 [安装指南](docs/installer.md)。卸载请使用 Windows“已安装的应用”，不再使用旧安装器。

适用于 Windows 版 RizomUV 的非官方简体中文补丁。安装后通过专用中文启动器进入 RizomUV，即可在不改动原程序和工程数据的情况下显示中文界面。

目前主要面向 **RizomUV Real Space 2025.0**，已在 **2025.0.104** 上实际测试。

## 下载与安装

普通用户只需下载 Releases 中的 `RizomUVChineseInstaller.exe`。

1. 保存工程并关闭 RizomUV。
2. 运行安装器；出现系统权限提示时选择“是”。
3. 选择包含 `rizomuv.exe` 的 RizomUV 安装目录。
4. 按向导点击“安装”。
5. 安装完成后，从桌面或开始菜单打开“RizomUV 中文版”。

常见的默认目录：

```text
C:\Program Files\Rizom Lab\RizomUV 2025.0
```

如果安装器没有自动找到软件，请手动选择正确目录，不要选择 `ChineseLauncher` 子目录。

## 如何启动

请始终使用“RizomUV 中文版”快捷方式启动。直接运行原版 `rizomuv.exe` 不会加载汉化，这是正常现象。

如果快捷方式创建失败，也可以直接运行：

```text
RizomUV安装目录\ChineseLauncher\RizomUVChineseLauncher.exe
```

汉化文件统一放在原软件目录的 `ChineseLauncher` 文件夹内，原版 `rizomuv.exe` 不会被替换。

## 汉化范围

当前补丁主要翻译：

- 顶部菜单与菜单命令；
- 工具栏、面板、按钮和选项；
- 工具提示、提示信息和常用操作文字。

这是显示层汉化。补丁只在 RizomUV 运行时改变文字的显示结果，不会把中文写入模型、UV、工程文件、脚本或导出数据，也不会修改许可证逻辑。

少量动态内容、第三方组件或尚未收录的词条仍可能显示英文。部分固定尺寸区域也可能因中文较长而出现拥挤或截断。

## 更新汉化

安装新版前先关闭 RizomUV，然后直接运行新版安装器并再次按向导点击“安装”即可。安装器会安全替换旧版汉化文件；如果安装中途失败，会尽量保留或恢复上一版。

## 卸载与恢复原版

保存工程并退出 RizomUV，在 Windows“已安装的应用”中卸载“RizomUV 中文补丁”。只删除补丁拥有的文件与快捷方式，保留修改过的词典、额外文件及备份，不删除用户工程。产品关联与字体的恢复规则见 [安装指南](docs/installer.md)。

## 常见问题

### 安装后仍然是英文

- 确认启动的是“RizomUV 中文版”，不是原版快捷方式。
- 确认安装目录下存在 `ChineseLauncher` 文件夹。
- 关闭 RizomUV，再使用最新版安装器覆盖安装一次。

### 安装器提示目录无效

请选择直接包含 `rizomuv.exe` 的文件夹。安装器仅接受有效的 64 位 RizomUV 程序目录。

### 提示 RizomUV 正在运行或文件被占用

保存并退出 RizomUV。如果窗口已经关闭，请在任务管理器中确认没有残留的 `rizomuv.exe`，然后重试。

### 启动器提示汉化加载失败

为避免软件处于不完整状态，启动器会停止本次启动。建议重新安装汉化，并检查安全软件是否隔离了 `ChineseLauncher` 中的 DLL。

### 安全软件报警

中文启动器需要把汉化运行时加载到 RizomUV 进程中，少数安全软件可能将这种行为误判为 DLL 注入。请只从可信来源下载；如有疑虑，可检查源码或自行构建。

### 中文不完整、翻译不准确或排版异常

提交反馈时请附上 RizomUV 完整版本号、问题界面的完整截图、复现步骤，以及 `ChineseLauncher\RizomUVChineseRuntime.log`（如果存在）。

## 兼容性说明

- 支持 Windows x64。
- 已实际验证 RizomUV Real Space 2025.0.104。
- 同一 2025.0 系列的小版本通常可以使用，但不保证所有界面完全一致。
- RizomUV 大版本更新后，可能需要等待补丁适配。
- 汉化不参与 UV 展开、优化、排布和导出计算，不会影响这些功能的数据结果。

## 从源码构建

本节仅供开发者使用。需要 Visual Studio 2022、CMake 和 Windows x64 C++ 工具链。

```powershell
cmake -S . -B build/obj -G "Visual Studio 17 2022" -A x64
cmake --build build/obj --config Release --target RizomUVOneClickPackage
```

生成的单文件安装器位于 `dist\RizomUVChineseInstaller.exe`。

```text
source\          运行时、启动器和安装器源码
translations\    正式中文字典
icon\            图标资源
build\           本地构建文件与编译结果
dist\            可发布安装器
```

## 作者、许可与声明

安装器的交互与安全回滚思路参考了 GPL-3.0 项目 [iillya/Toolbag](https://github.com/iillya/Toolbag)，并针对 RizomUV 独立实现。

许可信息见 [LICENSE](LICENSE)。RizomUV 是 Rizom-Lab 的产品；本项目是非官方社区汉化补丁，与 Rizom-Lab 没有隶属关系。

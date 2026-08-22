# RizomUV 简体中文显示层补丁

补丁采用两个相互独立的显示层通道：

- 原生菜单通过 Windows 菜单接口替换。
- 主体界面通过 GDI 绘制和测量 IAT Hook 同步替换。

补丁不修改 `rizomuv.exe`，不改变内部命令、项目数据或许可证逻辑。

## 一键安装与拆卸

发布给普通用户时只需要一个文件：

```text
distribution\安装RizomUV汉化.exe
```

安装器会自动检测默认的 RizomUV 2025.0 目录，也允许手动选择其他安装位置。
点击“一键安装汉化”后会：

- 将启动器、运行时 DLL 和中文词库安装到 `RizomUV 2025.0\RizomUVChinese`；
- 在桌面和开始菜单创建“RizomUV 简体中文版”快捷方式；
- 采用临时目录写入和旧版本回滚，避免更新失败留下半安装状态；
- 保持 `rizomuv.exe` 及 RizomUV 自带文件不变。

重新运行同一个安装器并点击“一键拆卸汉化”，即可删除汉化目录及安装器创建的
快捷方式。若 RizomUV 正在运行导致文件被占用，安装器会停止操作并提示先关闭软件。

当前安装包使用正式词库 `ui_zh-CN.json`。未翻译的长说明也保留在词库中，
以“英文原文 → 英文原文”的方式维持原始显示，后续翻译审核后再替换译文。

正式运行时会将词库中不存在的英文界面文字去重记录到：

```text
%LOCALAPPDATA%\RizomUVChinese\missing_ui_text_进程ID.jsonl
```

记录由后台线程定期更新，不会在 GDI 绘制调用中直接写磁盘。完成一轮界面遍历后，
可将漏词记录重新分类为待翻译目录：

```powershell
python plugin\development\tools\build_translation_catalog.py `
  (Get-ChildItem "$env:LOCALAPPDATA\RizomUVChinese\missing_ui_text_*.jsonl").FullName `
  --existing plugin\translations\ui_zh-CN.json `
  --output plugin\development\translation_catalogs\rizomuv_2025.0.104
```

## 构建

```powershell
cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64
cmake --build build-msvc --config Release
```

生成自包含的一键安装包：

```powershell
cmake --build build-msvc --config Release --target RizomUVOneClickPackage
```

## 汉化样片

构建后运行：

```powershell
build-msvc\Release\RizomUVChineseLauncher.exe
```

启动器挂起启动原版 RizomUV，加载 `RizomUVChineseRuntime.dll` 后恢复主线程。
运行时从同目录的 `ui_zh-CN.json` 加载词库；诊断日志和漏词记录统一写入
`%LOCALAPPDATA%\RizomUVChinese`，避免普通用户无法写入安装目录。

2025.0.104 当前正式词库包含 3821 条映射，其中 3609 条中文译文、212 条需要
保持原样的快捷键、单位和产品名。启动实测中运行时成功加载全部词条，绘制与测量
使用同一份中文结果；动态用户路径、显卡名称和许可证机器指纹不进入正式词库。

## 文件夹规划

```text
plugin/
├─ source/
│  ├─ rizomuv_chinese_installer.cpp  一键安装与拆卸程序
│  ├─ rizomuv_chinese_launcher.cpp   中文启动器
│  ├─ rizomuv_chinese_runtime.cpp    进程内翻译运行时
│  ├─ *_hooks.cpp / *.h              显示层 Hook 模块
│  ├─ translation_dictionary.cpp     运行词库
│  └─ rizomuv_localizer/             模块接口头文件
├─ development/
│  ├─ diagnostics/     界面探测器与文字嗅探器
│  └─ tools/           词库维护与英文文本提取工具
└─ translations/     正式 UTF-8 JSON 运行词库
dependencies/
└─ reference/         原始提取结果与核心程序提取资料
distribution/      可直接分发的单文件安装包
```

一键安装器的产品交互与安全回滚思路参考了 GPL-3.0 项目
[iillya/Toolbag](https://github.com/iillya/Toolbag)，并针对 RizomUV 的独立启动器和
GDI IAT Hook 架构重新实现。

## 使用

先启动 RizomUV，再执行：

```powershell
build-msvc\Release\RizomUVUiProbe.exe --process rizomuv.exe --output rizomuv-ui.json
```

持续观察 15 秒，捕获期间打开的对话框和菜单：

```powershell
build-msvc\Release\RizomUVUiProbe.exe --process rizomuv.exe --watch 15 --output rizomuv-ui.json
```

测试某个标准窗口是否允许显示层替换（3 秒后自动恢复）：

```powershell
build-msvc\Release\RizomUVUiProbe.exe --process rizomuv.exe --test-text "File" "文件"
```

探测报告包含窗口类名、控件层级、可读取文本、菜单文本以及发送
`WM_GETTEXT`/`WM_SETTEXT` 的结果。`--test-text` 只对完全匹配的一个控件执行，
且会自动恢复原文。

## RizomUV 2025.0.104 初步结果

- 捕获到 2321 个窗口/控件、9 个原生菜单项和 278 个 UI Automation 元素。
- File、Help、Open、Save、Save As 和 Auto Save 等菜单文字可直接读取。
- `File -> 文件` 临时替换测试成功，3 秒后成功恢复。
- 大多数主体面板仅暴露为 `wxWindow`/`wxWindowNR`，名称为 `panel`；按钮和标签
  文字没有通过 HWND 或 UI Automation 暴露。

结论：原生菜单可通过菜单接口替换；主体界面已确认经过 GDI 绘制与测量入口。

## 文字绘制链路嗅探器

构建后通过下面的启动器运行 RizomUV：

```powershell
build-msvc\Release\RizomUVSnifferLauncher.exe
```

DLL 会自动捕获启动后 5 秒内的文字。需要捕获弹出窗口或其他面板时，按
`Ctrl+Shift+F12`，随后 3 秒内操作目标界面。结果保存在启动器旁边的
`RizomUV_text_sniffer.jsonl`。嗅探器只记录，不替换文字。

2025.0.104 实测捕获到：

- `GetTextExtentPoint32W`：1452 条唯一文本，包括 Cut、Weld、Unfold、Optimize、
  Pack 以及大量工具提示。
- `ExtTextOutW`：13 条首屏绘制文本，包括 UV Set、Select、Unwrap、Packing、
  Auto Seams 和 Contextual Help。

这证明主体界面使用 GDI 文字测量和绘制。正式运行时已经在相同入口使用统一词库
替换，确保绘制与尺寸测量获得相同中文。

## 英文词条目录

通过嗅探日志生成可重复的分类目录：

```powershell
python plugin\development\tools\build_translation_catalog.py `
  build-msvc\Release\RizomUV_text_sniffer.jsonl `
  --existing plugin\translations\ui_zh-CN.json `
  --output plugin\development\translation_catalogs\rizomuv_2025.0.104
```

生成内容：

- `english_text_catalog.json`：完整原文索引、来源 API、出现次数和当前译文。
- `pending_interface_labels_zh-CN.json`：短标签、按钮和菜单。
- `pending_tooltips_zh-CN.json`：包含标题、快捷键和说明的复合工具提示。
- `pending_descriptions_zh-CN.json`：长句和帮助说明。
- `review_internal_zh-CN.json`：翻译前需要人工确认的内部标识。
- `ignored_shortcuts_numbers.json`：默认不翻译的快捷键与数值。

`translation_catalogs` 保留各轮提取、分类、排除和翻译结果，正式运行时只读取
`plugin\translations\ui_zh-CN.json`。

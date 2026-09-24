# 开发与验证

## 模块职责

| 文件 | 职责 |
| --- | --- |
| `source/hook.cpp` | 词典初始化、运行时生命周期、应用模块发现 |
| `source/gdi_iat_hooks.cpp` | 六个公开 GDI 绘制/测量入口、文字查找与边界保护 |
| `source/rizomuv_localizer/iat_compatibility.h` | 有边界检查的普通/延迟导入遍历 |
| `source/translation_dictionary.cpp` | UTF-8 JSON 校验、一次性加载和只读查询 |
| `source/native_menu_localizer.cpp` | 原生菜单文本及顶部署名窗口 |
| `source/runtime_log.cpp` | 启动诊断、不可写目录回退和日志轮换 |
| `source/launcher.cpp` | 参数传递、创建进程、加载运行时、启动失败处理 |
| `source/inno/` | 宿主目录检查、安装与卸载、包清单与构建 |
| `source/tests/compatibility.cpp` | 导入结构、接口语义、词典和参数转义回归验证 |

保留按职责拆分的小模块；不合并成单个大文件。`translations` 存放正式词典，`build` 存放临时产物，`dist` 仅存放发行安装器。

## 构建

需要 Visual Studio 2022 C++ x64 工具链、CMake、Python 3.9+、Inno Setup 7.1。构建脚本默认使用 VS BuildTools 的标准安装位置；Inno 编译器可通过 `INNO_ISCC` 指定。

```bat
source\build.bat
```

运行时和启动器输出到 `build/out`，安装器输出到 `dist/RizomUVChineseInstaller.exe`。MSVC 运行库静态链接，避免旧版宿主缺少新版 VC 运行库。安装和运行不依赖构建用的 Python。

```bat
cmake --build build/inno-obj --config Release --target RizomUVCompatibilityTests
build\out\RizomUVCompatibilityTests.exe translations/dictionary_zh.json
python source/inno/package.py --test-mode
```

测试安装器位于 `build/inno/test-package`，使用独立的产品 ID 与用户级卸载记录，不创建桌面或开始菜单快捷方式。它仍会写入指定目录，必须使用 `build` 下独立的测试宿主目录。测试卸载会完整删除该目录中的 `ChineseLauncher`。

## 兼容性原则

只接入宿主程序目录内已加载模块的公开 GDI 导入槽，按已解析的函数地址识别，支持缺少名称表、DLL 别名和延迟导入。仅在运行时启动后的前 120 秒内，每两秒检查一次后加载模块和已解析的延迟导入（含前约 10 秒菜单初始化阶段），不覆盖第三方已替换的槽；首次调用可能仍显示英文。120 秒到期后扫描线程退出，已安装 Hook 和只读词典继续工作；此后才加载或解析的接口不再自动补接。

词典在首次接入前加载，此后不可变。绘制回调通过 C++20 字符串视图直接查找并引用词典译文，不复制原文、不读磁盘、不写日志。命中统计在启动诊断完成后停止写入。不要增加每帧全窗口遍历或联网翻译。

字形索引、可改写文字缓冲、逐字符测量数组等无法安全映射的调用保持原语义。原生菜单只在启动阶段短时扫描，并在实际改动时重绘。署名窗口通过事件定位并合并待处理消息，其位置估算仍依赖菜单布局。

不能用“API 存在”代替完整界面测试。新增版本应分别记录静态接口检查、启动加载、实际界面和工程操作验证的状态。

## 词典维护

正式文件为 UTF-8 JSON，使用 `translations` 字符串映射。保留大小写、空格、换行、占位符和 `$$` 分隔符；不得把扫描出的函数符号、库配置路径或脚本内部键直接作为界面词条。

补词需人工结合界面语境审查。新增词条不应改写原有译文，除非已确认错误。构建前运行词典解析验证，并核对安装包清单 `build/inno/manifest.json` 中的载荷哈希。

性能复查及可复现基准见 [性能记录](performance-2026-09-25.md)。

# 开发与验证

## 模块职责

| 文件 | 职责 |
| --- | --- |
| `source/hook.cpp` | 词典初始化、运行时生命周期、应用模块发现 |
| `source/gdi_iat_hooks.cpp` | 六个公开 GDI 绘制/测量入口、文字查找与边界保护 |
| `source/rizomuv_localizer/iat_compatibility.h` | 有边界检查的普通/延迟导入遍历 |
| `source/translation_dictionary.cpp` | UTF-8 JSON 校验、一次性加载和只读查询 |
| `source/tooltip_html.cpp` | 提示 HTML 正文翻译、实体处理和格式保留 |
| `source/tooltip_hook.cpp` | 已验证宿主的提示入口识别、字符串桥接和虚表接入 |
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
cmake --build build/inno-obj --config Release --target RizomUVTooltipTests
build\out\RizomUVTooltipTests.exe translations/dictionary_zh.json "C:\Program Files\Rizom Lab\RizomUV 2027.0\rizomuv.exe"
cmake --build build/inno-obj --config Release --target RizomUVInstallerSupportTests
build\out\RizomUVInstallerSupportTests.exe
python source/inno/package.py --test-mode
```

测试安装器位于 `build/inno/test-package`，使用独立的产品 ID 与用户级卸载记录，不创建桌面或开始菜单快捷方式。它仍会写入指定目录，必须使用 `build` 下独立的测试宿主目录。测试卸载仅删除归属补丁的文件，人工修改的词典和额外文件应保留。

## 兼容性原则

常规文字接入宿主程序目录内已加载模块的公开 GDI 导入槽，按已解析的函数地址识别，支持缺少名称表、DLL 别名和延迟导入。仅在运行时启动后的前 120 秒内，每两秒检查一次后加载模块和已解析的延迟导入（含前约 10 秒菜单初始化阶段），不覆盖第三方已替换的槽；首次调用可能仍显示英文。120 秒到期后扫描线程退出，已安装 Hook 和只读词典继续工作；此后才加载或解析的接口不再自动补接。

2027.0.598 的 HTML 提示在独立入口处理：只修改该提示子类的 SetPage 虚表槽，不修改通用 HTML 窗口、系统模块或指令。内部 ABI 接入必须同时通过完整宿主文件 SHA-256、PE 架构/时间戳/映像大小、类型名、代码及槽位校验；其他构建保留原入口，不猜测偏移。译文通过宿主复制构造函数创建临时字符串，并由宿主清理；不改写原输入，不混用 CRT 分配和释放。配置溯源与边界见 [提示路径记录](tooltip-hook-2026-10-14.md)。

词典在首次接入前加载，此后不可变。绘制回调通过 C++20 字符串视图直接查找并引用词典译文，不复制原文、不读磁盘、不写日志。命中统计在启动诊断完成后停止写入。不要增加每帧全窗口遍历或联网翻译。

HTML 规范化词表引用正式词典的不可变译文，词典必须比翻译器存活更久。每线程只缓存最近一段提示，输入和输出各限制为 16,384 字符；初始化新词表时使用新的修订标记使旧缓存失效。大提示不入缓存，超过 131,072 字符的输入或输出保留原文。缓存仅减少重复 SetPage 排版调用的翻译处理，宿主仍正常执行每次排版。

各 CMake 目标统一警告、UTF-8、字符集和静态运行库配置。可用 `-DRIZOMUV_OUTPUT_DIR=绝对路径` 为 Debug 构建设置独立输出目录，避免覆盖 `build/out` 的正式 Release 载荷。安装打包先复制并校验固定载荷快照，输出权限处理成功后才原子替换旧安装器。

字形索引、可改写文字缓冲、逐字符测量数组等无法安全映射的调用保持原语义。原生菜单只在启动阶段短时扫描，并在实际改动时重绘。署名窗口通过事件定位并合并待处理消息，其位置估算仍依赖菜单布局。

署名窗口选择可调整大小的主框架，排除启动画面和浮动工具窗；无边框全屏另以完整显示器范围和 RizomUV 标题识别。链接先创建为隐藏窗口，成功绘制两块图层后才显示并缓存位置。首次绘制未成功时仅每 500 毫秒重试，最多 5 秒，成功即停止；之后仍按主窗口事件定位，不增加常驻轮询。运行日志区分线程启动、主窗口绑定、图层失败及实际显示状态。署名回归使用测试进程自己的隐藏窗口，不启动 RizomUV 或打开署名网址。

不能用“API 存在”代替完整界面测试。新增版本应分别记录静态接口检查、启动加载、实际界面和工程操作验证的状态。

## 词典维护

正式文件为 UTF-8 JSON，使用 `translations` 字符串映射。保留大小写、空格、换行、占位符和 `$$` 分隔符；不得把扫描出的函数符号、库配置路径或脚本内部键直接作为界面词条。

补词需人工结合界面语境审查。新增词条不应改写原有译文，除非已确认错误。构建前运行词典解析验证，并核对安装包清单 `build/inno/manifest.json` 中的载荷哈希。

性能复查及可复现基准见 [性能记录](performance-2026-09-25.md)。

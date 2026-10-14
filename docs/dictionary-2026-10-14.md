# 界面词条补充记录

2026-10-14 根据用户截图和本机 2027.0.598 程序中的文字补齐标签。新增 23 条、修订 9 条，正式词典由 5,213 条增至 5,236 条。

| 截图中的原文 | 译文 |
| --- | --- |
| Islands and groups | UV 岛与组 |
| Folder | 文件夹 |
| Interval | 间隔 |
| Omniverse log level | Omniverse 日志级别 |
| Multithreading | 多线程 |
| glTF rebuild quads at import | 导入 glTF 时重建四边形 |
| Explorer | 数据浏览器 |
| Log | 日志 |
| Ambient | 环境光 |
| 3D space borders | 3D 网格边界 |
| Intensity | 强度 |

同组补充高亮、叠加显示、悬停、大小选项、选择转换和选择模式行为选项，以及命令和颜色库标签。组合选项 `16|Medium`、`32|Huge`、`Both|Click and hotkey`、`Click|Toolbar click` 只收录实际显示的标签，保留程序选项标识符。快捷键由菜单翻译路径原样保留，无需为每个快捷键另建词条。

统一数据浏览器的名称、带省略号菜单项和相关提示，Windows 文件资源管理器相关词条仍使用“资源管理器”。统一 3D 网格边界的颜色与宽度译法；这与 UV 接缝含义不同。

吸附提示 `Use the viewport zoom to determine the snapping distance.` 的两个尾随空格变体原有译文不同，HTML 归一化后会被判定为冲突而跳过翻译。现统一为“根据视口缩放程度确定吸附距离。”，保留两个原文键。

验证使用严格词典加载、原生兼容性回归和 HTML 提示翻译回归，并核对打包清单中的词典哈希。本批仅补充词典和说明，复用代码审查后的运行时与启动器；全部修改归入正式版 1.0.5。未启动或操作 RizomUV，实际界面显示需更新后核对。

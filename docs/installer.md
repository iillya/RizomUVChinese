# 安装与卸载

面向用户的完整说明已统一到 [README](../README.md#安装与启动)。

- 安装：退出 RizomUV，运行 `dist/RizomUVChineseInstaller.exe`，选择直接包含 x64 `rizomuv.exe` 的目录。
- 启动：使用“RizomUV 中文版”快捷方式，或该目录下 `ChineseLauncher/RizomUVChineseLauncher.exe`。
- 更新：关闭软件后覆盖安装。安装器会替换正式词典，请先把自定义内容备份到 `ChineseLauncher` 以外。
- 卸载：通过 Windows“已安装的应用”卸载。整个 `ChineseLauncher` 文件夹都会删除，包括额外文件、修改过的词典和备份。
- 迁移：同一安装器只管理一个软件目录，更换位置前先卸载原补丁。

补丁不接管工程文件关联，不安装 Qt 或字体组件。使用和卸载均不需要 Python、PowerShell 脚本或开发环境。构建步骤见 [开发文档](development.md)。

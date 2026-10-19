# 参与贡献

- 保持 x86/x64 的导出接口、错误码和宏行为一致。
- 修改运行时行为时补充对应测试；模块和 ABI 变动需运行真实安装器测试。
- 使用中文说明和简短注释；公共接口变更同步更新 `docs/api.md`。
- Git 提交标题和正文统一使用英文，标题遵循 Conventional Commits，例如 `feat: add file access permissions`。
- 更新依赖时固定提交、检查许可证，重新执行两个架构的 Debug/Release 测试。
- 提交问题时附上 Windows、NSIS、插件版本及最小 `.nsi`/JS 复现。

实现分层：`src/plugin.cpp` 负责 NSIS ABI 和字符串传输；`src/runtime.cpp` 负责运行时、调用及资源生命周期；`src/paths.cpp` 负责路径和文件读取。

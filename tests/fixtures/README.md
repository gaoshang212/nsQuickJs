# 测试夹具说明

本目录的 JS/MJS 文件由原生测试和真实 NSIS 安装器共同使用。普通脚本中的注释说明测试意图；以下文件依赖精确的字节内容，请勿自动格式化或转码：

- `bom.js`：以 UTF-8 BOM 开头，返回字符串“带 BOM 的脚本”，验证读取器去除 BOM 后正常求值。
- `invalid-utf8.js`：包含故意构造的无效 UTF-8 字节，验证读取器返回 `E_ENCODING`；保持 `.gitattributes` 中的 `-text` 设置，不在文件内部添加注释。

`pending*.mjs`、`timeout.mjs` 和错误导入夹具故意无法正常完成。请保留这些行为；`pending-held.mjs` 的全局引用还用于验证模块生命周期补丁和卸载清理。

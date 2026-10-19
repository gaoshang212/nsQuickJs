// 普通脚本通过宿主授权的 fs API 读写 rootDir 内文件，结果返回给 EvalFile。
// 覆盖示例自有文件，再读取 JSON，最后删除，避免重复演示留下数据。
fs.writeText('file-demo.json', JSON.stringify({ message: '你好，文件！', total: 42 }));
// 显式存入全局对象，允许重复 EvalFile，避免顶层 const 重复声明。
globalThis.fileDemoResult = JSON.parse(fs.readText('file-demo.json'));
fs.remove('file-demo.json');
fileDemoResult;

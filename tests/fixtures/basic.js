// 全局计数器用于验证多次 EvalFile 在同一个上下文中执行。
globalThis.counter = (globalThis.counter || 0) + 1;
// 全局加法函数，用于验证空模块别名下的 JSON 参数调用。
globalThis.add = (a, b) => a + b;
// 最后一个表达式作为 EvalFile 的返回值。
counter;

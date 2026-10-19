// 全局求值次数用于确认同一路径经不同别名导入时仍命中模块缓存。
globalThis.moduleLoads = (globalThis.moduleLoads || 0) + 1;
// 返回两个参数之和，验证模块函数调用及 JSON 参数顺序。
export function add(a, b) { return a + b; }
// 返回模块求值次数，重复导入同一模块后仍应为 1。
export const loads = () => globalThis.moduleLoads;
// 原样返回参数，验证对象和 Unicode 在 JSON 往返中保持一致。
export const echo = value => value;
// 抛出已知错误，验证返回堆栈包含此模块文件名。
export const fail = () => { throw new Error('模块错误'); };

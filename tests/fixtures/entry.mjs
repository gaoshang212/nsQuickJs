// 入口夹具同时覆盖相对导入、异步导出函数和顶层 await。
import { add } from './math.mjs';
// 以 Promise 返回参数之和，验证 Call 自动推进微任务。
export const compute = async (a, b) => await Promise.resolve(add(a, b));
// 顶层 await 完成后才可用的导出值，期望为 42。
export const ready = await Promise.resolve(42);
// 读取顶层初始化结果，验证 LoadModule 已等待求值完成。
export const getReady = () => ready;

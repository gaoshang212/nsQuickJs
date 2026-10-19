// 循环依赖的另一端；只在函数调用时读取对方的导出。
import { a } from './cycle-a.mjs';
// 返回本模块标识，供 cycle-a.both 拼接结果。
export function b() { return 'b'; }
// 通过反向依赖调用 a，保留双向导入关系。
export function other() { return a(); }

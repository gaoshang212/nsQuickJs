// 与 cycle-b.mjs 互相导入，验证引擎正常链接循环依赖。
import { b } from './cycle-b.mjs';
// 返回本模块标识，供循环依赖的另一端调用。
export function a() { return 'a'; }
// 同时访问两端导出，期望返回 "ab"。
export function both() { return a() + b(); }

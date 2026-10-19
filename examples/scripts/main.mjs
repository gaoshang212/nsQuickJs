// 示例入口：导入相对模块，演示共享状态与异步返回值。
import { sum } from './math.mjs';

// 模块内累计调用次数；同一上下文重复 Call 时保持，不随函数返回而重置。
let calls = 0;
// 计算 a、b 之和并返回 total/calls 对象，供 NSIS 接收 JSON 结果。
export async function calculate(a, b) {
    calls += 1;
    return { total: await Promise.resolve(sum(a, b)), calls };
}
// 主动抛出错误，演示 NSIS 如何读取 E_JS 和带堆栈的错误说明。
export function fail() { throw new Error('示例异常'); }

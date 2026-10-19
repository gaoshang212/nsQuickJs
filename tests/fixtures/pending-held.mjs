// 将永不结算的 Promise 保留在全局，重现异步闭包与模块定义的销毁依赖。
// 空执行器故意不调用 resolve/reject，使队列耗尽后返回 E_PENDING。
globalThis.heldPromise = new Promise(() => {});
await globalThis.heldPromise;

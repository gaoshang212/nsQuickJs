// 顶层 await 直接拒绝，验证 LoadModule 返回 E_JS 和拒绝原因。
await Promise.reject(new Error('模块拒绝'));

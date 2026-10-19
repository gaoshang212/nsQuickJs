// 先进入异步续体再无限循环，验证模块异步求值仍受同一超时限制。
await Promise.resolve();
while (true) {}

// 空执行器永不结算；验证顶层 await 无可推进任务时返回 E_PENDING。
await new Promise(() => {});

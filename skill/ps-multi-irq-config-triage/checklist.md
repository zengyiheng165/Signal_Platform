# checklist — 多中断配置检查

> 逐项勾选；任何一项无法确认就写"未知"，不要用"应该/大概"补位。

## 0. PL 事实（**前置门禁，必须先确认；拿不到就先问用户**）

- [ ] 已拿到 `.hwh` / `.xsa`，或用户已口述 PL 中断情况 ________（来源：hwh / 用户）
- [ ] 中断源有几路 ________
- [ ] 每路连到哪个 GIC ID ________
- [ ] 是否有中断汇聚（`xlconcat`）________
- [ ] UART 中断号 ________
- [ ] 以上有缺项 ⇒ 已向用户列出问题清单，缺项记为"未知"（**不允许默认或猜**）

## A. 事实确认（从源码抄，不凭印象）

- [ ] GIC 设备 ID ________（`INTC_DEVICE_ID` 的定义）
- [ ] 各路中断号宏 ________（`RX_INTR_ID0/1`、`TX_INTR_ID0/1`、`UART_INT_IRQ_ID`）
- [ ] 每路对应的中断服务函数 ________
- [ ] 优先级与触发类型实参 ________
- [ ] 中断服务函数的私有数据实参 ________

## B. GIC 初始化

- [ ] `XScuGic_LookupConfig` 结果判了 NULL
- [ ] `XScuGic_CfgInitialize` 判了 `XST_SUCCESS`
- [ ] **每一路**都调用了 `XScuGic_SetPriorityTriggerType`（含 UART）

## C. 中断服务函数注册

- [ ] 每一路都有 `XScuGic_Connect`
- [ ] 每次 `Connect` 都判了 `XST_SUCCESS`
- [ ] `Connect` 次数 = `XScuGic_Enable` 次数 = 实际路数

## D. 异常向量

- [ ] `Xil_ExceptionInit()` 有
- [ ] `Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT, …)` 有
- [ ] `Xil_ExceptionEnable()` 有

## E. 外设侧使能顺序

- [ ] 先 `XAxiDma_IntrDisable`（清上电挂起）
- [ ] 再 `XScuGic_Enable`（逐路）
- [ ] 最后 `XAxiDma_IntrEnable` / `XUartPs_SetInterruptMask`

## F. 上板验收

- [ ] 每个中断服务函数里有计数变量，主循环每秒打印
- [ ] 触发一次各计数只 +1（一路疯涨 = 没清中断源）
- [ ] 某路一直为 0 ⇒ 回到 B / C / E 查那一路

## G. 红牌（出现即停）

- [ ] 只 `Connect` 不 `XScuGic_Enable`
- [ ] 只给 DMA 设优先级、UART 漏设
- [ ] 漏 `Xil_ExceptionEnable()`
- [ ] `UartPs` 只 Connect、没设中断掩码
- [ ] 一次改多处
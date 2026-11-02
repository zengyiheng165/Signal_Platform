# 决策树 — 多中断配置

> 先判"**全部不来**"还是"**某一路不来**"：全部不来查异常挂载与 GIC 初始化；某一路查那一路的 Connect / Enable / 中断号。

## 前置门禁：PL 是否已知？

1. 有 `.hwh` / `xparameters_ps.h` ⇒ 直接读中断路数与 GIC ID，再进下面的现象分支
2. 没有，但用户能口述 ⇒ 按 `SKILL.md` Step 0 的表逐项问清楚
3. 用户也说不清 ⇒ 结论只能写"未知"，**不要猜中断号与路数**

## 现象 1：所有中断都不来

1. `Xil_ExceptionInit()` / `Xil_ExceptionRegisterHandler` / `Xil_ExceptionEnable()` 三句是否齐全
2. `XScuGic_CfgInitialize` 是否成功
3. `XScuGic_Enable` 是否真的被调用（不是只 Connect）
4. 外设侧是否使能（`XAxiDma_IntrEnable`、`XUartPs_SetInterruptMask`）

## 现象 2：只有某一路不来

1. 那一路 `XScuGic_Connect` 是否有
2. 那一路 `XScuGic_Enable` 是否有
3. 那一路的中断号是否与 BD 连线一致
4. 那一路是否漏了 `SetPriorityTriggerType`

## 现象 3：中断进了但主循环没反应

1. 中断服务函数是否真的被调用（在里面加计数或打印）
2. 主循环读的标志是否真的在更新（在中断服务函数里加计数，用调试器或打印确认）
3. 标志是否在主循环里被清零得太早

## 现象 4：一路中断反复进入

1. 中断服务函数里是否清了外设侧中断
2. 是否只在 GIC 侧 Ack 而没清外设
3. 中断源是否一直保持有效电平

## 现象 5：UART 中断不触发

1. 是否 `XUartPs_SetInterruptMask`（只 Connect 不够）
2. UART 那一路是否设了优先级/触发类型
3. 串口是否真的产生了需要中的事件

## 通用规则

- 先判"全部不来 / 某一路不来"
- 一次只改一处，改完回读 + 复跑检查
- 结论必须能指到代码行
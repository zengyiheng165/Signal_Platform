# 三个可复现的失败模式（改一行即可验证）

> 都基于本模板的 `intr.c`：注释掉某一处，观察现象，再恢复。用于验证本 Skill 的判据是否成立。

## 例子 1 - 注释掉 `Xil_ExceptionEnable()`

- **做法**：把 `intr.c` 里 `Xil_ExceptionEnable();` 注释掉，重新编译下载。
- **现象**：**所有中断都不进**（串口、DMA 全无响应），但程序能跑。
- **判据**：异常向量三句缺一句，所有中断都不进。
- **恢复**：取消注释，中断恢复正常。

## 例子 2 - 注释掉 UART 那行 `SetPriorityTriggerType`

- **做法**：把 4 路 DMA 中任一行的 `XScuGic_SetPriorityTriggerType(…, RX_INTR_ID0, 0xA0, 0x3);` 注释掉。
- **现象**：**那一路 DMA 中断偶发不来**，其余各路正常。
- **判据**：漏设优先级/触发类型的那一路会偶发不来（UART 在模板里就是这样，见 `SKILL.md` 来源第 1 条）。
- **恢复**：取消注释，该路恢复正常。

## 例子 3 - 注释掉某一路 `XScuGic_Enable`

- **做法**：把 `XScuGic_Enable(Intc, RX_INTR_ID0);` 注释掉（Connect 保留）。
- **现象**：**只有那一路 DMA 中断不来**，其余各路正常。
- **判据**：只 Connect 不 Enable ⇒ 该路永不响应。
- **恢复**：取消注释，该路恢复正常。
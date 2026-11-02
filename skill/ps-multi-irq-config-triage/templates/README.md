# 工程模板（PS 端多中断配置）

> `intr.h` / `intr.c` 是固定模板，不改动。本模板只负责 **GIC 初始化与中断使能**；
> 中断服务函数本身在 `dma.c` / `uart.c` 里，不在本模板内。

## 文件分工

| 文件 | 内容 | 改动方式 |
|---|---|---|
| `intr.h` | 声明 `intr_init` / `disable_intr_system`，包含所需头文件 | 只加函数或改包含 |
| `intr.c` | `intr_init`（GIC 初始化 → 优先级/触发类型 → Connect → 异常挂载 → 外设使能）、`disable_intr_system` | 只改中断号与路数 |
| `README.md` | 本文件 | — |

## 填空清单

| # | 位置 | 填什么 | 从哪取 |
|---|---|---|---|
| 1 | `INTC_DEVICE_ID` | GIC 设备 ID（模板是 `XPAR_SCUGIC_SINGLE_DEVICE_ID`）| `xparameters_ps.h` |
| 2 | `RX_INTR_ID0/1`、`TX_INTR_ID0/1` | 4 路 DMA 中断的 GIC ID | BD 连线 / `XPS_FPGA0..3_INT_ID` |
| 3 | `UART_INT_IRQ_ID` | 串口中断号 | `xparameters_ps.h` |
| 4 | `SetPriorityTriggerType` 实参 | 优先级（模板 0xA0）、触发类型（模板 0x3 = 上升沿）| 中断源要求 |
| 5 | Connect 路数 | = 实际中断源路数 | Block Design |

> **中断号来自 PL**：各路 ID 由 Block Design 连线决定；拿不到 `.hwh` 且用户未告知 ⇒ **先问用户**，不要默认。

## 固定顺序（模板里的实际顺序）

```
1. XScuGic_LookupConfig
2. XScuGic_CfgInitialize
3. XScuGic_SetPriorityTriggerType   ← 每一路都要设
4. XScuGic_Connect                  ← 每一路一次，判返回值
5. Xil_ExceptionInit / Xil_ExceptionRegisterHandler / Xil_ExceptionEnable
6. XAxiDma_IntrDisable              ← 先清掉上电挂起
7. XScuGic_Enable                   ← GIC 侧逐路使能
8. XAxiDma_IntrEnable + XUartPs_SetInterruptMask   ← 外设侧使能
```

## 与常见错误写法的差异

| 点 | 常见写法 | 正确的做法 | 后果 |
|---|---|---|---|
| GIC 使能 | 只 `Connect` | `Connect` + `XScuGic_Enable` | 该路永不响应 |
| 优先级/触发类型 | 只给 DMA 设 | **每一路都设**（含 UART） | 漏掉的那路偶发不来 |
| 异常向量 | 只 `RegisterHandler` | 再加 `Xil_ExceptionEnable()` | 所有中断都不进 |
| UART 使能 | 只 Connect | 还要 `XUartPs_SetInterruptMask` | 串口中断不触发 |
| 关中断范围 | 以为全关 | `disable_intr_system` 只断开传入的 ID | 其余路仍在响应 |

## 三个可复现的失败模式（改一行即可复现）

1. **注释掉 `Xil_ExceptionEnable()`** ⇒ 所有中断都不进；恢复后正常。
2. **注释掉 UART 那行 `XScuGic_SetPriorityTriggerType`** ⇒ 串口中断偶发不响应；恢复后正常。
3. **注释掉 `XScuGic_Enable(Intc, RX_INTR_ID0)`** ⇒ 只有那一路 DMA 中断不来；恢复后正常。

## 上板前自查

- [ ] `Connect` 次数 = `XScuGic_Enable` 次数 = 实际中断路数
- [ ] 每一路都有 `SetPriorityTriggerType`（包括 UART）
- [ ] `Xil_ExceptionInit` / `RegisterHandler` / `Enable` 三句齐全
- [ ] UART 有 `XUartPs_SetInterruptMask`
- [ ] 每路中断服务函数里都有清中断动作

## 本模板不含什么

- 中断服务函数本体（在 `dma.c` / `uart.c`）
- DMA 传输长度、缓存同步、旧帧丢弃
- 中断嵌套、优先级抢占、多核中断分发
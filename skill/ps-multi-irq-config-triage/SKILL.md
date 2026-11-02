---
name: ps-multi-irq-config-triage
description: >
  检查PS端多路中断的配置：GIC初始化、优先级与触发类型、中断服务函数注册、
  异常向量挂载、外设侧使能顺序。
  当用户提及某一路中断不触发、所有中断都不来、中断进了但主循环没反应、中断反复进入时使用。
  只覆盖PS端的中断控制器与外设使能配置。
allowed-tools: Read, Grep, Glob, Bash(python:*), Bash(grep:*)
---

# 多中断配置检查 Skill

多路中断同时存在时，"中断不生效"往往是**某一步漏了或顺序错了**。本 Skill 按模板里的固定流程
逐项核对，把结论落到"哪一路、漏了哪个调用、顺序该怎样"。
**只读检查**：不改中断服务函数的业务逻辑、不改 DMA 长度与缓存策略。

## 适用范围

- 任何"GIC + 多路外设中断（AXI DMA 各通道、UART 等）"的裸机工程
- 现象：某一路中断不触发、所有中断都不来、中断进了但主循环没反应、中断反复进入

## 失效条件

- PL 端的改动（RTL / Block Design / 约束）：PL 事实只作为输入，改 PL 不在本 Skill 内
- DMA 传输长度、缓存同步、旧帧丢弃（那些属于另一类问题）
- 中断嵌套与优先级抢占的总线级设计、多核（CPU1）中断分发
- 裸机以外的操作系统中断框架

## 检查流程

### Step 0: 先拿到 PL 侧事实（前置门禁，未确认不得往下配）

**中断号由 PL 侧连线决定**，不能凭经验默认：

| PL 侧事实 | 决定 PS 侧的什么 | 从哪里取 |
|---|---|---|
| AXI DMA / 自定义 IP 引出了几路中断 | 需要 `XScuGic_Connect` 几次、几路要 `XScuGic_Enable` | `.hwh` / Block Design 的中断连线；或 Vivado 里看 `xlconcat`/中断汇聚 |
| 每路中断连到哪个 GIC ID | `RX_INTR_ID0` / `RX_INTR_ID1` / `TX_INTR_ID0` / `TX_INTR_ID1` 这些宏取什么值 | `.hwh` / `xparameters_ps.h`（`XPS_FPGA0_INT_ID` 等）|
| 是否有中断汇聚（`xlconcat`） | 多个源共用一个 ID 还是各自独立 | Block Design |
| UART 中断号 | `UART_INT_IRQ_ID` | PS 端固定（`xparameters_ps.h`）|

**取法优先级**：`.hwh` / `xparameters_ps.h` > 用户在 Vivado 里查 > 用户口述。
**都拿不到 ⇒ 停下来先问用户**，不允许默认中断号，也不允许按"应该是这么多路"推测。

### Step 1: 确认事实（从源码抄，不凭印象）

| 项 | 从哪里取 |
|---|---|
| GIC 设备 ID | `INTC_DEVICE_ID` 的定义（模板里是 `XPAR_SCUGIC_SINGLE_DEVICE_ID`）|
| 各路中断号宏 | `intr.h` / `dma.h` 里的 `RX_INTR_ID0/1`、`TX_INTR_ID0/1`、`UART_INT_IRQ_ID` |
| 中断服务函数 | 每路对应哪个函数（`RxIntrHandler0/1`、`TxIntrHandler0/1`、`uart_intr_handler`）|
| 优先级与触发类型 | `XScuGic_SetPriorityTriggerType` 的实参（模板里 4 路 DMA 都设了；UART 是否设了？）|
| 外设侧使能 | UART 的 `XUartPs_SetInterruptMask`、DMA 的 `XAxiDma_IntrEnable` |

```bash
grep -rn "XScuGic_" --include=*.c --include=*.h .
grep -rn "INTR_ID\|INT_IRQ_ID" --include=*.h .
grep -rn "Xil_Exception" --include=*.c .
```

### Step 2: GIC 初始化与优先级/触发类型

模板里的固定四步（缺一步后面对全不成立）：

```c
intc_cfg = XScuGic_LookupConfig(INTC_DEVICE_ID);          /* 1. 取配置 */
if (NULL == intc_cfg) { return XST_FAILURE; }

status = XScuGic_CfgInitialize(Intc, intc_cfg, intc_cfg->CpuBaseAddress);  /* 2. 初始化 */
if (status != XST_SUCCESS) { return XST_FAILURE; }

/* 3. 每路都要设优先级与触发类型（漏了就用默认值） */
XScuGic_SetPriorityTriggerType(Intc, RX_INTR_ID0, 0xA0, 0x3);
XScuGic_SetPriorityTriggerType(Intc, RX_INTR_ID1, 0xA0, 0x3);
XScuGic_SetPriorityTriggerType(Intc, TX_INTR_ID0, 0xA0, 0x3);
XScuGic_SetPriorityTriggerType(Intc, TX_INTR_ID1, 0xA0, 0x3);
XScuGic_SetPriorityTriggerType(Intc, UART_INT_IRQ_ID, 0xA0, 0x3);   /* 模板里缺这一行 ⇒ 见来源第 1 条 */
```

判据：

- **每一路中断都要设**优先级与触发类型（漏一路，那一路就可能偶发不来）
- `0x3` = 上升沿触发（具体取值看中断源要求）

### Step 3: 注册中断服务函数（每路一次）

```c
status = XScuGic_Connect(Intc, RX_INTR_ID0, (Xil_InterruptHandler) RxIntrHandler0, AxiDma0);
if (status != XST_SUCCESS) { return status; }
/* 其余各路同理：RX_INTR_ID1 / TX_INTR_ID0 / TX_INTR_ID1 / UART_INT_IRQ_ID */
```

判据：

- **Connect 的次数 = 使能的次数 = 实际中断源的路数**，三者必须一致
- 每路 Connect 都要判 `XST_SUCCESS`
- 第 4 个实参是传给中断服务函数的私有数据（模板传的是 `AxiDma0/1`、`Uart_Ps`）

### Step 4: 挂载异常向量（只有一处，全局一次）

```c
Xil_ExceptionInit();
Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT,
        (Xil_ExceptionHandler) XScuGic_InterruptHandler, (void *) Intc);
Xil_ExceptionEnable();
```

判据：

- 这三句缺任何一句 ⇒ **所有中断都不会进**（最容易漏的是 `Xil_ExceptionEnable()`）
- 只在初始化里做一次，不要每路重复注册

### Step 5: 外设侧使能（先关、再开 GIC、最后开外设）

```c
/* 1. 先把 DMA 各路中断关掉（清掉上电时的历史挂起） */
XAxiDma_IntrDisable(AxiDma0, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DEVICE_TO_DMA);
XAxiDma_IntrDisable(AxiDma1, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DEVICE_TO_DMA);
XAxiDma_IntrDisable(AxiDma0, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DMA_TO_DEVICE);
XAxiDma_IntrDisable(AxiDma1, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DMA_TO_DEVICE);

/* 2. 打开 GIC 侧（每一路） */
XScuGic_Enable(Intc, UART_INT_IRQ_ID);
XScuGic_Enable(Intc, RX_INTR_ID0);
XScuGic_Enable(Intc, RX_INTR_ID1);
XScuGic_Enable(Intc, TX_INTR_ID0);
XScuGic_Enable(Intc, TX_INTR_ID1);

/* 3. 打开外设侧（DMA / UART） */
XAxiDma_IntrEnable(AxiDma0, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DEVICE_TO_DMA);
XAxiDma_IntrEnable(AxiDma1, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DEVICE_TO_DMA);
XAxiDma_IntrEnable(AxiDma0, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DMA_TO_DEVICE);
XAxiDma_IntrEnable(AxiDma1, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DMA_TO_DEVICE);

XUartPs_SetInterruptMask(Uart_Ps, XUARTPS_IXR_RXOVR);   /* UART 侧的使能掩码 */
```

判据：

- 顺序：**先关外设中断 → 开 GIC → 再开外设中断**；反了会把上电挂起当新中断
- UART 必须设 `XUartPs_SetInterruptMask`，只 Connect + GIC Enable 是不会进中断的
- `IntrDisable` 不需要写两遍（模板里出现了两次，属于冗余，核对时留意）

### Step 6: 上板验证（给出可复现的数字）

- 每个中断服务函数里对一个计数变量累加，主循环每秒打印一次各路计数
- 触发一次应只加 1；若一路疯涨 ⇒ 中断源没清
- 某路一直为 0 ⇒ 回到 Step 2/3/5 查那一路
- 关中断时用 `disable_intr_system`（模板里只 Disconnect 两个 ID，其余路仍在，注意范围）

### 输出格式

```text
=== 多中断配置检查报告 ===
[✓/✗] [PL]      中断路数与 GIC ID 的来源（hwh / 用户口述）
[✓/✗] [GIC]     LookupConfig / CfgInitialize / SetPriorityTriggerType 是否每路都设
[✓/✗] [Connect] Connect 次数 = Enable 次数 = 实际路数
[✓/✗] [异常]    Xil_ExceptionInit / RegisterHandler / Enable 三句齐全
[✓/✗] [外设]    先 IntrDisable → GIC Enable → IntrEnable / XUartPs_SetInterruptMask
[✓/✗] [清源]    每路中断服务函数是否有清中断动作
=== 总结 ===
通过: X/6   结论: PASS / 需修复（列出最小修改点）
未知: 证据不足的项（写明缺什么）
```

## ⚠️ Gotchas

- **编译通过 ≠ 中断能进**：`Xil_ExceptionEnable()` 漏了照样编译通过。
- **只 Connect 不 Enable**：GIC 侧没使能，中断永远不响应。
- **漏设优先级/触发类型**：那一路可能偶发不来，且很难复现。
- **Connect 与 Enable 路数不一致**：多注册少使能（或反之）最容易漏。
- **UART 只 Connect 没设 `XUartPs_SetInterruptMask`**：串口中断不会进。
- **重复 `IntrDisable`**：不影响功能但会掩盖"到底哪一步生效"，核对时按实际顺序看。
- **诊断阶段不要改业务逻辑**：一动手原始现象就没了。
- **不要凭经验推中断号**：中断号来自 BD 连线或 `xparameters_ps.h`。
- **PL 侧不在本 Skill 内**：接线问题要在 PL 侧解决。
- **拿不到上板证据时**：结论写"未知"，不要用"应该/大概"补位。

## 来源：失败与观察

下列现象来自实际调试，**不绑定具体板卡或数值**：

1. **UART 中断没设优先级/触发类型**：只对 4 路 DMA 设了 `SetPriorityTriggerType`，串口那一路漏掉 ⇒ 串口中断偶发不响应。
2. **`IntrDisable` 重复出现**：模板里同一组关中断写了两遍 ⇒ 顺序被掩盖，排查时容易误判"使能没生效"。
3. **`disable_intr_system` 只断开两路**：调用后其余中断仍在响应 ⇒ 关中断的范围与预期不符。

## 配套文件

- `checklist.md`：事实确认、GIC / Connect / 异常 / 外设使能逐项核对、上板验收、红牌。
- `decision-tree.md`：现象 → 该取什么证据 → 根因候选。
- `examples/README.md`：三个真实排查例子。
- `scripts/irq_config_check.py`：静态检查（Connect 与 Enable 配对、缺 `SetPriorityTriggerType`、缺异常挂载、重复 Disable 等）。
- `templates/`：`intr.h` / `intr.c` + 填空说明。
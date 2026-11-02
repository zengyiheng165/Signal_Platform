---
name: ps-axi-dma-capture-guard
description: >
  检查PS端AXI DMA采集/发送链路：传输长度与点数是否一致、缓存同步位置、
  提交与等待顺序、超时后是否整帧丢弃、是否只取本帧有效点数。
  当用户提及DMA采不到数据、每帧超时、数据看起来不变、SimpleTransfer返回失败时使用。
  只覆盖PS端的DMA配置与数据搬运。
allowed-tools: Read, Grep, Glob, Bash(python:*), Bash(grep:*)
---

# AXI DMA 采集链路检查 Skill

DMA 通路出问题时，现象往往只有一句"采不到数据"。本 Skill 按固定顺序核对 6 个必要条件，
把结论落到"具体哪个长度/哪一处缓存/哪一步顺序"，并给出最小修改点。
**只读检查**：不改算法、不改中断配置、不重写业务逻辑。

## 适用范围

- 任何"IP → AXI DMA(S2MM) → 内存"或"内存 → AXI DMA(MM2S) → IP"的裸机链路
- 现象：采不到数据、每帧超时、数据看起来不变、`XAxiDma_SimpleTransfer` 返回失败

## 失效条件

- Scatter-Gather 描述符管理（本 Skill 只覆盖 Simple / Direct Register 模式）
- **中断控制器（GIC）的注册与使能**、中断优先级与触发类型 —— 这些配置不在本 Skill 内，
  本 Skill 只要求"标志位能被主循环观察到"
- IP 侧数据格式定义、多通道带宽规划、算法正确性
- PL 端 RTL / Block Design / 约束，以及显示通路 —— 不在本 Skill 内。

## 检查流程

### Step 0: 先拿到 PL 侧事实（前置门禁，未确认不得往下配）

**DMA 的模式与传输点数由 PL 决定**，不能凭空假设：

| PL 侧事实 | 决定 PS 侧的什么 | 从哪里取 |
|---|---|---|
| DMA 实例是否存在、是否 SG 模式 | 用 `XAxiDma_SimpleTransfer` 还是描述符链 | 看该 AXI DMA 实例的参数（`.hwh` 里 `MODTYPE="axi_dma"` 那段），或 Vivado 的 Re-Customize IP 页面 |
| S2MM / MM2S 哪一个使能 | 方向：`XAXIDMA_DEVICE_TO_DMA` / `XAXIDMA_DMA_TO_DEVICE` | 同上（看通道使能勾选项）|
| 流位宽（`TDATA`） | 每点字节数、对齐要求 | 同上（看 Stream Data Width）|
| 源 IP 每次产生的数据量（包长 / 点数） | **传输点数**（`len = 点数 × 每点字节数`）| 自定义 IP 参数 / IP 手册 / 用户告知 |
| 是否有反压（tready）/ 是否需要触发 | 单次传输还是连续流；超时预算 | 问用户 / 看自定义 IP 手册 |
| 采样时钟 | 帧时间与 `POLL_TIMEOUT_COUNTER` | `.hwh` 里 `clk_wiz` 的输出频率 |

**取法优先级**：`.hwh`（或 `.xsa` 解出的 hwh）> 用户在 Vivado 里查 > 用户口述。

**都拿不到 ⇒ 停下来先问用户**，按上表逐项问；**不允许默认 PL 参数，也不允许按经验假设点数**。
用户只给得出部分信息时，缺的项写进 `未知`，并说明缺哪项会阻塞哪一步。

### Step 1: 确认事实（不凭印象，逐项从源码抄）

| 项 | 从哪里取 |
|---|---|
| DMA 基地址 | `XAxiDma_LookupConfig(...)` 的实参 |
| 通道方向 | `XAXIDMA_DEVICE_TO_DMA`（S2MM）/ `XAXIDMA_DMA_TO_DEVICE`（MM2S） |
| 传输长度 | `XAxiDma_SimpleTransfer` 的第 3 个实参（**表达式**，不是注释） |
| 点数来源 | 上式由哪个变量推导 |
| 每点字节数 | 按数据格式确定（从缓冲区指针类型与打包方式算）|
| 缓冲地址与大小 | 缓冲区声明处；是否 4 字节对齐（Flush 按缓存行取整）、是否够 `点数 × 每点字节数` |
| 状态标志 | `RxDone*` / `TxDone*` / `Error*` 的定义与清零位置 |

```bash
grep -rn "XAxiDma_SimpleTransfer" --include=*.c .
grep -rn -A3 "XAxiDma_SimpleTransfer" --include=*.c .
grep -rn "DCacheFlushRange" --include=*.c .
```

### Step 2: 长度一致性（先查这一步，命中率最高）

```bash
python scripts/dma_len_check.py --points <N> --bytes-per-point <B> \
    --max-points <MAX> [--dma-len <代码里的长度>] [--fs <fs>] [--mhz <MHz>] --json
```

判据：

- `MISMATCH` ⇒ **停止往下查**，先改长度。长度不匹配时 S2MM 永远到不了 IOC（完成中断）、`done` 永不置位，**且没有错误码**。
- 长度必须由点数**推导**，不允许出现与点数无关的常数。
- 若存在"点数会变"的路径（切档、开功能），每条路径各自核对一遍。

### Step 3: 缓存同步

- 提交前：对**将被 DMA 读写**的缓冲做 `Xil_DCacheFlushRange`，长度 = `点数 × 每点字节数`
- 长度都用**字节**，并向上对齐到缓存行；不允许按行数/点数误传

### Step 4: 提交与等待（顺序固定）

```c
/* 1. 清标志（标志由 ISR 置位，主循环只读） */
RxDone0 = 0;
TxDone0 = 0;
Error0 = 0;

/* 2. 提交前：把将被 DMA 读写的缓冲刷出缓存（长度按字节） */
Xil_DCacheFlushRange((UINTPTR)RxBufferPtr0, MAX_PKT_LEN/2);
Xil_DCacheFlushRange((UINTPTR)TxBufferPtr0, MAX_PKT_LEN/2);

/* 3. 提交：长度由点数推导；返回 XST_SUCCESS 只代表"已提交" */
XAxiDma_SimpleTransfer(&AxiDma0, (UINTPTR)RxBufferPtr0, MAX_PKT_LEN/2, XAXIDMA_DEVICE_TO_DMA);
XAxiDma_SimpleTransfer(&AxiDma0, (UINTPTR)TxBufferPtr0, MAX_PKT_LEN/2, XAXIDMA_DMA_TO_DEVICE);

/* 4. 等待完成（带超时；返回值必须判） */
if (Xil_WaitForEventSet(POLL_TIMEOUT_COUNTER, NUMBER_OF_EVENTS, &RxDone0) != XST_SUCCESS) {
    /* 超时：整帧丢弃，不复用上一帧 */
}
if (Xil_WaitForEventSet(POLL_TIMEOUT_COUNTER, NUMBER_OF_EVENTS, &TxDone0) != XST_SUCCESS) {
    /* 超时：整帧丢弃 */
}

```

判据：

- 上面用的都是模板里真实可用的 API：`Xil_DCacheFlushRange`（`xil_cache.h`）、
  `XAxiDma_SimpleTransfer`（`xaxidma.h`）、`Xil_WaitForEventSet`（`xil_util.h`，返回 `u32`）
- **源 IP 若需要触发，必须在提交之后触发**：先触发后提交会丢前几个点（模板里没有触发步骤，接源 IP 时要自己加）
- 等待必须有超时；`Xil_WaitForEventSet` 返回非 `XST_SUCCESS` 即超时 ⇒ **整帧丢弃**
- `XAxiDma_SimpleTransfer` 返回成功**只代表"已提交"**，不代表"会完成"
- 模板 `main.c` 目前**忽略**了 `Xil_WaitForEventSet` 的返回值 ⇒ 工程里建议判 `XST_SUCCESS`，否则超时无法丢弃整帧

### Step 5: 只取本帧有效点数

- 拷贝/使用循环的边界必须是**本帧实际点数**，不能是缓冲上限
- 否则点数变小后会混入上一帧的旧数据（读到的不是本帧数据）。

### Step 6: 上板验证（给出可复现的数字）

- 连续 1000 帧：统计完成次数、错误次数、超时次数，要求**超时为 0**
- 单帧端到端耗时 ≈ `点数 ÷ 采样率`
- 人为把长度改成错误值一次：应复现"永不完成"，证明检测手段有效（改回）
- 数据新鲜度：让源 IP 输出递增计数，确认每帧读到的值都在变化

### 输出格式

```text
=== AXI DMA 通路检查报告 ===
[✓/✗] [PL]     DMA 模式(SG?) / 方向 / 每点字节数 / 点数 的来源（hwh 还是用户口述）
[✓/✗] [长度]   期望 <N*B> 字节，代码 <LEN> 字节
[✓/✗] [缓存]   提交前是否 Flush、长度是否按字节
[✓/✗] [顺序]   清标志 → 提交 → 触发 → 等待 → 超时丢弃
[✓/✗] [超时]   POLL_TIMEOUT_COUNTER 是否 ≥ 采集时间 / 单次轮询开销 × 3
[✓/✗] [取数]   是否只取本帧有效点数
[✓/✗] [缓冲]  地址对齐（≥4 字节）、大小足够、Flush 覆盖整个缓冲
=== 总结 ===
通过: X/7   结论: PASS / 需修复（列出最小修改点）
未知: 证据不足的项（写明缺什么）
```

## ⚠️ Gotchas

- **编译通过 ≠ 配置正确**：长度不匹配、缓存未同步都能编译通过。
- **PL 未确认就配 PS = 猜**：DMA 模式与点数由 PL 决定；读不到 `.hwh` 又没问清 ⇒ 只能写 `未知`，不要给具体数值。
- **`XST_SUCCESS` 只代表"已提交"**，不能当作"传输完成"的判据。
- **不要只查第一处调用点**：同一工程常有多条采集路径。
- **不要因为"以前一直能跑"就排除长度问题**：常数长度在默认点数下永远是对的，只在切档时暴露。
- **不要用加延时替代长度核对**：只会把"永不完成"变成"偶尔完成"。
- **诊断阶段不要改业务逻辑**：一动手，原始现象就无法复现。
- **中断不是本 Skill 的事**：标志位由中断服务函数置位，但中断怎么注册/使能与本 Skill 无关；
  若标志永远不变，先确认中断链本身是否打通（那属于另一类问题）。
- **拿不到上板证据时**：结论写 `未知`，不要用"应该/大概"补位。

## 来源：失败与观察

下列现象来自实际调试，**不绑定具体板卡或数值**：

1. **长度写死常数**：采集长度写成固定字节数（= 默认点数 × 每点字节数），与"切到更小点数"的路径冲突
   ⇒ S2MM 永不完成 ⇒ 每帧丢弃，表现为"一开某功能，数据就不再更新"。
2. **超时后继续用旧数据**：某帧有一路没搬完，仍继续测量与显示 ⇒ 读到错帧、读数异常。
3. **按上限整段拷贝**：只采到 N 点却按 2N 点拷贝 ⇒ 混入上一帧旧数据 ⇒ 读数异常。

## 配套文件

- `checklist.md`：事实确认、长度/缓存/顺序核对、上板验收、红牌。
- `decision-tree.md`：现象 → 该取什么证据 → 根因候选。
- `examples/README.md`：三个真实排查例子（超时/顺序/PL 接线）。
- `scripts/dma_len_check.py`：长度/对齐/缓冲/超时预算自检（支持 `--json`）。
- `templates/`：`dma.h` / `dma.c` / `main.c` + 填空说明（不含 GIC 配置）。

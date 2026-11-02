# 技能包（Skill）

本目录是作品的**可复用技能包**：把调试中反复出现的判据、脚本与模板，固化成"AI 或人都能直接照做"的步骤。
每个技能都是一个独立目录，入口是 `SKILL.md`；三个技能**互不依赖**，可单独拿走使用。

## 命名约定（PS / PL 必须分清）

| 前缀 | 覆盖范围 | 判据 |
|---|---|---|
| `ps-` | **PS 端**：ARM 裸机 C 代码、驱动 API、GIC/中断、缓存、寄存器时序 | 能改的是 `.c` / `.h` 与寄存器调用 |
| `pl-` | **PL 端**：RTL、Block Design、IP 参数、时序约束 | 要改的是 `.bd` / `.hwh` / `.xdc` |

## 目录

```
skill/
├── README.md                      本文件（技能总索引与使用说明）
├── manifest.yaml                  技能索引（name / description / path / version）
├── plugin.json                    插件清单（agent-plugins 1.0 规范，可直接安装）
├── ps-axi-dma-capture-guard/      PS 端：AXI DMA 采集/发送链路检查
├── ps-multi-irq-config-triage/    PS 端：多中断配置与“中断不生效”排查
└── pl-vdma-display-pipeline/      PL 端：VDMA 显示通路配置审计
```

每个技能目录内固定 6 类文件：

| 文件 | 作用 |
|---|---|
| `SKILL.md` | 唯一入口：为什么需要 / 何时使用 / 输入 / 前置条件 / 取证命令 / 工作流 / 输出 / 约束 / 来源 / 复用边界 / 异常处理 / Gotchas / 配套文件 |
| `checklist.md` | 可勾选清单：事实确认、逐项核对、上板验收、红牌 |
| `decision-tree.md` | 现象 → 该取什么证据 → 根因候选 |
| `examples/README.md` | 三个真实排查例子（场景 → 查什么 → 结论 → 验证）|
| `scripts/`、`templates/` | 确定性检查脚本（Python 3 标准库）、工程模板/只读 Tcl |

## 技能一览

| 技能 | 端侧 | 只负责 | 入口 |
|---|---|---|---|
| `ps-axi-dma-capture-guard` | PS | DMA 传输长度与点数一致性、缓存同步、提交/等待顺序、超时与旧帧丢弃 | `ps-axi-dma-capture-guard/SKILL.md` |
| `ps-multi-irq-config-triage` | PS | GIC 使能链、外设侧使能、ID 与触发类型、异常入口、清源、共享标志可见性 | `ps-multi-irq-config-triage/SKILL.md` |
| `pl-vdma-display-pipeline` | PL | 方向、行/场时序、位宽链、VTG 来源、fsync、**连线**、像素格式，判“能否正常显示一帧” | `pl-vdma-display-pipeline/SKILL.md` |

---

## 1. `ps-axi-dma-capture-guard`（PS 端）

**适用范围**：任何 Zynq-7000 裸机工程里“DMA 从自研 IP 采数据、或往 IP 发数据”的链路。
典型现象：采不到数据、每帧超时、数据看起来不变、`XAxiDma_SimpleTransfer` 返回失败。

**前置**：PS 侧配置以 **PL** 为前提 —— 先确认 DMA 模式（是否 SG）、方向（S2MM/MM2S）、流位宽、每次数据量；
读不到 `.hwh` 且用户没告知时，**先向用户要 PL 情况**，不要默认参数。

**使用方法**：
1. Step 1：用 `checklist.md` 的 A 段从源码抄事实（不凭印象）；
2. Step 2：跑 `scripts/dma_len_check.py`，对齐“点数 × 每点字节数”与实际传输长度；
3. Step 3：核对缓存同步（提交前 Flush，长度按字节）；
4. Step 4：核对提交与等待（清标志 → 提交 → 触发 → 等待 → 超时整帧丢弃）；
5. Step 5：核对取数范围（只取本帧有效点数）；
6. Step 6：上板验收（连续 1000 帧超时为 0），并按 `SKILL.md` 的“输出格式”给出结论。

**失效条件**：Scatter-Gather 描述符管理；多通道带宽规划；IP 侧数据格式定义；
**中断控制器（GIC）的注册、使能、优先级与触发类型**（本技能只要求“标志能被主循环观察到”）；算法本身的正确性。

**验证有效**：
- 脚本在真实工程上：长度正确时输出 `PASS`；长度不匹配时输出 `FAIL` 并给出差值与“S2MM 永不完成”的后果说明；
- 模板 `templates/` 的 `dma.c` 对目标 BSP 编译 `error=0`（`main.c` 依赖使用者自己的 `intr.h`）；
- `examples/` 给出三个真实排查例子（传输慢看超时与标志复位 / 丢前几个点看开关顺序 / 条件都正常查 PL 接线）；

**来源（从哪些失败或观察总结）**：长度写死常数导致切档后永不完成；超时帧继续使用导致读到错帧。

---

## 2. `ps-multi-irq-config-triage`（PS 端）

**适用范围**：任何“GIC + 多路外设中断（AXI DMA 各通道、UART 等）”的裸机工程。
典型现象：某一路中断不触发、所有中断都不来、中断进了但主循环没反应、中断反复进入。

**前置**：中断号由 **PL** 侧连线决定 —— 先确认中断源有几路、每路连到哪个 GIC ID、是否有中断汇聚（`xlconcat`）；
读不到 `.hwh` 且用户未告知时，**先向用户要 PL 情况**，不要默认中断号与路数。

**使用方法**：
1. Step 0：确认 PL 侧中断路数与 GIC ID（`.hwh` / `xparameters_ps.h` / 用户口述）；
2. Step 1：用 `checklist.md` 的 A 段从源码抄事实（ID / 处理函数 / 优先级实参 / 私有数据）；
3. Step 2~5：逐项核对 GIC 初始化、`SetPriorityTriggerType`、`Connect`、异常挂载、外设侧使能顺序；
4. 先判“全部不来 / 某一路不来”，再按 `decision-tree.md` 取证据；
5. Step 6：上板用“ISR 计数 + 主循环打印”验收，按最小修改点一次改一处。

**失效条件**：PL 端 RTL / Block Design / 约束；DMA 传输长度与缓存策略；中断嵌套与优先级抢占的总线级设计；
多核（CPU1）中断分发；裸机以外的操作系统中断框架。

**验证有效**：
- 脚本对真实工程能报出“只 `Connect` 没 `XScuGic_Enable`”“缺 `SetPriorityTriggerType`”“缺异常挂载”“`IntrDisable` 重复”等问题；
- 模板 `templates/intr.h` / `intr.c` 配合 `dma.h`、目标 BSP 编译 `error=0`；
- `examples/` 给出三个“注释一行即可复现”的失败模式。

**来源（从哪些失败或观察总结）**：只给 4 路 DMA 设优先级/触发类型、串口那一路漏设 ⇒ 串口中断偶发不响应；
同一组 `XAxiDma_IntrDisable` 写了两遍 ⇒ 顺序被掩盖、排查时容易误判使能没生效；
`disable_intr_system` 只断开传入的两路 ⇒ 其余中断仍在响应。

---

## 3. `pl-vdma-display-pipeline`（PL 端）

**适用范围**：任何“帧缓冲 → VDMA(MM2S) → AXI4-Stream Video → 视频输出 → 面板”的显示链路。
典型现象：黑屏、无信号、花屏、颜色不对、画面偏移、画面停住、显示器报“超出范围”。

**前置（调用后的第一件事）**：先问清**用户面板**信息（像素格式 RGB888/RGB565、位宽、DE 或 HS/VS、HFP/HSW/HBP、VFP/VSW/VBP、极性、像素时钟）；
没有面板数据既判断不了 IP 够不够用，也算不出 `v_tc` 该怎么配。

**使用方法**：
1. Step 0：索取面板信息（表见 `SKILL.md`）；
2. Step 1：跑 `scripts/vdma_config_audit.py --hwh <bd>.hwh --panel-format rgb888|rgb565`，判 E1~E8；
3. Step 2~3：算帧周期/帧字节，并用 `scripts/vtc_timing_calc.py` 由面板时序算 `v_tc` 的 8 个参数、与 `.hwh` 逐项比对；
4. Step 4：输出“能否显示”的结论与最小修改点（只给方案，由用户在 Vivado 里改）；
5. Step 5：上板用静态测试图（纯色 / 竖色条 / 棋盘格）验收。

**失效条件**：应用层绘制与刷新策略、撕裂/闪烁的软件侧优化、帧率与带宽调优；RTL 修改与时序收敛（WNS）；
LCD 背光/电源等硬件问题；PS 端 C 代码；没有 `.hwh` 且用户也说不清 IP 参数与连线的情况。

**验证有效**：
- `vdma_config_audit.py` 能判 E1~E8，并对 `--panel-format rgb565` 识别链路里有没有 24bit→565 的转换 IP；
- `vtc_timing_calc.py` 能反推 HFP/HSW/HBP、VFP/VSW/VBP，并与面板参数逐项比对；
- 两个脚本对不同 `.hwh` 实测可跑通（真实工程的 RTL/BD 参数来自 `.hwh`，不写死）。

**来源（从哪些失败或观察总结）**：`vtg_ce` 没接 `gen_clken` ⇒ 全黑而参数全对；
RGB565 面板用 `v_axi4s_vid_out` ⇒ 颜色/位宽错，必须自写转换 IP；
`v_tc` 没按面板手册配 ⇒ 偏移/压缩/黑边/不识别；
用有效尺寸算帧率 ⇒ 差约 10%；
两个 `clk_wiz` 混淆 ⇒ 帧率错；
单帧缓冲 ⇒ 能显示但可能撕裂。

---

## 怎么用

**人**：打开对应技能的 `SKILL.md`，按“工作流”执行，用 `checklist.md` 逐项确认。

**AI 对话**：给出 `SKILL.md` 路径，并要求“严格按其中工作流与约束执行，证据不足写进 `unknown`”。

**作为插件安装**：本目录自带 `manifest.yaml` 与 `plugin.json`，按各宿主方式指向本目录即可
（Codex / Claude Code / Cursor / VS Code 的差异见 AMD 官方 `install-plugin.md`）。

**自动化检查（无需上板）**：

```bash
python skill/ps-multi-irq-config-triage/scripts/irq_config_check.py --src ./src
python skill/ps-axi-dma-capture-guard/scripts/dma_len_check.py --points <N> --bytes-per-point <B> --fs <fs> --mhz <MHz>
python skill/pl-vdma-display-pipeline/scripts/vdma_config_audit.py --hwh <bd>.hwh --panel-format rgb888
python skill/pl-vdma-display-pipeline/scripts/vtc_timing_calc.py --hwh <bd>.hwh
```

## 设计原则

1. **端侧分离**：`ps-` 只看 C 代码与寄存器，`pl-` 只看 BD/IP/约束；互不替代。
2. **单一职责**：一个技能只回答一类问题；`SKILL.md` 控制在 500 行以内。
3. **代码优于文字**：关键步骤给可执行命令或脚本，不给笼统描述。
4. **确定性降级**：能脚本化的检查写成脚本，模型只负责判读结果。
5. **来自失败**：每个技能都有“来源：失败与观察”，逐条对应真实故障，且不绑定具体板卡数值。
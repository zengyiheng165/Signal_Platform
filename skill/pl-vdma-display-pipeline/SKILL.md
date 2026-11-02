---
name: pl-vdma-display-pipeline
description: 当用户提及检查PL端的VDMA显示通路（MM2S方向、行/场时序、位宽链、VTG来源、fsync同步、连线、像素格式）：黑屏、花屏、颜色不对、显示器不识别、画面停住时使用。只做到“能正确显示一帧”为止，应用层绘制与性能调优不在范围内；PS 端 C 代码不在范围内。关键词：PL, Zynq, VDMA, MM2S, v_tc, v_axi4s_vid_out, vtg_ce, TDATA, pixel clock, RGB565, display
allowed-tools: Read, Grep, Glob, Bash(python:*)
---

# VDMA 显示通路检查 Skill

回答一个问题：**这条 PL 端 VDMA 显示通路，配置与连线是否成立、能否正常显示一帧正确画面。**
做到"能显示"就结束 —— 不评估应用层怎么画、不优化刷新率、不做性能调优。
**只读检查**：不改 BD / IP / RTL / 约束，只输出"该改什么、改成多少"，由用户在 Vivado 里执行。

> **调用本技能的第 1 个动作：询问屏幕参数。**
> 没有像素格式与行/场时序明细，就**算不出** `v_tc` 的 8 个参数，也**定不了** RGB 格式
> （RGB888 还是 RGB565）与是否需要转换 IP ⇒ 不允许跳过，也不允许默认按 RGB888 处理。

## 适用范围

- 任何"帧缓冲 → VDMA(MM2S) → AXI4-Stream Video → 视频输出 → 面板"的显示链路
- 现象：黑屏、无信号、花屏、颜色不对、画面偏移、画面停住、显示器报"超出范围"

## 失效条件

- 应用层绘制与刷新策略、撕裂/闪烁的软件侧优化、帧率与带宽调优
- RTL 修改与时序收敛（WNS）、LCD 背光/电源等硬件问题
- **PS 端 C 代码**（写帧缓冲、启动 VDMA、应用层逻辑）
- 没有 `.hwh` 且用户也说不清 IP 参数与连线的情况 ⇒ 只能给排查方向，不能下结论

## 检查流程

### Step 0（最先做）: 询问屏幕参数（决定 v_tc 配置与 RGB 格式）

**调用本技能时的第一个动作就是按下面这张表逐项问**；用户没给就不往下判，缺项记为"未知"。

| 要问的 | 为什么 | 影响 |
|---|---|---|
| 分辨率（H×V） | 与 `v_tc` 的有效尺寸对照 | E2 |
| **像素格式：RGB888 / RGB565** | `v_axi4s_vid_out` 只支持等宽分量，565 不支持 | **E8** |
| 总线位宽（16/18/24 bit） | 决定是否需要转换 IP、`TDATA` 宽度 | E3 / E8 |
| 接口方式（DE / HS+VS） | 决定极性位与转换 IP 的输出 | E2 / E8 |
| 行/场时序（HFP/HSW/HBP、VFP/VSW/VBP） | 直接对应 `v_tc` 的 8 个参数 | E2 |
| 同步极性（HSYNC/VSYNC/DE） | 与面板手册一致 | E2 |
| 像素时钟范围 | 面板能否锁定 | E2 |

**用户没给就必须先问**：没有面板数据，既判断不了"IP 够不够用"，也算不出 `v_tc` 该怎么配。
拿不到就写"未知"，不允许默认按 RGB888 处理。

### Step 1: 判定 8 个必要条件（E1~E8）

| 编号 | 条件 | 判据 |
|---|---|---|
| E1 | 方向 | VDMA 使能了 MM2S（`C_INCLUDE_MM2S = 1`）|
| E2 | 时序 | 有 `v_tc`；`C_GENERATE_EN=1`；`HFRAME > HACTIVE`；`VFRAME > VACTIVE`；`C_GEN_INTERLACED=0` |
| E3 | 位宽链 | `TDATA = 分量位宽 × 3`；VDMA 流位宽 = `v_axi4s_vid_out` 的 `S_AXIS` 位宽；`M_AXI ≥ TDATA` |
| E4 | VTG 来源 | `v_axi4s_vid_out` 的 `C_VTG_MASTER_SLAVE=0` ⇒ 必须有 `v_tc` |
| E5 | 帧同步 | `C_USE_FSYNC=1` ⇒ 必须有 fsync 源（通常 `v_tc` 的 SOF）|
| E6 | 可配置性 | VDMA 有 AXI-Lite 基地址（PS 才能配置）|
| E7 | 连线 | `vtg_ce` 必须接到 `v_tc` 的 `gen_clken`；`aclken`/`vid_io_out_ce`/`v_tc.clken` 未接信号属正常（IP 默认使能）|
| E8 | 像素格式 | 面板 RGB888 ⇒ 8bit 分量的 `vid_out` 可用；面板 **RGB565** ⇒ 必须有 24bit→565 的转换环节（自写 IP）|

任一项不成立 ⇒ 直接给出该条修法，**不用继续分析别的现象**。

### Step 2: 算两个数字（判断"时序是否自洽"）

```
帧周期 = HFRAME × VFRAME / pclk        （必须用含消隐的总数）
帧字节 = HACTIVE × VACTIVE × 每像素字节
```

判据：由 pclk 反算的帧率 = 面板额定帧率（±0.5%）；帧字节与帧缓冲大小相符。

### Step 3: 按用户面板手册的时序参数配置 v_tc（数值只能来自用户）

**先向用户索取面板手册的 Timing 表**，逐项抄下来：

| 手册里的常见写法 | 含义 | 对应 `v_tc` 参数 |
|---|---|---|
| DCLK / fDCLK | 像素时钟（MHz） | 决定 MMCM 输出，并用于帧率校验 |
| HACTIVE / HDISP | 有效宽度 | `C_GEN_HACTIVE_SIZE` |
| HFP / HSW / HBP | 行前肩 / 行同步脉宽 / 行后肩 | 累加出 HFRAME 与 HSYNC 位置 |
| VACTIVE / VDISP | 有效高度 | `C_GEN_VACTIVE_SIZE` |
| VFP / VSW / VBP | 场前肩 / 场同步脉宽 / 场后肩 | 累加出 VFRAME 与 VSYNC 位置 |
| HSYNC / VSYNC / DE 极性 | 高有效 / 低有效 | `C_GEN_*_POLARITY` |

**换算式**（把手册数值代入，不要抄任何现成设计、也不要用 IP 默认值）：

```
HFRAME       = HACTIVE + HFP + HSW + HBP
HSYNC_START  = HACTIVE + HFP
HSYNC_END    = HACTIVE + HFP + HSW
VFRAME       = VACTIVE + VFP + VSW + VBP        （逐行）
VSYNC_VSTART = VACTIVE + VFP
VSYNC_VEND   = VACTIVE + VFP + VSW
pclk         = HFRAME × VFRAME × fps
```

**用脚本算，并与 `.hwh` 比对**：

```bash
# 反推 hwh 现有值：看现在到底填了什么
python scripts/vtc_timing_calc.py --hwh <bd>.hwh

# 由用户面板数值算应填值，并逐项比对差异
python scripts/vtc_timing_calc.py --hwh <bd>.hwh \
    --hact <HA> --vact <VA> --hfp <HFP> --hsw <HSW> --hbp <HBP> \
    --vfp <VFP> --vsw <VSW> --vbp <VBP> --fps <fps>
```

判据：

- 同步必须落在消隐区：`HSYNC_START ≥ HACTIVE`、`HSYNC_END ≤ HFRAME`；`VSYNC_VSTART ≥ VACTIVE`、`VSYNC_VEND ≤ VFRAME`
- 脉宽不能为 0（`HSYNC_START < HSYNC_END`、`VSYNC_VSTART < VSYNC_VEND`）
- 4 个极性位与手册一致
- 不满足的表现：画面左右/上下偏移、被压缩、有黑边、显示器不识别

### Step 4: 给出"能否显示"的结论

- 8 项全过 ⇒ **PL 侧具备出图条件**；下一步是 PS 启动 VDMA（写地址/长度/帧数 + 置运行位），那属于 PS 侧，本 Skill 到此为止
- 有 E 项 ⇒ 指出是哪一项、改成什么值、如何复验
- 证据不足 ⇒ 写"未知"，不要用"应该/大概"补位

### Step 5: 上板确认（静态测试图，不涉及应用层）

- 全屏纯色（R / G / B 各一次）⇒ 颜色顺序与通道正确
- 竖色条（每 8 列一色）⇒ 无 stride/对齐错位
- 棋盘格 ⇒ 无丢块、无错行
任一不通过 ⇒ 回到对应 E 项。

### 输出格式

```text
=== VDMA 显示通路检查报告 ===
[✓/✗] [面板]   像素格式（RGB888/RGB565）、分辨率、时序明细是否已提供
[✓/✗] [E1]     VDMA 使能 MM2S
[✓/✗] [E2]     v_tc 存在且 HFRAME>HACTIVE、VFRAME>VACTIVE、逐行
[✓/✗] [E3]     位宽链：TDATA = 分量×3，且与 vid_out S_AXIS 一致
[✓/✗] [E4]     VTG 来源（vid_out 为 slave 时必须有 v_tc）
[✓/✗] [E5]     USE_FSYNC 时有 fsync 源
[✓/✗] [E6]     VDMA 有 AXI-Lite 基地址
[✓/✗] [E7]     vtg_ce 已接到 v_tc 的 gen_clken
[✓/✗] [E8]     像素格式与转换 IP 匹配（RGB565 必须有转换环节）
=== 总结 ===
通过: X/9   结论: PASS / 需修复（列出最小修改点）
未知: 证据不足的项（写明缺什么）
```

## 约束

本技能 MUST NOT：

- 评价应用层绘制、刷新策略、撕裂优化（不在范围内）
- **修改 PL 端任何内容**（BD 连线 / IP 参数 / RTL / 约束）：只输出"该改什么、改成多少"，由用户执行
- 代替用户运行会改动工程的命令（`set_property`、`generate_target`、`save_bd_design`）
- 把"某个现成设计的参数值"当成通用答案：`v_tc` 的 8 个参数必须由用户自己的面板手册算出
- 跑综合、实现、仿真
- 在没读 `.hwh` 的情况下判断参数
- 用"加 FIFO / 提优先级 / 改缓冲数"掩盖 E1~E8 的硬性不成立
- 手抄参数：一律来自 `.hwh`（或在 Vivado 里看到的值），并给行号

## 来源：失败与观察

下列现象来自实际调试，但**不绑定任何具体面板或板卡**：

1. **`vtg_ce` 没接 `v_tc` 的 `gen_clken`** ⇒ 什么都显示不出来；参数（分辨率/时序/位宽/时钟）全对，只有连线缺失。
2. **RGB565 面板 + `v_axi4s_vid_out`**：该 IP 只支持等宽分量（8/10/12/16），出不了 5/6/5 ⇒ 颜色/位宽错，必须自写转换 IP。
3. **`v_tc` 没按面板手册配**：沿用默认值或抄别家屏 ⇒ 画面偏移、压缩、黑边、显示器不识别。
4. **用有效尺寸算帧率**：必须用 `HFRAME × VFRAME`（含消隐），否则帧率差约 10%。
5. **两个 clk_wiz 混淆**：一个采样时钟、一个像素时钟，取错就得出错误帧率。
6. **单帧缓冲**：`C_NUM_FSTORES=1` 能显示，但可能撕裂/局部闪（属于"能显示但不完美"）。

## ⚠️ Gotchas

- **参数全对也可能全黑**：`vtg_ce` 不接 `gen_clken` 时，分辨率/时序/位宽/时钟都正常，VTG 就是不推进。
- **`.hwh` 里"自闭合无连接"有两种含义**：接默认常量（正常，如 `aclken`），或真的悬空（致命，如 `vtg_ce`）——对 `vtg_ce` 必须按致命处理。
- **`v_axi4s_vid_out` 不支持 RGB565**：它要求三个分量位宽相同，面板 565 必须自己做 24bit→565 转换。
- **别按 RGB888 假设**：不问题面板就默认 888，会在 565 面板上得到错色/错位（看起来像"花屏"）。
- **`v_tc` 的默认值不是你的屏**：8 个参数必须由面板的前肩/脉宽/后肩算出来。
- **`HFRAME`/`VFRAME` 含消隐**：用有效尺寸算帧率必错。
- **改完 IP 参数必须重新生成 `.hwh`**：审的是生成产物，不是 GUI 里的旧值。
- **两个 clk_wiz 的频率不能互换**：像素时钟错 ⇒ 帧率错、显示器不识别。
- **`C_NUM_FSTORES` 是帧缓冲数**：1 就是单缓冲（能显示，但可能撕裂）。
- **PL 侧自洽 ≠ 会显示**：还需要 PS 侧启动 VDMA；PS 端不在本 Skill 内。
- **拿不到上板证据时**：结论写"未知"，不要用"应该/大概"补位。

## 配套文件

- `checklist.md`：面板信息、E1~E8 逐项核对、上板验收、红牌。
- `decision-tree.md`：现象 → 该取什么证据 → 根因候选。
- `examples/README.md`：两个可复现的失败模式（`vtg_ce` 连线、RGB565 面板）+ 一个触屏建议（电容屏引出 EMIO I2C 配触摸）。
- `scripts/vdma_config_audit.py`：一键判定 E1~E8（`--panel-format`、`--json`）。
- `scripts/vtc_timing_calc.py`：由面板时序算 `v_tc` 8 参数 / 从 hwh 反推 / 逐项比对。
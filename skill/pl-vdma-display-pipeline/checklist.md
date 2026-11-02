# checklist — VDMA 显示通路检查

> 逐项勾选；证据不足就写"未知"，不要用"应该/大概"补位。

## 0. 面板信息（**必须先问清；拿不到就先问用户**）

| 项 | 值 |
|---|---|
| 分辨率（H×V） | |
| **像素格式（RGB888 / RGB565）** | |
| 总线位宽（16/18/24 bit） | |
| 接口方式（DE / HS+VS） | |
| HFP / HSW / HBP | |
| VFP / VSW / VBP | |
| 同步极性（HSYNC / VSYNC / DE） | |
| 像素时钟范围 | |

## A. E1~E8 逐项核对

| 编号 | 判据 | 实测值 | 通过 |
|---|---|---|---|
| E1 | VDMA 使能 MM2S（`C_INCLUDE_MM2S = 1`）| | |
| E2 | 有 `v_tc`；`C_GENERATE_EN=1`；`HFRAME>HACTIVE`；`VFRAME>VACTIVE`；`C_GEN_INTERLACED=0` | H____ V____ HACT____ VACT____ EN____ IL____ | |
| E3 | `TDATA = 分量×3`；VDMA 流位宽 = `vid_out` 的 `S_AXIS`；`M_AXI ≥ TDATA` | M_AXI____ TDATA____ 分量____ | |
| E4 | `vid_out` 的 `C_VTG_MASTER_SLAVE=0` ⇒ 必须有 `v_tc` | MS____ VTC____ | |
| E5 | `C_USE_FSYNC=1` ⇒ 有 fsync 源 | FSYNC____ | |
| E6 | VDMA 有 AXI-Lite 基地址 | | |
| E7 | `vtg_ce` 接到 `v_tc` 的 `gen_clken`（悬空/接常量 ⇒ 全黑）| | |
| E8 | 像素格式匹配：RGB888 ⇒ 分量 8bit 可用；**RGB565 ⇒ 必须有转换环节** | | |

## B. 两个数字

- [ ] 帧周期 = HFRAME × VFRAME ÷ pclk = ________ ms（应等于面板额定的 1/fps）
- [ ] 帧字节 = HACTIVE × VACTIVE × 每像素字节 = ________

## C. v_tc 8 个参数与面板手册比对

- [ ] 反推的 HFP / HSW / HBP 与手册一致
- [ ] 反推的 VFP / VSW / VBP 与手册一致
- [ ] 同步位置落在消隐区，且脉宽 ≥ 1
- [ ] 4 个极性位与手册一致

## D. 上板验收（静态测试图，不涉及应用层）

- [ ] 全屏纯色 R / G / B 各一次 ⇒ 颜色顺序与通道正确
- [ ] 竖色条（每 8 列一色）⇒ 无 stride/对齐错位
- [ ] 棋盘格 ⇒ 无丢块、无错行

## E. 红牌（出现即停）

- [ ] 不问题面板就按 RGB888 判
- [ ] 只看参数、不看连线（E7 的 `vtg_ce`）
- [ ] 把"应用层怎么画/怎么刷"混进来
- [ ] 替用户改 PL（BD / IP / RTL / 约束）
- [ ] 手抄或编造参数值（不来自 `.hwh` 或 Vivado 里的实际值）
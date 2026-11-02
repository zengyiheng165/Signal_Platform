#ifndef __CLK_WIZ1_H_
#define __CLK_WIZ1_H_

#include "xclk_wiz.h"
#include "xparameters.h"

/* ---- DRP 寄存器（与原 clk_wiz.h 完全一致）---- */
#define CLK_SR_OFFSET    0x04    /* Status Register: bit0=SRDY, bit1=LOCKED */
#define CLK_CFG0_OFFSET  0x200   /* Clock Configuration Register 0  (D + M) */
#define CLK_CFG2_OFFSET  0x208   /* Clock Configuration Register 2  (CLKOUT0 分频) */
#define CLK_CFG23_OFFSET 0x25C   /* Clock Configuration Register 23 (写 bit0 触发 LOAD) */

/* ============================================================
 * 只允许三种频率，M/D/O 全部写死在表里
 * ============================================================
 * 寄存器位域（取自 system_clk_wiz_0_0_clk_wiz_drp.vhd:431/434）：
 *   0x200:  D           = bits[7:0]
 *           M 整数部分   = bits[15:8]
 *           M 千分位小数 = bits[25:16]
 *           小数使能     = bit26
 *   0x208:  O 整数部分   = bits[7:0]
 *           O 千分位小数 = bits[17:8]
 *           小数使能     = bit18
 *
 * 【必须成对写 0x200 与 0x208】
 * DRP 重配置时 mmcm_pll_drp.v 会用软件侧的 clkfbout_reg 值连带重写
 * CLKFBOUT/DIVCLK 寄存器（S2_CLKFBOUT_MULT / S2_DIVCLK_DIVIDE 端口），
 * 只写 0x208 会把倍频寄存器写成旧值，导致失锁。
 *
 * 表中十三组参数（均经锚点自检 0x05773C08 -> M=60.375/D=8 验证，
 * 并对每组回解 M/D/O 校验）：
 *
 *   频率         M        D   O         VCO
 *   5 MHz        32.000   5   128.000   640
 *   6 MHz        38.250   5   127.500   765
 *   6.144 MHz    44.375   6   120.375   739.583
 *   8 MHz        10       1   125.000  1000
 *   8.192 MHz    60.375   8    92.125   754.6875
 *   16 MHz       10       1    62.500  1000
 *   16.384 MHz   43.625   5    53.250   872.5
 *   32 MHz       10       1    31.250  1000
 *   32.768 MHz   16.875   2    25.750   843.75
 *   48 MHz       49.500   5    20.625   990
 *   49.152 MHz   43.625   5    17.750   872.5   <- 实际 49.15493MHz, +59.6ppm
 *   64 MHz       10       1    15.625  1000
 *   65.536 MHz   16.875   2    12.875   843.75
 *
 * 8.192 MHz 那组的 0x200 = 0x05773C08、0x208 = 0x00047D5C，
 * 与 IP 上电默认值完全相同。
 * ============================================================ */

#define CLK_WIZ_FRAC_EN   (1u << 18)   /* 0x208 的小数使能位 */

typedef struct {
    u32 freq_hz;    /* 目标频率（Hz） */
    u32 cfg0_reg;   /* 0x200 的值：D + M + 小数使能 */
    u32 cfg2_reg;   /* 0x208 的值：CLKOUT0 输出分频 O */
    float vco_mhz;  /* 该组对应的 VCO，仅供核对 */
} clk_wiz_entry_t;

static const clk_wiz_entry_t clk_wiz_table[] = {
    /*  freq_hz       0x200        0x208        VCO(MHz)   M / D / O */
    {   5000000u, 0x04002005u, 0x00000080u,  640.0000f },  /* 32.000 / 5 / 128.000 */
    {   6000000u, 0x04FA2605u, 0x0005F47Fu,  765.0000f },  /* 38.250 / 5 / 127.500 */
    {   6144000u, 0x05772C06u, 0x00057778u,  739.5833f },  /* 44.375 / 6 / 120.375 */
    {   8000000u, 0x04000A01u, 0x0000007Du, 1000.0000f },  /* 10     / 1 / 125.000 */
    {   8192000u, 0x05773C08u, 0x00047D5Cu,  754.6875f },  /* 60.375 / 8 /  92.125 */
    {  16000000u, 0x04000A01u, 0x0005F43Eu, 1000.0000f },  /* 10     / 1 /  62.500 */
    {  16384000u, 0x06712B05u, 0x0004FA35u,  872.5000f },  /* 43.625 / 5 /  53.250 */
    {  32000000u, 0x04000A01u, 0x0004FA1Fu, 1000.0000f },  /* 10     / 1 /  31.250 */
    {  32768000u, 0x076B1002u, 0x0006EE19u,  843.7500f },  /* 16.875 / 2 /  25.750 */
    {  48000000u, 0x05F43105u, 0x00067114u,  990.0000f },  /* 49.500 / 5 /  20.625 */
    {  49152000u, 0x06712B05u, 0x0006EE11u,  872.5000f },  /* 43.625 / 5 /  17.750 */
    {  64000000u, 0x04000A01u, 0x0006710Fu, 1000.0000f },  /* 10     / 1 /  15.625 */
    {  65536000u, 0x076B1002u, 0x00076B0Cu,  843.7500f },  /* 16.875 / 2 /  12.875 */
};

#define CLK_WIZ_TABLE_LEN  (sizeof(clk_wiz_table) / sizeof(clk_wiz_table[0]))

/* 设置 clk_out1 输出频率，单位 MHz。
 * 只接受表中列出的频率：
 *   5 / 6 / 6.144 / 8 / 8.192 / 16 / 16.384 / 32 / 32.768 /
 *   48 / 49.152 / 64 / 65.536 MHz
 * 其它值直接返回（不改硬件）。无返回值、无串口输出。 */
void clk_wiz_cfg(u32 BaseAddr, double freq);

#endif /* __CLK_WIZ1_H_ */

#include "clk_wiz1.h"
#include "xparameters.h"

/* 三种频率写死：8.192 / 16.384 / 32.768 MHz
 *   每个频率带自己的一组 M/D（0x200）和输出分频 O（0x208），
 *   两者必须成对写入，否则 DRP 重配置会用旧倍频值重写寄存器导致失锁。
 *   0x25C 写 bit0 触发 DRP 重配置 LOAD，最后等 LOCKED。 */

XClk_Wiz clk_wiz_inst;

void clk_wiz_cfg(u32 BaseAddr, double freq)   /* 频率单位：兆赫兹 */
{
    XClk_Wiz_Config *clk_cfg_ptr;
    u32 freq_hz;
    u32 i;
    u32 status;
    u32 timeout;

    if (freq <= 0.0) {
        return;
    }

    /* 1. 换算成 Hz 并查表（只允许表中三个预设值） */
    freq_hz = (u32)(freq * 1000000.0 + 0.5);   /* +0.5 实现四舍五入 */

    for (i = 0u; i < (u32)CLK_WIZ_TABLE_LEN; i++) {
        if (clk_wiz_table[i].freq_hz == freq_hz) {
            break;
        }
    }
    if (i >= (u32)CLK_WIZ_TABLE_LEN) {
        return;      /* 不在预设表里，保持原频率不动 */
    }

    clk_cfg_ptr = XClk_Wiz_LookupConfig(BaseAddr);

    /* 初始化 XCLK_Wiz */
    XClk_Wiz_CfgInitialize(&clk_wiz_inst, clk_cfg_ptr, clk_cfg_ptr->BaseAddr);

    /* 2. 配置倍频/输入分频（该频率专属的 M/D） */
    XClk_Wiz_WriteReg(clk_cfg_ptr->BaseAddr, CLK_CFG0_OFFSET,
                      clk_wiz_table[i].cfg0_reg);

    /* 3. 配置 CLKOUT0 输出分频系数 */
    XClk_Wiz_WriteReg(clk_cfg_ptr->BaseAddr, CLK_CFG2_OFFSET,
                      clk_wiz_table[i].cfg2_reg);

    /* 4. 加载重配置的参数（写 LSB 产生 SEN 脉冲，启动 DRP 序列） */
    XClk_Wiz_WriteReg(clk_cfg_ptr->BaseAddr, CLK_CFG23_OFFSET, 0x00000003);

    /* 5. 等 MMCM 重新锁定。
     *    SR bit0=SRDY（重配置模块就绪），bit1=LOCKED，这里必须判 bit1。
     *    加计数上限是为了锁不住时不会死等。 */
    for (timeout = 0u; timeout < 1000000u; timeout++) {
        status = XClk_Wiz_ReadReg(clk_cfg_ptr->BaseAddr, CLK_SR_OFFSET);
        if (status & 0x00000002u) {      /* Bit1 = LOCKED */
            return;
        }
    }
}

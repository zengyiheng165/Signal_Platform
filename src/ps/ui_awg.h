/******************************************************************************
 * ui_awg.h - 信号发生器界面(DAC1 / DAC2 两路输出)
 *
 * 硬件事实(依据 PL):
 *   - 普通 DDS(my_dds_0) 双路: CH1 -> DAC1, CH2 -> DAC2; 4 种波形: 正弦/方波/三角/锯齿
 *   - BRAM DDS(my_dds_bram_0) 单路: 只接 DAC1; 波形由 dds_wave.c 生成后写入 BRAM
 *   - 数据选择器(GPIO 高两位): 00 = DAC1 用普通 DDS CH1; 11 = DAC1 用 BRAM 波形
 *     DAC2 不受选择器影响, 始终由普通 DDS CH2 驱动
 *   - 满幅 11.5 Vpp(专业示波器实测标定), 无直流偏置(所以没有"偏置"参数)
 *
 * 屏幕分区: 顶栏(0..47) / 信息行(48..71) / 预览区(72..335, 只画当前通道) /
 *           参数区(336..439, 当前通道+当前波形的参数行, 3 行可见可滚动) / 按钮栏(440..479)
 *
 * 交互: 波形 12 项平铺循环(选到后 8 项即把 DAC1 的源切到 BRAM);
 *       参数区每行数值框可点开数字键盘; 枚举行用左右箭头切档。
 * 精度: 频率 1 Hz; 幅度 0.1 Vpp(0~11.5V); 相位 0.1°(0~360.0°); 百分比 0.1%。
 ******************************************************************************/
#ifndef UI_AWG_H
#define UI_AWG_H

#include "xil_types.h"

/* ---------------- 波形清单(平铺 12 项) ---------------- */
#define UI_AWG_W_SINE      0u   /* 0..3: 普通 DDS(两个通道都能选) */
#define UI_AWG_W_SQUARE    1u
#define UI_AWG_W_TRIANGLE  2u
#define UI_AWG_W_SAW       3u
#define UI_AWG_W_PULSE     4u   /* 4..11: BRAM 波形(只可能出现在 DAC1) */
#define UI_AWG_W_SINC      5u
#define UI_AWG_W_HARM      6u
#define UI_AWG_W_AM        7u
#define UI_AWG_W_DSB       8u
#define UI_AWG_W_FM        9u
#define UI_AWG_W_PM       10u
#define UI_AWG_W_ASK      11u
#define UI_AWG_W_CNT      12u
#define UI_AWG_W_DDS_CNT   4u

/* ---------------- 参数行 ---------------- */
#define UI_AWG_ROW_VIS     8     /* 参数区行数(左栏一次全显示, 不滚动) */
/* ★ 屏幕左右分栏: 左 = 参数区(所有行一次显示, 取消滚动), 右 = 波形预览
   (都在顶栏/信息行之下、按钮栏之上: y = 72..439, 高 368) */
#define UI_AWG_PANEL_X    0
#define UI_AWG_PANEL_W    240
#define UI_AWG_PANEL_Y    72
#define UI_AWG_PANEL_H    368
#define UI_AWG_PREV_X     240
#define UI_AWG_PREV_W     560
#define UI_AWG_PREV_Y     72
#define UI_AWG_PREV_H     368
#define UI_AWG_ROW_H       44     /* 每行高度: 7 x 44 = 344, 左栏 368 放得下 */
#define UI_AWG_HARM_MAX    8     /* 谐波次数上限 */

#define UI_AWG_K_NUM       0u    /* 数值行: 点数值框弹数字键盘 */
#define UI_AWG_K_ENUM      1u    /* 枚举行: 左右箭头切档 */

#define UI_AWG_U_HZ        0u    /* 频率(键盘里可切 Hz/kHz/MHz, 精确到 1 Hz) */
#define UI_AWG_U_VPP       1u    /* 幅度: Vpp, 0.1 V */
#define UI_AWG_U_DEG       2u    /* 相位: 度, 0.1° */
#define UI_AWG_U_PCT       3u    /* 百分比: 0.1% */
#define UI_AWG_U_INT       4u    /* 整数 */
#define UI_AWG_U_X10       5u    /* x10 的整数(显示成 x.y) */

/* 参数 id */
#define UI_AWG_P_WAVE      0u
#define UI_AWG_P_FREQ      1u
#define UI_AWG_P_VPP       2u
#define UI_AWG_P_PHASE     3u
#define UI_AWG_P_OUT       4u
#define UI_AWG_P_DUTY      5u    /* 方波(占空比): x10 (0..1000 = 0~100.0%) */
#define UI_AWG_P_WIDE      6u    /* Sinc: 1..1000 */
#define UI_AWG_P_HARM_N    7u    /* 谐波: 次数 1..8 */
#define UI_AWG_P_HARM_H    8u    /* 谐波: 编辑序号 1..n */
#define UI_AWG_P_HARM_A    9u    /* 谐波: 当前序号电压幅值(mV) */
#define UI_AWG_P_HARM_P   10u    /* 谐波: 当前序号相位 0.1° */
#define UI_AWG_P_AM_M     11u    /* AM: 调制比 x10 */
#define UI_AWG_P_FMFC     12u    /* AM/DSB/FM/PM: 载波比 1..256 */
#define UI_AWG_P_FM_KF    13u    /* FM: 调频灵敏度 x10 */
#define UI_AWG_P_PM_KP    14u    /* PM: 峰值相偏(周) x10 */
#define UI_AWG_P_ASK_CYC  15u    /* 2ASK: 每码元载波周期数 1/2/4/8 */
#define UI_AWG_P_ASK_BITS 16u    /* 2ASK: 码元数 8/16/32/64 */
#define UI_AWG_P_ASK_TYPE 17u    /* 2ASK: 码型 0=固定码 1=m序列 */
#define UI_AWG_P_ASK_SEL  18u    /* 2ASK 自定义: 正在编辑的码元序号 */
#define UI_AWG_P_ASK_VAL  19u    /* 2ASK 自定义: 该码元值(0/1) */

/* 范围 */
#define UI_AWG_FREQ_MIN_HZ      1
#define UI_AWG_FREQ_MAX_HZ      20000000
#define UI_AWG_FREQ_MAX_BRAM_HZ 100000
/* ★ 谐波(AWG_W_HARM)专用频率上限: 谐波是【软件合成一张 4096 点表】再由 BRAM-DDS 播放,
   硬件是 32 位相位累加器 + 120MHz 采样 ⇒ 5MHz 时频率字 = 5e6*35.7913941333 = 178,956,970,
   远小于 int32 上限(溢出临界约 60MHz); 此时每周期仍有 120e6/5e6 = 24 个相位码, 8 次谐波
   (40MHz) 也在 60MHz Nyquist 之内 ⇒ 可以放开到 5MHz。
   注意【只放开谐波】: AM/DSB/FM/PM/ASK 的表内含 fmfc(最多 256) 个载波周期, 基波一旦超过
   约 234kHz 载波就会越过 Nyquist 混叠 ⇒ 它们必须保留 100kHz 的限制。 */
#define UI_AWG_FREQ_MAX_HARM_HZ 5000000
/* ★ 满幅(与 PL 幅度定标一致, 实测): 普通 DDS 幅度寄存器 128 = 11.5Vpp(专业示波器实测, 原写 10.85Vpp),
   BRAM 幅度寄存器 1024 = 11.5Vpp。这里是界面幅度上限, 也是桥接换算的分母
   (helloworld.c 的 AWG_FULL_MVPP 引用它) —— 实测有偏差只改这一个数。
   注: 普通 DDS 幅度是 8 位(0~128), 1 LSB ≈ 89.8mV(11500/128); BRAM 是 0~1024, 1 LSB ≈ 11.2mV(11500/1024)。 */
#define UI_AWG_VPP_MAX_MV       11500   /* ★ 实测标定: 满幅 = 11.5Vpp(原写 10.85Vpp, 用专业示波器实测修正) */

/* 参数行描述 */
typedef struct {
    u8 id;      /* UI_AWG_P_xxx */
    u8 kind;    /* UI_AWG_K_xxx */
    u8 unit;    /* UI_AWG_U_xxx */
} ui_awg_row_t;

/* 一路输出(DAC1 或 DAC2) */
typedef struct {
    u8  on;
    u8  wave;           /* UI_AWG_W_xxx */
    s32 freq_hz;
    s32 vpp_mv;
    s32 phase_mdeg;   /* ★ 单位 0.1 度: 0..3600(=360.0 度); awg_ph12() 按 /3600 换算成 12 位相位字 */
    s32 duty_x10;
    s32 wide;
    s32 am_x10;
    s32 fmfc;
    s32 kf_x10;
    s32 kp_x10;
    s32 ask_cyc;
    s32 ask_bits;
    s32 ask_type;
    u8  ask_code[64];   /* 2ASK 自定义码序列(每码元 0/1) */
    u8  ask_sel;        /* 2ASK 自定义: 当前码元序号(1 基) */
    u8  harm_n;
    u8  harm_h;
    u16 harm_a[UI_AWG_HARM_MAX];   /* 各次电压幅值(mV), 不是百分比 */
    s32 harm_p[UI_AWG_HARM_MAX];
    u8  tab_dirty;
} ui_awg_ch_t;

/* 整体状态 */
typedef struct {
    ui_awg_ch_t ch[2];  /* ch[0] = DAC1(可普通可 BRAM), ch[1] = DAC2(只能普通) */
    u8 sel;
    u8 scroll;
    u8 need_apply;
    u8 redraw_params;   /* 参数区需要重画(滚动/参数变化时置位, 由 draw_live 消费) */
    u8 redraw_preview;  /* 预览区需要重画(BRAM 表重新生成后置位, 由 draw_live 消费) */
} ui_awg_t;

/* ---------------- 接口 ---------------- */
void ui_awg_default(ui_awg_t *a);

const char *ui_awg_wave_name(u8 wave);
const char *ui_awg_wave_short(u8 wave);   /* 按钮栏用的短名(7 个按钮宽度有限) */
u8   ui_awg_is_bram(u8 wave);
u8   ui_awg_wave_list(u8 ch, u8 *out);
s32  ui_awg_freq_max(u8 wave);
int  ui_awg_rows(const ui_awg_t *a, ui_awg_row_t *out, int max);
int  ui_awg_row_count(const ui_awg_t *a);
const char *ui_awg_row_name(const ui_awg_t *a, u8 id, char *tmp, int tmpsz);
void ui_awg_row_text(const ui_awg_t *a, u8 id, char *buf);
s32  ui_awg_row_get(const ui_awg_t *a, u8 id);
void ui_awg_row_set(ui_awg_t *a, u8 id, s32 v);
void ui_awg_row_step(ui_awg_t *a, u8 id, int dir);
/* 参数区命中部位(ui_awg_hit() 的返回值) */
#define UI_AWG_HIT_NONE   0
#define UI_AWG_HIT_BOX    1    /* 数值框: 弹数字键盘 */
#define UI_AWG_HIT_LT     2    /* 枚举行: 左箭头(减一档) */
#define UI_AWG_HIT_GT     3    /* 枚举行: 右箭头(加一档) */
#define UI_AWG_HIT_SBUP   4    /* 右侧滚动条上段: 上一行 */
#define UI_AWG_HIT_SBDN   5    /* 右侧滚动条下段: 下一行 */

/* 数字键盘的单位槽(与 ui_numkbd 的槽位号一一对应; 文字由 app 在 ui_app_default 里登记) */
#define UI_AWG_KU_VPP   2u     /* ★ 复用频谱仪已登记的 "Vpp"(=UI_SPEC_UNIT_VPP); 3 是 mVpp, 别弄错 */
#define UI_AWG_KU_KHZ   4u     /* 复用 "kHz" */
#define UI_AWG_KU_MHZ   5u     /* 复用 "MHz" */
#define UI_AWG_KU_HZ    8u     /* "Hz" */
#define UI_AWG_KU_PCT   9u     /* "%" */
#define UI_AWG_KU_DEG  10u     /* 度 */
#define UI_AWG_KU_INT  11u     /* 整数(不显示单位) */
#define UI_AWG_KU_X10  12u     /* "x0.1" */

/* 参数区命中: 返回 UI_AWG_HIT_*, *vis = 可见行序号(0..2), *id = 参数 id */
int  ui_awg_hit(const ui_awg_t *a, int x, int y, int *vis, u8 *id);

/* 数值行的键盘配置: 单位槽 / 小数位 / 输入串长度上限 / 初值串(不含单位) */
void ui_awg_row_kbd(const ui_awg_t *a, u8 id, u8 *ku, u8 *dec, u8 *len_max, char *init);

/* 键盘定点值(输入数字 x 1000) -> 参数内部量纲 */
s32  ui_awg_kbd_to_val(const ui_awg_t *a, u8 id, u8 ku, s32 kv);

void ui_awg_wave_next(ui_awg_t *a);
void ui_awg_wave_set(ui_awg_t *a, u8 wave);   /* 子界面选中某个波形(该通道不允许的会被忽略) */
void ui_awg_sel_toggle(ui_awg_t *a);
void ui_awg_output_toggle(ui_awg_t *a);
void ui_awg_scroll(ui_awg_t *a, int dir);

void ui_awg_draw(u32 fb, ui_awg_t *a);
void ui_awg_draw_live(u32 fb, ui_awg_t *a);
void ui_awg_draw_status(u32 fb, const ui_awg_t *a);
void ui_awg_draw_params(u32 fb, const ui_awg_t *a);
void ui_awg_draw_preview(u32 fb, const ui_awg_t *a);
void ui_awg_flush(u32 fb);
void ui_awg_flush_chrome(u32 fb);

/* ---- 旧接口兼容(ui_app.c 的按钮标签/动作还在用, 按钮栏在下一步再改) ---- */
const char *ui_awg_shape_name(u8 shape);
void ui_awg_fmt_freq(char *b, s32 hz);
void ui_awg_fmt_vpp(char *b, s32 mv);
void ui_awg_shape_next(ui_awg_t *a);
void ui_awg_freq_step(ui_awg_t *a, int dir);
void ui_awg_freq_decade(ui_awg_t *a, int dir);
void ui_awg_amp_step(ui_awg_t *a, int dir);

#endif /* UI_AWG_H */

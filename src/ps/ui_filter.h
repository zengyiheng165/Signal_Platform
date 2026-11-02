/******************************************************************************
 * ui_filter.h - 数字滤波器界面(第五个独立页面)
 *
 * 硬件现状(已核对 BD):
 *   - FIR 输入 = 顶层 ad_data_1(ADC1 原始 12bit), 与 ADC IP 并联, 无使能/旁路;
 *   - IIR 输入 = 同一个 ad_data_1;
 *   - 两者输出都进 wave_sel, 由 GPIO[3:2] 选谁出 DAC:
 *         sel=00 普通DDS / 01 = FIR / 10 = IIR / 11 = BRAM-DDS
 *   - fs 固定 = 8.19199MHz(clk_wiz_0 输出, ADC 内部 PLL 直出采样时钟), 不可切档;
 *   - 系数下发只走 filter.c 的两个函数(FIR_COEFFICIENT_RELOAD / IIR_COEFFICIENT_SET),
 *     本模块不直接碰任何寄存器。
 *
 * 刷新约定(与工程一致, 防闪屏):
 *   - 触摸只置标志/改状态, 大区域重画只在帧边界的 *_draw_live() 里做;
 *   - 液晶刷新一律按【整行】粒度(800*3 = 2400 字节)整条刷, 绝不按 x 裁;
 *   - 整屏重画只由 ui_app_draw() 在切页那一帧调用。
 ******************************************************************************/
#ifndef UI_FILTER_H
#define UI_FILTER_H

#include "xil_types.h"

/* ---------------- 页面布局(与其它页一致: 顶栏0..47 信息行48..71 主体72..438 按钮栏440..479) -- */
#define UI_FLT_Y0       72
#define UI_FLT_Y1       440
#define UI_FLT_H        (UI_FLT_Y1 - UI_FLT_Y0)

/* 参数行(左栏) */
#define UI_FLT_ROW_X    12
#define UI_FLT_ROW_Y    84
#define UI_FLT_ROW_W    360
#define UI_FLT_ROW_H    44
#define UI_FLT_ROW_GAP  6
#define UI_FLT_ROW_N    6          /* 类型/结构/阶数/窗/截止/输出 */

/* 数值键区(右栏): 直接显示已量化的系数, 便于自查 */
#define UI_FLT_K_X      388
#define UI_FLT_K_Y      84
#define UI_FLT_K_W      400
#define UI_FLT_K_H      348

/* ---------------- 参数取值 ---------------- */
#define UI_FLT_TYPE_LP  0u        /* 低通 */
#define UI_FLT_TYPE_HP  1u        /* 高通 */
#define UI_FLT_TYPE_BP  2u        /* 带通 */
#define UI_FLT_TYPE_BS  3u        /* 带阻 */
#define UI_FLT_TYPE_N   4u

#define UI_FLT_KIND_FIR 0u
#define UI_FLT_KIND_IIR 1u
#define UI_FLT_KIND_N   2u

#define UI_FLT_WIN_N    4u        /* 矩形/汉宁/汉明/布莱克曼 */

#define UI_FLT_IIR_MAX_ORDER 6    /* ★ 硬件: Ycoe0 是除法定标, 反馈只有 Ycoe1..7(=7 阶); 且 Xcoe7 是死抽头 => 分子实用 6 阶 */

/* 输出源 = GPIO[3:2] = wave_sel 的 sel 编码(必须一一对应!) */
#define UI_FLT_SRC_DDS  0u        /* sel=00 普通 DDS   */
#define UI_FLT_SRC_FIR  1u        /* sel=01 FIR 直出   */
#define UI_FLT_SRC_IIR  2u        /* sel=10 IIR 直出   */
#define UI_FLT_SRC_BRAM 3u        /* sel=11 BRAM-DDS   */

/* 状态 */
#define UI_FLT_ST_IDLE  0u        /* 未下发 */
#define UI_FLT_ST_OK    1u        /* 已下发 */
#define UI_FLT_ST_SAT   2u        /* 系数饱和(建议降阶或提高截止) */
#define UI_FLT_ST_FAIL  3u        /* 设计/量化失败 */

/* ---------------- 页面状态 ---------------- */
typedef struct {
    u8  type;             /* UI_FLT_TYPE_xxx */
    u8  kind;             /* UI_FLT_KIND_xxx */
    u8  order;            /* FIR: 低通 9 / 其它 8 (按类型自动定); IIR: 1..6(带通/带阻 <=3) */
    u8  win;              /* FIR 窗(0..3), IIR 时无效 */
    s32 fc_hz;            /* 低通/高通截止频率 */
    s32 f1_hz;            /* 带通/带阻下边 */
    s32 f2_hz;            /* 带通/带阻上边 */
    u8  out_on;           /* ★ 滤波器输出开关: 0 = 关(让信号发生器出声), 1 = 开(FIR/IIR 直出 DAC) */
    u8  status;           /* UI_FLT_ST_xxx */
    u8  row;              /* 当前选中的参数行(0..UI_FLT_ROW_N-1) */
    u8  need_apply;       /* ★ 请求重新设计+下发(只置位, 由主循环在帧边界消费并清零) */
    u8  redraw_params;    /* ★ 参数区需重画(触摸只置位, 由 draw_live 消费) */
    u8  redraw_bar;       /* ★ 顶栏+信息行需重画(按钮栏由触摸层 ui_touch_draw_bar 自己刷) */
    u8  coef_n;           /* 本次下发的系数个数(显示用) */
    s32 coef_max;         /* 量化后 |系数| 最大值(显示用, 便于自查是否饱和) */
    s16 coef[16];         /* ★ FIR: 下发的 10 个抽头; IIR: 下发的【分子】系数 b[0..coef_n-1] */
    s16 coef_y[16];       /* ★ IIR 专用: 下发的【分母】系数 a[0..coef_n-1](FIR 时恒 0, 不显示) */
    s32 q_shift;          /* IIR 的 q(移位量, 显示用) */
    /* ★ 采样率(可切换; 只【读】clk_wiz1.h 里那张频率表, 表本身一个字节都不改):
       fs_hz  = 当前 ADC 采样率(Hz): 既用于显示, 也用于【滤波器设计】的归一化;
       fs_idx = 在 ui_osc_fs_scope_tbl[] 里的档位下标(0..ADC_FS_SCOPE_N-1);
       fs_req = 1 = 请求切换采样率, 交 helloworld 主循环在帧边界消费(统一走 adc_fs_apply)。 */
    s32 fs_hz;
    u8  fs_idx;
    u8  fs_req;
} ui_filter_t;

/* ---------------- 钩子(与 ui_awg 同一套结构) ---------------- */
void ui_filter_default(ui_filter_t *a);

/* 整屏重画: 只在切页那一帧由 ui_app_draw() 调用(内含 lcd_clear) */
void ui_filter_draw(u32 fb, ui_filter_t *a);

/* 帧边界增量刷新: 消费 need_apply / redraw_params / redraw_bar */
void ui_filter_draw_live(u32 fb, ui_filter_t *a);

/* 顶栏(0..47) + 信息行(48..71) */
void ui_filter_draw_status(u32 fb, const ui_filter_t *a);

/* 参数区 + 系数区(72..439) */
void ui_filter_draw_params(u32 fb, const ui_filter_t *a);

/* 液晶刷新: 一律整行整条刷(这是防撕裂的关键) */
void ui_filter_flush(u32 fb);
void ui_filter_flush_chrome(u32 fb);

/* 触摸命中: 返回 0 = 没命中; 否则返回行号/特殊码, 由 ui_touch.c 决定动作 */
int  ui_filter_hit(const ui_filter_t *a, int x, int y, u8 *id);

/* 参数行步进(dir>0 加 / <0 减), 内部只改状态并置 redraw_params */
void ui_filter_row_step(ui_filter_t *a, int dir);

/* ★ 返回当前该给 wave_sel 的选择码(AWG_SRC_DDS/FIR/IIR), 不是字符串 */
u8   ui_filter_sel(const ui_filter_t *a);

#endif /* UI_FILTER_H */
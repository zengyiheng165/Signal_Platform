/******************************************************************************
 * ui_screens.h - LCD 界面层(基于 lcd_gfx + 中文点阵字库)
 *
 * 双路示波器界面(ui_osc_*), 面向真实 ADC 实时显示:
 *   布局(800x480):
 *     y=  0..47   顶栏: "双路示波器"(32px) + 已同步/未同步 + 运行/停止
 *     y= 48..71   信息行: 时基 / 触发源 / 边沿 / 模式 / 触发电平
 *     y= 72..375  波形区: 10x8 格(每格 80x38), CH1 黄 / CH2 天蓝
 *     y=376..479  测量区: 每通道三行 —— 峰峰值|最大值, 最小值|有效值, 频率|周期
 *
 * 接真实 ADC 的推荐流程:
 *   1) ui_osc_default(&osc);
 *      osc.sample_rate = 采样率(Hz);   osc.adc_bits = 位数(如 12);
 *      osc.adc_mv_fs  = 满量程(mV);    osc.adc_zero = 零点码值(如 2048);
 *   2) osc.ch[0].data = adc_buf0;  osc.ch[0].len = N;   
 *      osc.ch[1].data = adc_buf1;  osc.ch[1].len = N;
 *   3) 上电或换信号后调用一次 ui_osc_autoset(&osc);
 *      —— 自动选纵向档位、自动挑触发源(幅度大的通道)、自动定触发电平、自动时基
 *   4) 每采集完一帧就刷新一次(测量项会实时更新):
 *        ui_osc_draw(nxt_fb, &osc);   
 *        lcd_flush(nxt_fb);
 *        switch_frame();             
 *
 * 触发稳定性:
 *   - 触发带迟滞: 默认自动取 Vpp/16, 也可用 ui_osc_trig_hyst_step() 手动增减;
 *   - AUTO 模式找不到触发点时从缓冲起点显示, 状态栏显示"未同步";
 *   - 需要 NORMAL(未触发保持上一帧)时: if (ui_osc_refresh(&osc) || mode!=NORMAL) 再画。
 ******************************************************************************/
#ifndef UI_SCREENS_H
#define UI_SCREENS_H
  
#include "xil_types.h"

/* ---------------- 布局(800x480) ---------------- */
#define UI_OSC_HEADER_H   48          /* 顶栏高度 */
#define UI_OSC_INFO_Y     48          /* 信息行 */
#define UI_OSC_INFO_H     24
#define UI_OSC_WAVE_X     0           /* 波形区 */
#define UI_OSC_WAVE_Y     72
#define UI_OSC_WAVE_W     800
#define UI_OSC_WAVE_H     264         /* 纵向 8 格 x 33 */
#define UI_OSC_DIV_W      80          /* 横向 10 格 */
#define UI_OSC_DIV_H      33          /* 纵向每格高度 */
#define UI_OSC_BOTTOM_Y   336         /* 测量区(每通道三行) */
#define UI_OSC_BOTTOM_H   104
#define UI_OSC_CH_PANEL_W 400         /* 每个通道面板宽 */
#define UI_OSC_CH_TAB_H   20          /* 通道标题条高 */
#define UI_OSC_BTN_Y      440         /* 触摸按钮栏(ui_touch.c): y=440..479 */
#define UI_OSC_BTN_H      40

/* ---------------- ADC 采样率(UI 只提请求, 真正改 clk_wiz 由应用做) ---------------- */
/* ★ 采样时钟由 clk_wiz1.c 的预设表给出, 表外的频率 clk_wiz_cfg() 会直接忽略(什么都不改),
   所以"示波器"只能用表里的整数频率; 6.144/8.192/16.384/32.768/49.152/65.536
   这几个小数频率留给频谱仪(FFT 的 bin 间隔/频率轴/跨度都按 8.192MHz 算)。 */
#define ADC_FS_SCOPE_N   7        /* 示波器可用采样率个数: 5/6/8/16/32/48/64 MHz */
#define ADC_FS_SPEC_MHZ  8.192    /* 频谱仪固定用这个采样率 */
#define ADC_FS_CYCLES    8        /* 一条记录装几个周期: fs = (记录点数/N)*f。
                                     自动时基只显示 2 个周期/屏, 所以之后还能放大 ~4 倍看更多周期 */

/* ---------------- 配色(RGB565): 界面黑白灰, 仅波形/通道用彩色 ---------------- */
#define UI_OSC_COLOR_BG     0x0000    /* 波形区底色   纯黑 */
#define UI_OSC_COLOR_BAR    0x0000    /* 顶栏底色     纯黑 */
#define UI_OSC_COLOR_INFO   0x0000    /* 信息行底色   纯黑 */
#define UI_OSC_COLOR_PANEL  0x0000    /* 测量区底色   纯黑 */
#define UI_OSC_COLOR_TABBG  0x18C3    /* 备用深灰 */
#define UI_OSC_COLOR_GRID   0x2925    /* 网格         #262626 低对比不抢眼 */
#define UI_OSC_COLOR_MID    0x4A49    /* 中央十字线   中性灰 */
#define UI_OSC_COLOR_FRAME  0xFFFF    /* 边框/分隔线  纯白 */
#define UI_OSC_COLOR_DIM    0x5ACB    /* 触发虚线     中性灰 */
#define UI_OSC_COLOR_LABEL  0xA534    /* 标签文字     浅灰 */
#define UI_OSC_COLOR_TEXT   0xFFFF    /* 主文字       白 */
#define UI_OSC_COLOR_OK     0xFFFF    /* 已同步       白 */
#define UI_OSC_COLOR_BAD    0x8C71    /* 未同步       灰 */
#define UI_OSC_COLOR_KBD_BG 0xCE79    /* 数字键盘面板底 灰白 #CECECE(键/输入框仍是纯白) */
/* 运行/停止药丸: 白底黑字 / 深灰底白字 */
#define UI_OSC_COLOR_RUN_BG   0xFFFF
#define UI_OSC_COLOR_RUN_FG   0x0000
#define UI_OSC_COLOR_STOP_BG  0x18C3
#define UI_OSC_COLOR_STOP_FG  0xFFFF
/* ---- 以下彩色只用于波形与通道标识 ---- */
#define UI_OSC_COLOR_CH1    0xFFE0    /* 通道1 黄色    #FFFF00 纯黄 */
#define UI_OSC_COLOR_CH2    0x05FF    /* 通道2 天蓝色  #00BFFF 深天蓝 */
/* 通道标签条底色: 通道色的深色版(两通道暗度相当), 条上文字用对应通道色 */
#define UI_OSC_COLOR_CH1_BG 0x5240    /* 深黄   #554A00, 黄字对比 8.5:1 */
#define UI_OSC_COLOR_CH2_BG 0x028C    /* 深天蓝 #005063, 天蓝字对比 4.2:1 */

/* 触发边沿 */
#define UI_OSC_EDGE_RISE 0
#define UI_OSC_EDGE_FALL 1
/* 触发模式 */
#define UI_OSC_MODE_AUTO   0
#define UI_OSC_MODE_NORMAL 1
#define UI_OSC_MODE_SINGLE 2

/* 单通道测量结果 */
typedef struct {
    s32 vmin;        /* 最小值(ADC 码值) */
    s32 vmax;        /* 最大值 */
    s32 vpp;         /* 峰峰值 */
    s32 mean;        /* 平均值 */
    s32 rms;         /* 有效值(含直流分量) */
    s64 freq_mhz;
    u8  sq_wave;     /* ★ 1 = 本帧判定为方波/脉冲(样点大量贴在 min/max 上): 画波形时【不做 sinc 插值】, 走 min/max, 免得阶跃处出现过冲尖刺 */
                     /* ★ 频率, 单位 0.001Hz(毫赫兹), 0 = 无法测量。
                        防 32 位: 32 位在 >2.147MHz 会溢出, 高频时频率漂移。 */
} ui_osc_meas_t;

/* 单个通道 */
typedef struct {
    const s16 *data;         /* 采样数据(ADC 码值) */
    u32        len;          /* 样本个数 */
    s32        vdiv;         /* 纵向档位: 每格对应码值 (>0) */
    s32        center;       /* 显示区中线对应的码值(纵向位置) */
    s32        time_per_div_ns; /* 横向档位: 该通道每格时间(ns) */
    u16        color;        /* 波形颜色 */
    u8         on;           /* 1=显示该通道, 0=关闭(不画波形, 数值显示 --) */
} ui_osc_ch_t;

/* 示波器整体状态 */
typedef struct {
    ui_osc_ch_t ch[2];

    /* 采集参数 */
    s32 sample_rate;      /* 采样率 Hz */
    s32 adc_bits;         /* ADC 位数, 如 12 */
    s32 adc_mv_fs;        /* ADC 满量程 mV, 如 3300 */
    s32 adc_zero;         /* 零点对应码值, 如 2048 */

    /* 注意: 水平档位(每格时间)在 ui_osc_ch_t 里, 每个通道各一份 */

    /* 触发设置 */
    u8  trig_src;         /* 0=CH1, 1=CH2 */
    u8  trig_edge;        /* UI_OSC_EDGE_xxx */
    u8  trig_mode;        /* UI_OSC_MODE_xxx */
    u8  run;              /* 1=运行, 0=停止 */
    u8  sel_ch;           /* 触摸"档位±/时基±"作用的通道: 0=CH1, 1=CH2 */
    s32 trig_level;       /* 触发电平(码值) */
    s32 trig_hyst;        /* 触发迟滞(码值); <=0 表示自动(Vpp/16) */
    u8  trig_found;       /* 本次刷新是否找到触发点 */
    u32 trig_pos;         /* 触发点在数据中的下标 */
    double trig_off;      /* ★ 亚样点触发: 真实过零点相对 trig_pos 的小数偏移(样点, ∈(-1,0])。
                            触发搜索只能给出整数样点下标, 而过零点在 d[i-1] 与 d[i] 之间;
                            这个小数位置每帧随 ADC 采样相位漂移, 不插值补偿的话波形会被量化成
                            i 或 i-1 -> 整屏左右抖 1 个样点(= 80/每格样点数 个像素),
                            频率越高抖得越远(62MSPS 下 1MHz 6px / 5MHz 32px / 10MHz 64px)。
                            由触发沿两侧样点线性内插 + 带限重建修正得到; 0 = 不补偿 */

    /* 测量结果(由 ui_osc_refresh/autoset 填充) */
    ui_osc_meas_t meas[2];

    /* ★ 采样率变更请求(单位 0.001MHz, 0 = 无请求):
       ui_osc_autoset() 按测到的频率填, 应用调 clk_wiz_cfg 之后清零 */
    s32 fs_req_mhz1000;
} ui_osc_t;

/* ---------------- 接口 ---------------- */

void ui_osc_default(ui_osc_t *s);         /* 填充默认设置 */
u8   ui_osc_refresh(ui_osc_t *s);         /* 重测 + 重新触发, 返回 1=找到触发点 */
void ui_osc_measure(ui_osc_t *s, int ch); /* 单通道测量 */
void ui_osc_autoset(ui_osc_t *s);         /* 自动设置(档位/触发源/电平/时基) */

/* 示波器可用的整数采样率(与 clk_wiz1.h 的表对应): 5/6/8/16/32/48/64 MHz */
int    ui_osc_fs_scope_count(void);
double ui_osc_fs_scope_at(int idx);       /* idx 越界自动夹到端点 */
double ui_osc_fs_snap(double mhz);        /* 吸附到最近的可用频率 */

/* 手动调节 */
void ui_osc_trig_level_step(ui_osc_t *s, int dir);   /* dir>0 电平上移 */
void ui_osc_trig_hyst_step(ui_osc_t *s, int dir);    /* dir>0 迟滞变大(更抗噪) */
void ui_osc_trig_edge_toggle(ui_osc_t *s);
void ui_osc_trig_src_toggle(ui_osc_t *s);
void ui_osc_timebase_step(ui_osc_t *s, int dir);     /* 当前通道: dir>0 每格时间变大 */
void ui_osc_vdiv_step(ui_osc_t *s, int ch, int dir); /* dir>0 档位变大(波形变小) */
void ui_osc_sel_ch_toggle(ui_osc_t *s);              /* 切换"档位±/时基±"的通道 CH1/CH2 */
void ui_osc_ch_toggle(ui_osc_t *s, int ch);          /* 打开/关闭某通道(点通道名) */
void ui_osc_run_stop(ui_osc_t *s);

s32  ui_osc_samples_per_div(const ui_osc_t *s, int ch);  /* 指定通道的每格样本数 */

/* 绘制: 整屏 / 只波形区 / 只测量数值(内部都会重新测量与触发) */
void ui_osc_draw(u32 fb, ui_osc_t *s);
void ui_osc_draw_wave(u32 fb, ui_osc_t *s);
void ui_osc_draw_values(u32 fb, ui_osc_t *s);
/* 只叠加波形(不擦屏/不画网格): 单缓冲原地刷新用 */
void ui_osc_draw_trace(u32 fb, ui_osc_t *s);
/* 单缓冲"原地"刷新: 擦旧迹+补网格+画新迹+更新测量值; 不换源, 不闪 */
void ui_osc_invalidate(void);      /* 切界面/贴底板后作废"上一帧迹" */
void ui_osc_draw_live(u32 fb, ui_osc_t *s);
/* 局部 D-Cache 刷新(区域连续且 cache line 对齐) */
void ui_osc_flush_wave(u32 fb);
void ui_osc_flush_values(u32 fb);
/* 局部重画静态部分(按钮动作后刷新文字用, 避免整屏重画引起闪动) */
void ui_osc_draw_status(u32 fb, ui_osc_t *s);   /* 顶栏: 运行/停止 + 已同步/未同步 */
void ui_osc_draw_info(u32 fb, ui_osc_t *s);     /* 信息行: 时基/触发源/边沿/模式/电平 */
void ui_osc_draw_tabs(u32 fb, ui_osc_t *s);     /* 通道标题条: x.xxV/格 */
void ui_osc_flush_chrome(u32 fb);               /* 上述区域的 D-Cache 刷新 */
/* 文字绘制/测宽(公开给按钮栏等其它模块复用, 与界面同一套字库) */
int  ui_draw_text(u32 fb, int x, int y, const char *s, u16 color, int scale);
u32  ui_text_size(const char *s, int scale);
/* 波形区网格(10x8 格 + 白边框): 供信号发生器等其它界面复用 */
void ui_wave_grid(u32 fb);
/* 底部整条(通道条+测量值+面板分隔线): 关掉"界面"窗口后整条恢复用 */
void ui_osc_draw_bottom(u32 fb, ui_osc_t *s);

#endif /* UI_SCREENS_H */

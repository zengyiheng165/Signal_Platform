/******************************************************************************
 * ui_spec.h - 频谱仪界面(单通道显示: CH1 / CH2 可切换)
 *
 * 布局(800x480, 与示波器/信号发生器完全同一套边框、配色、字库):
 *   y=  0..47   顶栏: "频谱仪"(32px) + 运行/停止 + 当前通道(CH1/CH2)
 *   y= 48..71   信息行: 采样率 / 谱线数 / 分辨率 / 跨度 / 纵轴
 *   y= 72..335  频谱图: 共用 ui_wave_grid() 的 10x8 网格 + 幅度曲线
 *               横轴 = 0 .. span(默认 20kHz, 可 1-2-5 档缩放)
 *               纵轴 = dB(默认 10dB/格, 顶格 0dBFS) 或线性(0..满量程)
 *   y=336..439  读数区: 第一行 峰值频率/峰值幅度/平均底噪, 第二行 频率刻度
 *   y=440..479  触摸按钮栏(ui_touch.c)
 *
 * 数据来源(只有一个入口):
 *   ui_spec_set_spectrum(&app.spec, mag, nbins, sample_rate);
 *   mag[] 是 PL 的 FFT IP 算出来的线性幅度(ADC 码值, 满量程 = UI_SPEC_REF_MAG),
 *   nbins = FFT 点数 / 2; 本模块只做"谱线 -> 屏幕列"的映射与读数(不做插值)。
 ******************************************************************************/
#ifndef UI_SPEC_H
#define UI_SPEC_H

#include "xil_types.h"

#define UI_SPEC_COLS      800      /* 绘图列数(= 波形区宽度) */
#define UI_SPEC_REF_MAG   2048     /* 0dBFS 参考幅度(12bit ADC: 2^11) */
#define UI_SPEC_DB_RANGE  80       /* dB 纵轴总动态范围(dB): 8 格 x 10dB */

/* 跨度(Hz): 1-2-5 档 */
#define UI_SPEC_SPAN_MIN  1000
#define UI_SPEC_SPAN_DEF  20000    /* 默认 20kHz(对着 5kHz 正弦看比较合适) */

/* ---------------- 顶栏右上角的"设置"齿轮按钮 ----------------
   放在顶栏(y=8..39)而不是频谱绘图区(y=72..335)里, 两个理由:
   1) 绘图区是"原地刷新"逐列擦旧迹/补网格的目标, 按钮画进去会被当成网格的一部分擦掉
      或留下残影(ui_spec_draw_live() 只按上一帧记录的列范围回补背景);
   2) 验证台有"按钮刷新不碰频谱区"的检查(统计绘图区矩形内被改动的像素数),
      按钮放顶栏后, 点它的刷新只动自己这 40x32 一小块, 该检查保持 0。
   横坐标选在"运行/停止"药丸(x=692..787)左边, 与任何已有热区都不重叠。 */
#define UI_SPEC_SETBTN_X  632
#define UI_SPEC_SETBTN_Y  8
#define UI_SPEC_SETBTN_W  40
#define UI_SPEC_SETBTN_H  32

/* ★ Chirp-Z 按钮: 放在"设置"齿轮左侧, 间距 8px(不贴着) */
#define UI_CZT_BTN_X   544     /* = 632 - 8 - 80 */
#define UI_CZT_BTN_Y   8
#define UI_CZT_BTN_W   80
#define UI_CZT_BTN_H   32

/* ★ Chirp-Z 模式子窗口: 屏幕正中, 420x240(选模式2 时下面多出 3 个输入行) */
#define UI_CZT_POP_X   190     /* = (800-420)/2 */
#define UI_CZT_POP_Y   120     /* = (480-240)/2 */
#define UI_CZT_POP_W   420
#define UI_CZT_POP_H   240
#define UI_CZT_POP_RH  30      /* 每行高 */
#define UI_CZT_POP_GAP 6       /* 模式2 的三个输入行与上面三行之间的间隔 */
/* ★ 模式1 的 ±N bin: 左右箭头热区(绘制与触摸共用同一套坐标) */
#define UI_CZT_POP_BIN_X    (UI_CZT_POP_X + 186)   /* 该行右半部起点 */
#define UI_CZT_POP_ARW_W    34                     /* 一个箭头热区宽度 */
#define UI_CZT_POP_VAL_DX   40                     /* 数值文字偏移 */
#define UI_CZT_POP_ARW2_DX  92                     /* 右箭头偏移 */
/* 齿轮图标本体: 1bpp 掩码 32x32, 居中放进上面 40x32 的热区 (x=632+4=636, y=8+0=8)。
   掩码里实际有线条的范围是 28x28(齿顶半径 14), 四周留 2px 余量 -> 不会压到按钮边框。 */
#define UI_SPEC_GEAR_W    32
#define UI_SPEC_GEAR_H    32

/* 谱峰列表: 最多记住几个峰(给 Next Peak / Last Peak 跳转用) */
#define UI_SPEC_NPEAK     16     /* ★ 原 8: 放宽到 16, 保证"有多少真峰就能标到多少个" */

/* 最多同时显示几条谱线的数值(每条独立开关) */
#define UI_SPEC_NMARK     8

/* 线性纵轴时把谱峰幅度换算成电压的满量程标定(mVpp, 与示波器的 adc_mv_fs 一致) */
#define UI_SPEC_MV_FS     10330

/* ---------------- 参考电压(频谱仪"设置"子窗口的第 1 项) ----------------
   含义: 线性纵轴上端的参考电压(口径看单位档: V/mV = 峰值, Vpp/mVpp = 峰峰值), 纵轴下端固定 0。
   ★ 内部统一用整数 mV 存(默认 3300, 即 3.300V 或 3.300Vpp), 不用浮点, 避免精度/库依赖;
   ★ 单位档(ref_unit)只影响"显示与输入解释", 内部值恒为整数 mV;
   ★ 最小分辨率 1mV, 不允许负值(下限 0mV)。
   ★ 第二步已把它接进线性纵轴(ui_spec_lin_y): 上端 = ref_mv 对应的码值;
     dB 模式不受影响。 */
#define UI_SPEC_REF_MV_MIN   0        /* 下限 0mV: 不允许负值 */
#define UI_SPEC_REF_MV_MAX   5500     /* 上限 5500mV = 5.500V */
#define UI_SPEC_REF_MV_DEF   3300     /* 默认 3300mV = 3.300V */
#define UI_SPEC_REF_DEC      3        /* V/Vpp 档固定 3 位小数 = 1mV 分辨率 */

/* 参考电压的显示单位档: 单位按钮每点一下走一格 —— V -> mV -> Vpp -> mVpp -> V
   ★ V/mV 是"峰值"口径, Vpp/mVpp 是"峰峰值"口径, 同一段电压两者差 2 倍;
     切单位**不换算** ref_mv(数值原样保留), 只换口径 —— 所以谱线长度会跟着变
     (见 ui_spec_lin_y): 参考 3.3V 时 3.00Vpp 的谱线约半屏, 3.3Vpp 时接近顶格。
   ★ 显示格式只分"伏/毫伏": V/Vpp = "3.300", mV/mVpp = "3300"; 内部恒为整数 mV。 */
#define UI_SPEC_UNIT_V       0u       /* V    : "3.300" */
#define UI_SPEC_UNIT_MV      1u       /* mV   : "3300"  */
#define UI_SPEC_UNIT_VPP     2u       /* Vpp  : "3.300" */
#define UI_SPEC_UNIT_MVPP    3u       /* mVpp : "3300"  */
#define UI_SPEC_UNIT_N       4u       /* 单位档个数(循环切换用) */

/* ---------------- 频谱仪采样率(设置窗第 2 项) ----------------
   可选档位由宿主登记: helloworld.c 从 clk_wiz1.h 的表里挑"小数频率"那五档
   (6.144 / 8.192 / 16.384 / 32.768 / 49.152 MHz)升序登记一次;
   UI 只负责显示当前档 + 最右侧上下箭头改档, 真正改时钟由宿主在主循环里做:
       if (s->fs_req != 0) { adc_fs_apply(ui_spec_fs_mhz(s), 1); s->fs_req = 0; }
   ★ 档位表是宿主登记的, 不属于默认值 -> ui_spec_default() 会原样保留它。 */
#define UI_SPEC_FS_N   13       /* XXFSN: 8->13: 吃下 clk_wiz1.h 频率表全部 13 档(含 5/6/8/16/32/48/64 整数 MHz), 频率表本身未改 */

/* 峰值搜索用: 最多记住多少条全带谱线(应用侧给 N/2 条 FFT 谱线:
   16384 点 FFT -> 8192 条, 所以这里必须 >= 8192, 否则峰值搜索只能搜到低半带) */
#define UI_SPEC_FULL_BINS 8192

/* 每列在屏幕上的纵向范围(原地刷新用: 与 ui_draw_trace 同样的逐列记录法) */
typedef struct {
    s16 top;
    s16 bot;
    u8  on;
} ui_spec_col_t;

/* 一个谱峰(本帧找出来的局部极大, 给 Next/Last Peak 跳转用) */
typedef struct {
    s16 x;        /* 所在屏幕列 */
    s32 hz;       /* 频率 Hz */
    s16 db10;     /* 幅度 0.1dB(0 = 满量程) */
    u16 mag;      /* 线性幅度(ADC 码值) */
} ui_spec_peak_t;

/* 频谱仪整体状态 */
typedef struct {
    /* 采集/显示参数 */
    s32 sample_rate;      /* 采样率 Hz */
    s32 full_span;        /* 最大跨度 = sample_rate/2 */
    s32 span;             /* 当前显示跨度 Hz */
    s32 ref_mag;          /* 0dBFS 对应的线性幅度(默认 UI_SPEC_REF_MAG) */
    s32 mv_fs;            /* ADC 满量程 mVpp(线性纵轴换算电压用, 默认 UI_SPEC_MV_FS) */
    s32 nbins;            /* 应用侧给的谱线数(0 = 还没有数据) */

    /* 参考电压("设置"子窗口第 1 项, 只给线性纵轴用):
       ★ 内部恒为整数 mV(默认 3300), ref_unit 只影响显示/输入解释, 不改 ref_mv;
       ★ 第二步已接进线性纵轴映射(ui_spec_lin_y / ui_spec_ref_mag)。 */
    s32 ref_mv;           /* 参考电压 mV, 范围 UI_SPEC_REF_MV_MIN..MAX(不允许负值) */
    u8  ref_unit;         /* 显示单位档: V(0) / mV(1) / Vpp(2) / mVpp(3), 见 UI_SPEC_UNIT_* */
    u8  win;              /* ★ FFT 窗: 0=汉宁 1=汉明 2=布莱克曼 3=矩形(不加窗); 默认 0 = 与原来行为一致。乘窗在 helloworld.c 调用 fft_windows.c 的同名函数, 幅度按各窗相干增益归一 */

    /* 设置窗第 2 项"采样率": 档位表(宿主登记) + 当前档 + 变更请求 */
    u8  fs_n;                       /* 已登记档数(0 = 宿主还没登记 -> 箭头按不动) */
    u8  fs_idx;                     /* 当前档(0..fs_n-1) */
    u8  fs_req;                     /* 1 = 这一档还没写进时钟(宿主主循环处理) */
    double fs_mhz[UI_SPEC_FS_N];    /* 各档频率(MHz), 升序 */

    /* 设置窗第 3/4 项"频率起始/频率终止": 显示区间 = [f_start, f_start + span];
       span 仍然是"区间宽度", 所以"跨度+/-"按钮只改 f_start+span(也就是只动频率终止)。
       两个单位档只影响显示与输入解释, 内部恒为 Hz。 */
    s32 f_start;                    /* 显示区间起点(Hz, >= 0), 默认 0 = 原来的 0~跨度 */
    u8  f_start_unit;               /* 起始频率显示单位: UI_SPEC_FU_KHZ / UI_SPEC_FU_MHZ */
    u8  f_stop_unit;                /* 终止频率显示单位 */
    u8  chrome_req;                 /* 1 = 频率刻度/信息行要补画一次(见 ui_spec_sync_chrome) */
    u8  info_req;                   /* ★ 轻量: 只重画信息栏那一行(不动刻度/边框/频谱区, 避免闪) */

    /* 显示开关 */
    u8  ch;               /* 0 = CH1, 1 = CH2 */
    u8  run;              /* 1 = 运行(刷新), 0 = 停止(冻结显示) */
    u8  db;               /* 1 = dB 纵轴, 0 = 线性纵轴 */
    u8  valid;            /* 1 = 至少算过一帧 */
    u8  cur_init;         /* 1 = 光标已经放到过最大峰 */

    /* 逐列结果(线性幅度 + 0.1dB 值; dB 值 0 = 满量程) */
    u16 col_mag[UI_SPEC_COLS];
    s16 col_db[UI_SPEC_COLS];

    /* 测量结果 */
    s32 peak_x;           /* 峰值所在列 */
    s32 peak_hz;          /* 峰值频率 Hz */
    u8  czt_mode;         /* ★ Chirp-Z 模式: 0=关 1=峰值精测 2=自定义频段(界面用) */
    u8  czt_peak_bins;    /* ★ 模式1 频段宽度: ±N 个 bin (1..8) */
    u8  czt_popup;        /* ★ 模式子窗口是否开着 */
    u8  czt_pop_draw;
    u8  czt_row_draw;     /* ★ 轻量: 只重画模式1 那一行(点箭头时用, 避免整块重画闪烁) */     /* ★ 子窗口需要画(帧边界消费) */
    /* ★ 模式2 的三个独立参数(与频谱仪自己的 f_start/span 完全分开, 互不影响) */
    s32 czt_f_start;      /*   CZT 起始频率 Hz */
    s32 czt_f_end;        /*   CZT 终止频率 Hz */
    u16 czt_m;            /*   CZT 点数 M */
    s32 czt_fine_hz;      /* ★ CZT 模式1 精测频率 Hz(0 = 本帧没算/无效) */
    s32 czt_fine_db;      /* ★ CZT 幅值(0.1dB, 与 peak_db 同量纲) */
    /* ★ 模式2(自定义频段)的曲线数据: 由 helloworld.c 每帧填, 显示时按 a 方案替换频谱曲线 */
    u16 czt2_n;           /* 曲线点数 = m (2..8192) */
    s32 czt2_f0;          /* 第一点频率 Hz */
    s32 czt2_span;        /* ★ 模式2 的带宽(Hz): 底部频率刻度用它, 避免由 czt2_df(mHz) 反推带来的几 Hz 误差 */
    s32 czt2_df;          /* 点间隔(mHz): 用 mHz 存, 才能表达小于 1Hz 的栅格 */
    double czt2_mv[8193];   /* XXDBL: 由 u16 改为 double —— 原来 1 个 LSB ≈ 0.033 码, 与"主瓣顶相邻 bin 的真实差 0.02~0.08 码"同量级, 会糊掉波形; 改 double 后显示/曲线端近乎无损(只是存放精度, 不是插值)。 */    /* ★ 257->8193(与 czt2_abs 同步) */
    /* ★ 模式2 的峰值: 在【全精度】的 czt2_abs[] 上求出(不能用 czt2_mv[] 的整数比大小:
       主瓣顶部相邻 bin 的幅值差只有零点几, 取整后谁大谁小由舍入决定 -> 峰频会在几个 bin 之间乱跳)。
       czt2_peak_i = 峰所在的 bin 序号; czt2_peak_hz = 对应的频率(Hz)。 */
    u16 czt2_peak_i;
    s32 czt2_peak_hz;     /* ★ 峰值频率(Hz): 由 czt2_abs[] 全精度求出, 界面直接用它显示"Z2峰"与左上角读数 */
    u8  czt2_dirty;       /* ★ 1 = 曲线数据刚更新, 需要整块重画一次 */
    u8  czt2_relayout;    /* ★ 模式2: 置1 表示"下帧需要重新铺底+画网格"(参数/量程/模式变化时置, 平时不铺底 -> 不再每帧刷 712KB) */
    u16 czt_fine_mag;     /* ★ CZT 峰幅度(与 col_mag 同标度 -> 数值框/光标据此显示 Vpp) */
    s32 peak_db;          /* 峰值幅度 0.1dB */
    s32 avg_db;           /* 平均底噪 0.1dB */

    /* 内部状态(应用侧不用管) */
    ui_spec_col_t prev[UI_SPEC_COLS];  /* 屏幕上已经画出来的每列范围 */
    u8  prev_valid;
    u32 frames;

    /* 本帧找出来的谱峰列表(Next/Last Peak 用) */
    u8  mk_n;                     /* 峰数(0 = 这一帧没有明显峰) */
    ui_spec_peak_t mk[UI_SPEC_NPEAK];

    u16 full_mag[UI_SPEC_FULL_BINS];  /* 应用侧给的全带谱线(峰值搜索用) */
    u16 full_n;                       /* 全带谱线条数(0 = 只有显示谱) */

    /* 峰值光标: 白色边框的透明倒三角(一直显示), 停在某个谱峰上 */
    s32 cur_hz;                   /* 光标频率 Hz */
    s16 cur_db;                   /* 当前该频率处的幅度 0.1dB */
    u16 cur_mag;                  /* 当前该频率处的线性幅度 */
    s16 cur_tx;                   /* 上一帧三角的包围盒(擦除用) */
    s16 cur_ty;
    s16 cur_tw;
    s16 cur_th;

    /* 每条谱线独立的数值框: 记频率 + 上一帧矩形; Peak 按钮逐条开关 */
    u8  mrk_n;                    /* 已打开数值框的谱线条数 */
    s32 mrk_hz[UI_SPEC_NMARK];    /* 它们的频率 Hz */
    s16 mrk_tx[UI_SPEC_NMARK];
    s16 mrk_ty[UI_SPEC_NMARK];
    s16 mrk_tw[UI_SPEC_NMARK];
    s16 mrk_th[UI_SPEC_NMARK];
} ui_spec_t;

/* ---------------- 接口 ---------------- */
void ui_spec_default(ui_spec_t *s);

/* 按钮动作 */
void ui_spec_ch_toggle(ui_spec_t *s);        /* CH1 <-> CH2 */
void ui_spec_run_stop(ui_spec_t *s);
void ui_spec_db_toggle(ui_spec_t *s);        /* dB / 线性 */
void ui_spec_span_step(ui_spec_t *s, int dir); /* dir>0 跨度变大 */
void ui_spec_span_reset(ui_spec_t *s);       /* 跨度复位到默认 */
void ui_spec_peak_search(ui_spec_t *s);       /* Search: 找最大峰值谱线, 自动调跨度并把光标放上去 */
void ui_spec_peak_val_toggle(ui_spec_t *s);   /* Peak: 切换光标所在谱线的数值显示 */
int  ui_spec_cur_marked(const ui_spec_t *s);  /* 光标那条谱线的数值框是否已打开 */
void ui_spec_peak_next(ui_spec_t *s, int dir);/* Next/Last Peak: dir>0 往高频方向跳 */

/* 数据入口: PL 的 FFT IP 算好的幅度谱(线性, ADC 码值) */
void ui_spec_set_spectrum(ui_spec_t *s, const u16 *mag, u32 nbins, s32 fs);

/* 顶栏右上角的"设置"齿轮按钮(图标用底层图元画, 不依赖图片资源) */
void ui_spec_draw_set_btn(u32 fb, int pressed);       /* pressed!=0 反色(按下态) */
void ui_spec_flush_set_btn(u32 fb);                   /* 只刷按钮所在的那几行 */
int  ui_spec_setbtn_hit(int x, int y);                /* 1 = 命中按钮 */
void ui_spec_draw_czt_btn(u32 fb, int pressed, u8 mode);   /* ★ Chirp-Z 按钮 */
void ui_spec_flush_czt_btn(u32 fb);
int  ui_spec_cztbtn_hit(int x, int y);
void ui_spec_draw_czt_popup(u32 fb, const ui_spec_t *s);
void ui_spec_draw_czt_curve(u32 fb, ui_spec_t *s);   /* ★ CZT 模式2 的曲线绘制 */
void ui_spec_draw_czt_bin_row(u32 fb, const ui_spec_t *s);  /* ★ 只重画模式1 那一行(点箭头用) */  /* ★ 模式子窗口(帧边界画) */
/* (已删除) ui_spec_czt_popup_refresh: 那套独立底板缓存没有调用者, 已交给 ui_app 窗口层 */
void ui_spec_czt_popup_invalidate(void);   /* ★ 底板作废(模式切换后重新存干净底板) */      /* ★ 每帧调用: 开则画, 关则贴回底板 */
int  ui_spec_cztpop_hit(int x, int y);                     /* ★ 0/1/2=行, -1=不在窗内 */

/* 绘制 */
void ui_spec_draw(u32 fb, ui_spec_t *s);              /* 整屏(不含按钮栏) */
void ui_spec_draw_live(u32 fb, ui_spec_t *s);         /* 频谱区原地刷新(只动变化的列) */
void ui_spec_draw_plot(u32 fb, ui_spec_t *s);         /* 整块重画频谱区 */
void ui_spec_draw_status(u32 fb, const ui_spec_t *s); /* 顶栏 */
void ui_spec_draw_info(u32 fb, const ui_spec_t *s);   /* 信息行 */
void ui_spec_draw_params(u32 fb, const ui_spec_t *s); /* 读数区(峰值/底噪/频率刻度) */
void ui_spec_flush(u32 fb);                           /* 频谱区 + 读数区 D-Cache */
void ui_spec_flush_chrome(u32 fb);                    /* 顶栏 + 信息行 D-Cache */

/* 文字帮手(与界面共用字库, 供按钮栏/其它模块复用) */
const char *ui_spec_ch_name(u8 ch);        /* "CH1" / "CH2" */
void ui_spec_fmt_freq(char *b, s32 hz);    /* "5.00kHz" / "1.024MHz" */
void ui_spec_fmt_db(char *b, s32 db10);    /* "-12.3dB" */

/* 参考电压(设置窗第 1 项): 单位名 / 单位切换 / 显示格式化
   ★ ui_spec_fmt_ref_mv() 用整数分解(mV/1000 与 mV%1000 补零), 不许用浮点 printf:
       V  档 -> "3.300" (1mV 分辨率)      mV 档 -> "3300" (纯整数)
     调用方缓冲区 >= 8 字节即可(最长 "5.500" 5 字符 + '\0')。 */
const char *ui_spec_ref_unit_name(u8 unit);         /* "V" / "mV" / "Vpp" / "mVpp" */
void ui_spec_ref_unit_toggle(ui_spec_t *s);         /* 循环 V->mV->Vpp->mVpp->V(内部 mV 值不变) */
void ui_spec_win_toggle(ui_spec_t *s);              /* ★ 循环 汉宁->汉明->布莱克曼 */
const char *ui_spec_win_name(u8 win);              /* 汉宁 / 汉明 / 布莱克曼 */

/* 采样率档位(设置窗第 2 项): 宿主登记 -> UI 显示/步进 -> 宿主写时钟 */
void   ui_spec_fs_tab_set(ui_spec_t *s, const double *mhz, int n);  /* 升序登记可选档 */
int    ui_spec_fs_count(const ui_spec_t *s);        /* 档数(0 = 没登记) */
double ui_spec_fs_mhz(const ui_spec_t *s);          /* 当前档频率 MHz(没登记 -> 0) */
void   ui_spec_fs_text(const ui_spec_t *s, char *b);/* 当前档显示串 "8.192MHz" / "--" */
int    ui_spec_fs_has_up(const ui_spec_t *s);       /* 还能升档? */
int    ui_spec_fs_has_dn(const ui_spec_t *s);       /* 还能降档? */
void   ui_spec_fs_step(ui_spec_t *s, int dir);      /* dir>0 升一档, <0 降一档; 到头夹住 */
int    ui_spec_fs_idx_of_hz(const ui_spec_t *s, u32 hz);  /* 与 hz 一致的档(没有取最近) */

/* 频率分辨率(谱线间隔) = 采样率 / FFT 点数 = 采样率 / (2 × 单边谱线数)。
   ★ nbins 是**单边**谱线数(16384 点 FFT 给 N/2 = 8192 条), 所以分母必须乘 2 ——
     直接 sample_rate / nbins 会正好大两倍(信息行/设置窗原来就是这么错的)。
   没有谱线数据(nbins <= 0)时返回 0, 调用方显示 "--"。 */
s32 ui_spec_res_hz(const ui_spec_t *s);
s32 ui_spec_czt_to_db10(const ui_spec_t *s, u32 czt_mag);   /* ★ CZT 幅度 -> 0.1dB(与 peak_db 同标定) */

/* ---------------- 显示区间(设置窗第 3/4 项: 频率起始 / 频率终止) ----------------
   区间恒为 [f_start, f_start + span]:
     ★ span 就是原来的"跨度"(宽度), f_start 默认 0 -> 行为与以前完全相同(0 ~ 跨度);
     ★ 改"频率起始"时终止不动、改"频率终止"时起始不动, 区间就变成 起始~终止;
     ★ 两个端点各自的显示单位只有 kHz / MHz 两档。 */
#define UI_SPEC_FU_KHZ  0u      /* 频率单位: kHz */
#define UI_SPEC_FU_MHZ  1u      /* 频率单位: MHz */

s32  ui_spec_f_start(const ui_spec_t *s);            /* 区间起点(Hz) */
s32  ui_spec_f_stop(const ui_spec_t *s);             /* 区间终点 = f_start + span */
void ui_spec_set_f_start(ui_spec_t *s, s32 hz);      /* 终止不动 -> 区间变成 起始~终止 */
void ui_spec_set_f_stop(ui_spec_t *s, s32 hz);       /* 起始不动 */
void ui_spec_range_reset(ui_spec_t *s);              /* 区间复位: 0 ~ 默认跨度 */
const char *ui_spec_freq_unit_name(u8 unit);         /* "kHz" / "MHz" */
void ui_spec_freq_unit_toggle(u8 *unit);             /* kHz <-> MHz */
void ui_spec_fmt_freq_u(char *b, s32 hz, u8 unit);   /* 指定单位: "12.34kHz" / "1.234MHz" */
s32  ui_spec_freq_hz_from_kbd(s32 kbd_value, u8 unit);  /* 键盘定点值 -> Hz */
int  ui_spec_chrome_need(const ui_spec_t *s);        /* 1 = 信息行/频率刻度要补画 */
void ui_spec_sync_chrome(u32 fb, ui_spec_t *s);      /* 补画信息行 + 频率刻度/读数 + flush */
void ui_spec_fmt_ref_mv(char *b, s32 mv, u8 unit);  /* V: "3.300" / mV: "3300" */

/* 参考电压生效(第二步):
   ★ ui_spec_ref_set_mv(): 夹到 [UI_SPEC_REF_MV_MIN, UI_SPEC_REF_MV_MAX] 后写入 ref_mv;
   ★ ui_spec_ref_mv_from_kbd(): 数字键盘回车值(输入数字 x1000)按单位档换算成 mV
       —— V 档直接就是 mV, mV 档要 /1000;
   ★ ui_spec_ref_mag(): 参考电压对应的线性幅度(ADC 码值, 最小 1) ——
       = ref_mv * UI_SPEC_REF_MAG / mv_fs, 与 sp_fmt_volt() 的电压标定同源;
   ★ ui_spec_lin_y(): 线性纵轴的 y = 绘图区底 - (mag / mag_at_ref) * 绘图区高,
       dB 模式不走这条(参考电压只管线性纵轴)。验证台用它断言落点。 */
void ui_spec_ref_set_mv(ui_spec_t *s, s32 mv);
s32  ui_spec_ref_mv_from_kbd(s32 kbd_value, u8 unit);
s32  ui_spec_ref_mag(const ui_spec_t *s);
int  ui_spec_lin_y(const ui_spec_t *s, u32 mag);

#endif /* UI_SPEC_H */

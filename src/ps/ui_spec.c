/******************************************************************************
 * ui_spec.c - 频谱仪界面实现(单通道: CH1 / CH2 切换)
 *
 * 设计要点:
 *   - 一个界面状态 ui_spec_t, 屏幕每列存一个线性幅度 + 一个 0.1dB 值;
 *   - 原地刷新沿用"逐列记录 + 只重画变化的列"(与示波器轨迹同一套办法),
 *     擦旧时用 sp_bg_at() 把该列的网格/边框补回来, 所以不闪、不留残影;
 *   - 数据入口只有一个: ui_spec_set_spectrum() —— 幅度谱由 PL 的 FFT IP 算好后送进来,
 *     本模块只做"谱线 -> 屏幕列"的映射与读数: 不插值, 也不在 PS 里另算一遍谱。
 ******************************************************************************/
#include "ui_spec.h"
#include "ui_screens.h"
#include "lcd_gfx.h"
#include "xil_cache.h"
#include <math.h>
#include <string.h>
#include <stdio.h>       /* ★ 子窗口里用 sprintf 拼 "+-N bin"(补上, 免得隐式声明) */

/* ---------------- 布局: 与示波器/信号发生器一致 ---------------- */
#define SP_WAVE_X   UI_OSC_WAVE_X
#define SP_WAVE_Y   UI_OSC_WAVE_Y
#define SP_WAVE_W   UI_OSC_WAVE_W
#define SP_WAVE_H   297           /* ★ 频谱专用: 比示波器高 1 格(原 264) —— 底部信息栏已删, 网格直接延伸到刻度上方(72+297=369) */
#define SP_DIV_W    UI_OSC_DIV_W
#define SP_DIV_H    UI_OSC_DIV_H
#define SP_CLIP_X0  2
#define SP_CLIP_X1  (UI_OSC_WAVE_W - 3)
#define SP_CLIP_Y0  (UI_OSC_WAVE_Y + 2)
#define SP_CLIP_Y1  (SP_WAVE_Y + SP_WAVE_H - 3)
#define SP_PLOT_H   (SP_CLIP_Y1 - SP_CLIP_Y0)

#define SP_BOTTOM_Y 369           /* ★ 频谱专用: 原 UI_OSC_BOTTOM_Y(336), 信息栏删掉后刻度带下移一格 */
#define SP_BOTTOM_H 71            /* ★ 频谱专用: 只留频率刻度带(原 104 含信息栏 42px) */


/* ---------------- 小工具 ---------------- */
static s32 sp_clamp(s32 v, s32 lo, s32 hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

/* 1-2-5 档: up!=0 取比 v 大的下一档, 否则取比 v 小的上一档 */
static s32 sp_span_125(s32 v, int up)
{
    static const s32 tab[3] = { 1, 2, 5 };
    s32 mul = 1;
    int i;

    if (v < 1) {
        v = 1;
    }
    while (mul <= (v / 10)) {
        mul *= 10;
    }
    if (up != 0) {
        for (i = 0; i < 3; i++) {
            if ((tab[i] * mul) > v) {
                return tab[i] * mul;
            }
        }
        return mul * 10;
    }
    for (i = 2; i >= 0; i--) {
        if ((tab[i] * mul) < v) {
            return tab[i] * mul;
        }
    }
    return (mul > 1) ? (mul / 10) : 1;
}

/* 把显示区间 [f_start, f_start+span] 夹进 [0, 满带], 并保证宽度 >= 最小跨度。
   ★ 区间模型: span 是宽度, f_start 是起点(默认 0 = 老行为 "0~跨度")。
   采样率变小(满带变小)或端点设过头时, 必须走一次这个夹取。 */
static void sp_range_clamp(ui_spec_t *s)
{
    s32 maxs = (s->full_span > 0) ? s->full_span : UI_SPEC_SPAN_DEF;

    if (s->f_start < 0) {
        s->f_start = 0;
    }
    if (s->f_start > (maxs - UI_SPEC_SPAN_MIN)) {
        s->f_start = maxs - UI_SPEC_SPAN_MIN;      /* 起点也要给终点留出最小跨度 */
    }
    if (s->f_start < 0) {
        s->f_start = 0;                            /* 满带比最小跨度还窄的极端情况 */
    }
    if (s->span < UI_SPEC_SPAN_MIN) {
        s->span = UI_SPEC_SPAN_MIN;
    }
    if ((s->f_start + s->span) > maxs) {
        s->span = maxs - s->f_start;
    }
    if (s->span < 1) {
        s->span = 1;
    }
}

/* 线性幅度 -> 0.1dB(0 = ref 满量程) */
static s32 sp_mag_to_db10(u32 mag, s32 ref)
{
    float r;
    s32 v;

    if (mag == 0u) {
        return -1000;                 /* 0 显示成 -100dB */
    }
    if (ref < 1) {
        ref = UI_SPEC_REF_MAG;
    }
    r = 200.0f * log10f((float)mag / (float)ref);   /* 0.1dB 单位 */
    v = (s32)r;
    if (v < -1000) {
        v = -1000;
    }
    if (v > 0) {
        v = 0;                        /* 超过满量程顶格 */
    }
    return v;
}

/* ---------------- 参考电压 -> 线性纵轴 ----------------
   参考电压 ref_mv 是"线性纵轴上端"的电压(Vpp, 0..5500mV), 纵轴下端固定 0V。
   ★ 必须与 sp_fmt_volt() 的电压标定同源: 谱峰幅度 mag 是 ADC 码值(峰值),
     对应的峰峰电压 mVpp = mag * mv_fs / 2048(2048 个码 = 满量程 mVpp),
     反过来"参考电压对应的码值" mag_at_ref = ref_mv * 2048 / mv_fs
     (= ref_mv * UI_SPEC_REF_MAG / mv_fs —— UI_SPEC_REF_MAG 就是 2048)。
   ★ ref_mv = 0 时除数会是 0: 夹到 1(纵轴顶只剩 1 个码, 任何有幅度的谱线都顶格),
     公式本身不受影响。
   ★ 只给线性纵轴用: dB 模式两个分支一个字都没动。 */
s32 ui_spec_ref_mag(const ui_spec_t *s)
{
    s32 mv_fs = (s->mv_fs > 0) ? s->mv_fs : UI_SPEC_MV_FS;
    s32 rv = s->ref_mv;
    s32 m;

    if (rv < UI_SPEC_REF_MV_MIN) {
        rv = UI_SPEC_REF_MV_MIN;
    }
    if (rv > UI_SPEC_REF_MV_MAX) {
        rv = UI_SPEC_REF_MV_MAX;
    }
    m = (s32)(((s64)rv * (s64)UI_SPEC_REF_MAG) / (s64)mv_fs);
    if (m < 1) {
        m = 1;
    }
    return m;
}

/* 线性纵轴: 谱线长度 = 谱线电压(与参考电压同口径)占参考电压的百分比 × 显示区高度
   ★ 单位决定"口径", 两种口径差 2 倍(mag 是谱峰的幅度码, 读数峰峰值 = mag*mv_fs/2048):
       单位 V  / mV  : 参考电压是**峰值**   -> 谱线电压取峰值 A = Vpp/2 -> pct = mag/(2*ref)
       单位 Vpp/mVpp : 参考电压是**峰峰值** -> 谱线电压取峰峰值 Vpp    -> pct = mag/ref
     (ref = 参考电压对应的码值, 见 ui_spec_ref_mag(); 切单位不换算 ref_mv, 只换口径)
   ★ 谱线最高点 = 显示区最低处(0V)往上 pct × 显示区像素数:  y = SP_CLIP_Y1 - pct * SP_PLOT_H
   ★ 例(读数 3.00Vpp 的谱线): 参考 3.3V(峰值口径) -> 1.5/3.3 = 45.5% -> 约半屏;
                              参考 3.3Vpp(峰峰口径) -> 3.0/3.3 = 90.9% -> 接近顶格。
   ★ dB 纵轴完全不受影响。 */
int ui_spec_lin_y(const ui_spec_t *s, u32 mag)
{
    s32 ref = ui_spec_ref_mag(s);
    s32 m = (s32)mag;
    int pp = ((s->ref_unit == UI_SPEC_UNIT_VPP) || (s->ref_unit == UI_SPEC_UNIT_MVPP)) ? 1 : 0;

    if (m < 0) {
        m = 0;
    }
    if (pp != 0) {                      /* Vpp / mVpp: 参考是峰峰值, 谱线也取峰峰值 */
        if (m > ref) {
            m = ref;
        }
        return SP_CLIP_Y1 - (int)(((s64)m * (s64)SP_PLOT_H) / (s64)ref);
    }
    if (m > ref * 2) {                  /* V / mV: 参考是峰值, 谱线取 Vpp/2 */
        m = ref * 2;
    }
    return SP_CLIP_Y1 - (int)(((s64)m * (s64)SP_PLOT_H) / ((s64)ref * 2));
}

/* 该列在屏幕上的 y(dB / 线性两种模式) */
static int sp_col_y(const ui_spec_t *s, int x)
{
    if (x < 0) {
        x = 0;
    }
    if (x >= UI_SPEC_COLS) {
        x = UI_SPEC_COLS - 1;
    }

    if (s->db != 0u) {
        s32 range = UI_SPEC_DB_RANGE * 10;        /* 0.1dB */
        s32 v = s->col_db[x];
        if (v > 0) {
            v = 0;
        }
        if (v < -range) {
            v = -range;
        }
        return SP_CLIP_Y0 + (int)((-v) * SP_PLOT_H / range);   /* 0dB 在顶格 */
    }

    return ui_spec_lin_y(s, (u32)s->col_mag[x]);   /* 线性: 上端 = 参考电压 */
}

/* 频谱区某像素"没有曲线时"的颜色: 左右白边框 / 网格(横线优先) / 中央十字线 */
static u16 sp_bg_at(int x, int y)
{
    int iy = y - SP_WAVE_Y;

    if ((x == SP_WAVE_X) || (x == (SP_WAVE_X + SP_WAVE_W - 1))) {
        return UI_OSC_COLOR_FRAME;
    }
    if ((iy >= 0) && (iy < SP_WAVE_H) && ((iy % SP_DIV_H) == 0)) {
        return (iy == (4 * SP_DIV_H)) ? UI_OSC_COLOR_MID : UI_OSC_COLOR_GRID;
    }
    if (((x % SP_DIV_W) == 0) && (x > SP_WAVE_X) && (x < (SP_WAVE_X + SP_WAVE_W))) {
        return (x == (SP_WAVE_X + 5 * SP_DIV_W)) ? UI_OSC_COLOR_MID : UI_OSC_COLOR_GRID;
    }
    return UI_OSC_COLOR_BG;
}

static u16 sp_trace_color(const ui_spec_t *s)
{
    return (s->ch == 0u) ? UI_OSC_COLOR_CH1 : UI_OSC_COLOR_CH2;
}

/* ---------------- 峰值搜索(标峰) ---------------- */
#define SP_MK_SEP      6        /* ★ 只用来排除"同一个峰的相邻列"(局部极大已保证每峰 1 列); 原 90 列会把靠近的第二/第三个峰整个跳过 */
#define SP_MK_MIN_DB   60       /* ★ 峰只要比平均底噪高 6.0dB 就算(原 200 = 20dB 太严: 第三个弱峰进不了峰值表, peak 标过不去) */

/* 某一列的峰值频率: 在"全带谱线"里找该列范围内最大的那条谱线, **直接报它的频率**。
   ★ 不做抛物线插值: 读数只用 FFT 出来的数据(整数条谱线), 不造谱线之间的值。
   ★ 读数只跟 FFT 谱线有关, 和屏幕跨度无关。
   返回 0 = 没有全带谱线(软件算谱那条路), 调用方退回按屏幕列算 */
static int sp_bin_peak_hz(const ui_spec_t *s, s32 col_x, s32 *hz)
{
    u32 nb, lo, hi, k, ib;
    double bins_show, b, half, f;

    if ((s->full_n <= 4u) || (s->sample_rate <= 0) || (s->full_span <= 0)) {
        return 0;
    }
    if (col_x < 0) {
        col_x = 0;
    }
    if (col_x >= UI_SPEC_COLS) {
        col_x = UI_SPEC_COLS - 1;
    }

    nb = (u32)s->full_n;
    /* 这一列对应到第几条谱线: 显示区间是 [f_start, f_start+span],
       所以列的谱线位置要先加上 f_start 对应的一段 */
    bins_show = ((double)nb * (double)s->span) / (double)s->full_span;
    b = (((double)nb * (double)s->f_start) / (double)s->full_span) +
        (((double)col_x * bins_show) / (double)(UI_SPEC_COLS - 1));

    /* 该列覆盖的谱线范围(再留 2 条余量), 在这个范围里找最大的那条 */
    half = (bins_show / (double)(UI_SPEC_COLS - 1)) + 2.0;
    lo = (b > half) ? (u32)(b - half) : 1u;
    hi = ((b + half) < (double)(nb - 2u)) ? (u32)(b + half) : (nb - 2u);
    if (lo < 1u) {
        lo = 1u;
    }
    if (hi > (nb - 2u)) {
        hi = nb - 2u;
    }
    if (lo > hi) {
        return 0;
    }
    ib = lo;
    for (k = lo; k <= hi; k++) {
        if (s->full_mag[k] > s->full_mag[ib]) {
            ib = k;
        }
    }

    /* ★ 峰值频率 = 峰值那条谱线的频率(整数条谱线), 不做抛物线插值 */
    f = ((double)ib * (double)s->sample_rate) / (2.0 * (double)nb);
    *hz = (s32)(f + 0.5);
    return 1;
}

/* 取某个频率处的幅度: **就近取那一条真实谱线的幅度**, 不做相邻谱线线性插值。
   (M1 标线每帧用它跟踪幅度)  返回 0 = 没有全带谱线 */
static int sp_mag_at_hz(const ui_spec_t *s, s32 hz, u16 *mag)
{
    double b;
    u32 i0;

    if ((s->full_n == 0u) || (s->sample_rate <= 0) || (hz < 0)) {
        return 0;
    }
    b = ((double)hz * (2.0 * (double)s->full_n)) / (double)s->sample_rate;
    if (b < 0.0) {
        b = 0.0;
    }
    if (b > (double)(s->full_n - 1u)) {
        b = (double)(s->full_n - 1u);
    }
    i0 = (u32)(b + 0.5);                    /* 四舍五入到最近的谱线 */
    if (i0 >= (u32)s->full_n) {
        i0 = (u32)s->full_n - 1u;
    }
    *mag = s->full_mag[i0];
    return 1;
}

/* 跨度/通道变了以后, 旧跨度的谱线已经没有意义: 清掉,
   否则整块重画时会把上一组数据按新刻度画出来, 屏幕上看着就是"旧谱线残留" */
static void sp_data_clear(ui_spec_t *s)
{
    int x;

    for (x = 0; x < UI_SPEC_COLS; x++) {
        s->col_mag[x] = 0u;
        s->col_db[x]  = (s16)(-(UI_SPEC_DB_RANGE * 10));
    }
    /* ★ 不要清 nbins! 它是"FFT 配置"(单边谱线数 = 点数的一半), 不是这一帧的数据:
       清了它, 信息行的"分辨"会消失一帧再出现 —— 按跨度/通道/Search 这些会走到
       本函数的按钮时, 屏幕上就是"分辨那里闪一下"(其余项不依赖 nbins, 所以不闪)。
       数据有没有效看 valid/frames, 分辨显示只认 nbins 与采样率。 */
    s->mk_n = 0u;
    s->frames = 0u;
    s->prev_valid = 0u;         /* 频谱区下一帧整块重画 */
    s->chrome_req = 1u;         /* 区间/数据变了: 频率刻度与信息行也要补画一次 */
}

/* Search(类似示波器的"自动设置"): 在全带里找最大的峰值谱线,
   把跨度调到"峰大致落在屏幕中间"的 1-2-5 档, 并把峰值光标(倒三角)放到它上面。
   跨度过小看不见峰、跨度过大峰挤在最左边时, 按一下就能自动找到 */
void ui_spec_peak_search(ui_spec_t *s)
{
    s32 fpk = 0;
    u32 best = 0u;
    u32 bi = 0u;
    u32 i;
    s32 maxs = (s->full_span > 0) ? s->full_span : UI_SPEC_SPAN_DEF;

    /* ★ 按 Search 先关掉**所有**已经打开的峰值数值框(Peak 按钮开的那些):
       清 mrk_n 之后还必须把频谱区标脏(prev_valid = 0), 让下一帧整块重画 ——
       原地刷新的擦除只认"标记表里还有的框", 已经清空的框它擦不到, 会留在屏上;
       顺手把框矩形清零, 免得留下过期矩形在别的地方被当成"要擦的框"。 */
    s->mrk_n = 0u;
    for (i = 0u; i < (u32)UI_SPEC_NMARK; i++) {
        s->mrk_tw[i] = 0;
        s->mrk_th[i] = 0;
    }
    s->prev_valid = 0u;

    if ((s->full_n > 4u) && (s->sample_rate > 0)) {
        u32 sum = 0u;
        u32 avg;

        for (i = 0u; i < (u32)s->full_n; i++) {
            u32 m = s->full_mag[i];
            sum += m;
            if ((i >= 4u) && (m > best)) {          /* 前几个 bin 是直流/泄漏, 跳过 */
                best = m;
                bi = i;
            }
        }
        avg = sum / (u32)s->full_n;
        if ((bi < 1u) || (best < (avg * 8u))) {     /* 没有明显高过底噪的峰: 不动跨度 */
            return;
        }
        /* ★ 不做抛物线插值: 峰频率 = 峰值谱线本身的频率 */
        fpk = (s32)((((double)bi * (double)s->sample_rate) /
                     (2.0 * (double)s->full_n)) + 0.5);
    } else {
        if (s->valid == 0u) {
            return;                                 /* 还没有任何数据 */
        }
        fpk = s->peak_hz;                           /* 没有全谱数据: 用当前显示的峰 */
    }

    if (fpk <= 0) {
        return;
    }
    /* 目标跨度 = 峰频率的 2 倍(峰落在屏幕中间), 取 1-2-5 档并限制在满带内。
       减 1 是为了允许"正好等于 2 倍"的那一档(否则 2x10k 会被顶到 50k) */
    {
        /* 目标宽度 = 峰频率的 2 倍, 让峰大致落在区间中间; 区间起点跟着峰走 */
        s32 w  = sp_clamp(sp_span_125((fpk * 2) - 1, 1), UI_SPEC_SPAN_MIN, maxs);
        s32 st = fpk - (w / 2);

        if (st < 0) {
            st = 0;
        }
        if ((st + w) > maxs) {
            st = maxs - w;
        }
        if (st < 0) {
            st = 0;
        }
        s->f_start = st;
        s->span = w;
        sp_range_clamp(s);
    }
    sp_data_clear(s);                               /* 旧区间的谱线作废, 免得残留 */

    /* ★ 找到了最大峰值谱线: 把光标(倒三角)放上去。
       Search 自己只负责找到位置, 数值显示交给 Peak 按钮 */
    s->cur_hz   = fpk;
    s->cur_init = 1u;
    s->cur_tw   = 0;
    {
        u16 mm;
        if (sp_mag_at_hz(s, fpk, &mm) != 0) {
            s->cur_mag = mm;
            s->cur_db = (s16)sp_mag_to_db10(mm, s->ref_mag);
            /* ★ CZT 模式1: 必须放在这里 —— 上面这段光标跟踪每帧用 full_mag 重算 cur_mag/cur_db,
               覆盖放在它前面会被冲掉(实测: 频率改了、幅值没改)。 */
            if ((s->czt_mode == 1u) && (s->czt_fine_hz > 0)) {
                s->peak_hz = s->czt_fine_hz;
                s->peak_db = s->czt_fine_db;
                s->cur_hz  = s->czt_fine_hz;
                s->cur_mag = s->czt_fine_mag;
                s->cur_db  = (s16)s->czt_fine_db;
                if (s->mk_n > 0u) {
                    s->mk[0].hz   = s->czt_fine_hz;
                    s->mk[0].db10 = (s16)s->czt_fine_db;
                    s->mk[0].mag  = s->czt_fine_mag;
                }
            }
        }
    }
}

/* 找本帧的谱峰列表(局部极大, 彼此至少隔 SP_MK_SEP 列, 高于底噪 SP_MK_MIN_DB):
   给 Next Peak / Last Peak 跳转用, 本身不画任何东西 */
static void sp_find_peaks(ui_spec_t *s)
{
    int k, i, x;

    s->mk_n = 0u;
    for (k = 0; k < UI_SPEC_NPEAK; k++) {
        s32 best = -1;
        int bx = -1;

        for (x = 1; x < (UI_SPEC_COLS - 1); x++) {
            u32 m = (u32)s->col_mag[x];
            int near = 0;

            if ((m < (u32)s->col_mag[x - 1]) || (m < (u32)s->col_mag[x + 1])) {
                continue;                       /* 不是局部极大 */
            }
            if ((s32)s->col_db[x] < (s->avg_db + SP_MK_MIN_DB)) {
                continue;                       /* 离底噪太近, 当成噪声 */
            }
            for (i = 0; i < k; i++) {
                int d = (int)s->mk[i].x - x;
                if (d < 0) {
                    d = -d;
                }
                if (d < SP_MK_SEP) {
                    near = 1;
                    break;
                }
            }
            if (near != 0) {
                continue;
            }
            if ((s32)m > best) {
                best = (s32)m;
                bx = x;
            }
        }
        if (bx < 0) {
            break;
        }
        s->mk[k].x    = (s16)bx;
        s->mk[k].mag  = s->col_mag[bx];
        s->mk[k].db10 = s->col_db[bx];
        if (sp_bin_peak_hz(s, bx, &s->mk[k].hz) == 0) {
            s->mk[k].hz = s->f_start + (s32)(((s64)s->span * (s64)bx) / (s64)(UI_SPEC_COLS - 1));
        }
        s->mk_n = (u8)(k + 1);
    }
}

/* 线性纵轴时的电压格式化(定义在文件后面, 这里先声明) */
static void sp_fmt_volt(char *b, u32 mag, s32 mv_fs);   /* XXU32: mag 由 u16 改 u32 —— czt2_mv 现为 double 且带窗增益(汉宁 wg=2.0), 值可达 ~131000 > 65535, 用 u16 会回绕成错误的小值 */

/* ---------------- 峰值光标(白色边框透明倒三角 + 可选数值框) ----------------
   ★ "标"(倒三角/数值框)贴在哪儿: 用 sp_col_top_near() —— 屏幕上那一列附近实际画到的
     最高处, 而不是按光标幅度 cur_mag 单独换算出的小 y。
     原因: cur_mag 只取"全带谱线里最近的那一条", 而屏幕上那一列画的是 col_mag/col_db
     (取该列覆盖谱线的最大值, 通常比单条谱线高); 线性纵轴的上端=参考电压,
     刻度比 dB 陡好几倍, 会把这个差放大成几十像素 —— 结果就是倒三角飘在半空/或压在峰上,
     而不是"贴在用户看到的那条谱线顶上一点点"。 */
#define SP_TRI_W   11               /* 倒三角宽度(像素) */
#define SP_TRI_H   7                /* 倒三角高度 */
#define SP_TRI_GAP 4                /* 三角尖(底顶点)离谱线顶端的距离 */
#define SP_CUR_H   34               /* 数值框高度(两行 16px 字) */

/* 光标在屏幕上的列(0..COLS-1); 不在当前跨度里返回 -1 */
static int sp_cur_col(const ui_spec_t *s)
{
    if ((s->span <= 0) || (s->cur_hz < s->f_start) ||
        (s->cur_hz > (s->f_start + s->span))) {
        return -1;                                  /* 不在当前显示区间里 */
    }
    return sp_clamp((int)(((((s64)s->cur_hz - (s64)s->f_start) * (s64)(UI_SPEC_COLS - 1))) /
                          (s64)s->span),
                    0, UI_SPEC_COLS - 1);
}

/* 第 col 列附近(±halfw)屏幕上实际画到的最高处(最小 y, 已夹在绘图区内)。
   有逐列记录(prev[])就用记录(它含"与左邻列的连线", 就是屏幕上真正的顶),
   没有就退回该列自己算出来的高度 sp_col_y()。
   ★ 取的是"最高的那个": 三角宽 11 像素, 只要它跨到的列里有更高的, 三角就压上去了。 */
static int sp_col_top_near(const ui_spec_t *s, int col, int halfw)
{
    int k;
    int y = sp_clamp(sp_col_y(s, col), SP_CLIP_Y0, SP_CLIP_Y1);

    for (k = -halfw; k <= halfw; k++) {
        int x = col + k;
        int yk;

        if ((x < 0) || (x >= UI_SPEC_COLS)) {
            continue;
        }
        yk = (s->prev[x].on != 0u) ? (int)s->prev[x].top : sp_col_y(s, x);
        if (yk < y) {
            y = yk;
        }
    }
    return sp_clamp(y, SP_CLIP_Y0, SP_CLIP_Y1);
}

/* 擦掉上一帧的光标(三角 + 数值框), 并把盖住的列标脏以便重画轨迹 */
static void sp_cur_erase(u32 fb, ui_spec_t *s)
{
    int x, y;

    if (s->cur_tw <= 0) {
        return;
    }
    for (y = (int)s->cur_ty; y <= ((int)s->cur_ty + (int)s->cur_th - 1); y++) {
        for (x = (int)s->cur_tx; x <= ((int)s->cur_tx + (int)s->cur_tw - 1); x++) {
            lcd_pixel(fb, x, y, sp_bg_at(x, y));
        }
    }
    /* 这些列原来的轨迹也要单独擦掉并重画, 否则会留下竖直残影 */
    for (x = (int)s->cur_tx; x <= ((int)s->cur_tx + (int)s->cur_tw - 1); x++) {
        if ((x < 0) || (x >= UI_SPEC_COLS)) {
            continue;
        }
        if (s->prev[x].on != 0u) {
            int yy;
            for (yy = s->prev[x].top; yy <= s->prev[x].bot; yy++) {
                lcd_pixel(fb, SP_WAVE_X + x, yy, sp_bg_at(SP_WAVE_X + x, yy));
            }
        }
        s->prev[x].on = 0u;
    }
    s->cur_tw = 0;
}

/* 擦掉上一帧所有数值框(每条谱线一个), 并把盖住的列标脏 */
static void sp_mrk_erase(u32 fb, ui_spec_t *s)
{
    int i, x, y;

    for (i = 0; i < (int)s->mrk_n; i++) {
        int x0, y0, x1, y1;

        if ((int)s->mrk_tw[i] <= 0) {
            continue;
        }
        x0 = (int)s->mrk_tx[i];
        y0 = (int)s->mrk_ty[i];
        x1 = x0 + (int)s->mrk_tw[i] - 1;
        y1 = y0 + (int)s->mrk_th[i] - 1;
        for (y = y0; y <= y1; y++) {
            for (x = x0; x <= x1; x++) {
                lcd_pixel(fb, x, y, sp_bg_at(x, y));
            }
        }
        for (x = x0; x <= x1; x++) {
            if ((x < 0) || (x >= UI_SPEC_COLS)) {
                continue;
            }
            if (s->prev[x].on != 0u) {
                int yy;
                for (yy = s->prev[x].top; yy <= s->prev[x].bot; yy++) {
                    lcd_pixel(fb, SP_WAVE_X + x, yy, sp_bg_at(SP_WAVE_X + x, yy));
                }
            }
            s->prev[x].on = 0u;
        }
        s->mrk_tw[i] = 0;
    }
}

/* 画所有数值框: 每条"已打开显示"的谱线一个, 内容为频率 + 幅度 */
static void sp_mrk_draw(u32 fb, ui_spec_t *s)
{
    int i, y;

    for (i = 0; i < (int)s->mrk_n; i++) {
        s32 hz = s->mrk_hz[i];
        u16 mag = 0u;
        s16 db10 = 0;
        int col, cx, tw, tw2, w, lx, ly;
        char t1[20];
        char t2[20];

        s->mrk_tw[i] = 0;
        if ((s->span <= 0) || (hz < s->f_start) || (hz > (s->f_start + s->span))) {
            continue;                               /* 不在当前显示区间里 */
        }
        col = sp_clamp((int)(((((s64)hz - (s64)s->f_start) * (s64)(UI_SPEC_COLS - 1))) /
                             (s64)s->span),
                       0, UI_SPEC_COLS - 1);
        if (sp_mag_at_hz(s, hz, &mag) != 0) {
            db10 = (s16)sp_mag_to_db10(mag, s->ref_mag);
        } else {
            mag  = s->col_mag[col];
            db10 = s->col_db[col];
        }
        /* ★ CZT 模式1: 数值框(mrk_* 这一套, 不是 mk[])的频率与幅值改用 CZT 的结果。
           幅值来源本来是 sp_mag_at_hz()/col_mag[](频谱仪自己的谱线), 这里直接换成
           czt_fine_mag; 频率同理换成 czt_fine_hz。其余一律不动。 */
        if ((s->czt_mode == 1u) && (s->czt_fine_hz > 0)) {
            hz   = s->czt_fine_hz;
            mag  = s->czt_fine_mag;
            db10 = (s16)s->czt_fine_db;
        }
        ui_spec_fmt_freq(t1, hz);
        if (s->db != 0u) {
            ui_spec_fmt_db(t2, (s32)db10);
        } else {
            sp_fmt_volt(t2, mag, s->mv_fs);
        }
        tw  = (int)ui_text_size(t1, 1);
        tw2 = (int)ui_text_size(t2, 1);
        if (tw2 > tw) {
            tw = tw2;
        }
        w  = tw + 8;
        cx = SP_WAVE_X + col;
        /* 框贴在"屏幕上真正画出来的谱线顶"上方(与倒三角同一个基准, 两者间距固定) */
        y  = sp_col_top_near(s, col, SP_TRI_W / 2);
        ly = y - SP_CUR_H - SP_TRI_H - 6;            /* 框放在三角上方 */
        if (ly < SP_CLIP_Y0) {
            ly = y + 6;                              /* 上面放不下放下面 */
        }
        ly = sp_clamp(ly, SP_CLIP_Y0, SP_CLIP_Y1 - SP_CUR_H + 1);
        lx = sp_clamp(cx - (w / 2), SP_CLIP_X0, SP_CLIP_X1 - w + 1);

        lcd_fill_rect(fb, lx, ly, w, SP_CUR_H, UI_OSC_COLOR_BG);
        lcd_rect(fb, lx, ly, w, SP_CUR_H, sp_trace_color(s));
        (void)ui_draw_text(fb, lx + 4, ly + 1, t1, UI_OSC_COLOR_LABEL, 1);
        (void)ui_draw_text(fb, lx + 4, ly + 18, t2, UI_OSC_COLOR_TEXT, 1);

        s->mrk_tx[i] = (s16)lx;
        s->mrk_ty[i] = (s16)ly;
        s->mrk_tw[i] = (s16)w;
        s->mrk_th[i] = (s16)SP_CUR_H;
    }
}

/* 画光标倒三角: 顶点朝下的白色空心三角(内部透明), 尖指在峰顶上方 SP_TRI_GAP 像素处 */
static void sp_cur_draw(u32 fb, ui_spec_t *s)
{
    int col = sp_cur_col(s);
    int cx, ytop, k, x0, y0, x1, y1;

    s->cur_tw = 0;
    if (col < 0) {
        return;
    }
    cx = SP_WAVE_X + col;
    /* 基准 = 屏幕上这一列(以及三角宽度内)实际画到的最高处 —— 见 sp_col_top_near() 的说明。
       ★ 峰顶顶到绘图区上沿(线性纵轴上端=参考电压, 信号比它大就顶格)时, 上方没有空间,
         三角只能贴着上沿画, 会压住顶格的那一小段谱线; 想让三角完整在峰上方就把参考电压调大。 */
    ytop = sp_col_top_near(s, col, SP_TRI_W / 2) - SP_TRI_H - SP_TRI_GAP;
    ytop = sp_clamp(ytop, SP_CLIP_Y0, SP_CLIP_Y1 - SP_TRI_H);

    /* 倒三角: 上边一条横线 + 两条斜边收到底部顶点, 内部不填充 => 透明 */
    for (k = -(SP_TRI_W / 2); k <= (SP_TRI_W / 2); k++) {
        lcd_pixel(fb, cx + k, ytop, UI_OSC_COLOR_TEXT);
    }
    for (k = 1; k <= SP_TRI_H; k++) {
        int dx = ((SP_TRI_W / 2) * (SP_TRI_H - k)) / SP_TRI_H;
        lcd_pixel(fb, cx - dx, ytop + k, UI_OSC_COLOR_TEXT);
        lcd_pixel(fb, cx + dx, ytop + k, UI_OSC_COLOR_TEXT);
    }
    x0 = cx - (SP_TRI_W / 2);
    y0 = ytop;
    x1 = cx + (SP_TRI_W / 2);
    y1 = ytop + SP_TRI_H;

    s->cur_tx = (s16)x0;
    s->cur_ty = (s16)y0;
    s->cur_tw = (s16)(x1 - x0 + 1);
    s->cur_th = (s16)(y1 - y0 + 1);
}



/* ---------------- 每帧收尾: 求峰值/底噪 + 谱峰列表 + 光标跟踪 ---------------- */
static void sp_finish(ui_spec_t *s, const u16 *raw)
{
    int x;
    u32 peak = 0u;
    s32 peak_x = 0;
    s32 sum = 0;

    for (x = 0; x < UI_SPEC_COLS; x++) {
        u32 m = raw[x];

        s->col_mag[x] = (u16)m;
        s->col_db[x] = (s16)sp_mag_to_db10(m, s->ref_mag);

        if (m > peak) {
            peak = m;
            peak_x = x;
        }
        sum += s->col_db[x];
    }

    s->peak_x = peak_x;
    {
        s32 hz = 0;
        if (sp_bin_peak_hz(s, peak_x, &hz) == 0) {
            /* 没有全带谱线: 退回按屏幕列算(别忘了区间起点 f_start) */
            hz = s->f_start + (s32)(((s64)s->span * (s64)peak_x) / (s64)(UI_SPEC_COLS - 1));
        }
        s->peak_hz = hz;
    }
    s->peak_db = s->col_db[peak_x];

        /* ★ CZT 模式1: peak 框的频率/幅值两个变量改用 CZT 的结果;
           其余(谱线/坐标轴/mag[])一律不动。放在这里是因为这两个变量每帧
           会在本函数里被重算, 必须在其后覆盖才不会被冲掉。 */
        if ((s->czt_mode == 1u) && (s->czt_fine_hz > 0)) {
            s->peak_hz = s->czt_fine_hz;
            s->peak_db = s->czt_fine_db;
            /* 屏幕上的"peak 读数"不止底部面板: 还有光标读数与谱峰数值框,
               这两处也必须换成 CZT 的值, 否则看起来"根本没改"。 */
            s->cur_hz  = s->czt_fine_hz;
            s->cur_db  = (s16)s->czt_fine_db;

            if (s->mk_n > 0u) {
                s->mk[0].hz   = s->czt_fine_hz;
                s->mk[0].db10 = (s16)s->czt_fine_db;
        s->mk[0].mag  = s->czt_fine_mag;   /* ★ 数值框的幅值(它用的是 mag, 不是 db10) */
        s->cur_mag    = s->czt_fine_mag;   /* ★ 光标读数同理 */
            }
        }
    s->avg_db = sum / UI_SPEC_COLS;
    sp_find_peaks(s);               /* 找本帧的谱峰列表(Next/Last Peak 用) */

    /* ★ 谱峰数值框必须在 sp_find_peaks() 之后覆盖: 它会用 col_mag/col_db 重填 mk[],
       放在前面会被它冲掉(实测就是这样"看起来没改")。 */
    if ((s->czt_mode == 1u) && (s->czt_fine_hz > 0) && (s->mk_n > 0u)) {
        s->mk[0].hz   = s->czt_fine_hz;
        s->mk[0].db10 = (s16)s->czt_fine_db;
    }

    /* 峰值光标: 第一次有数据就放到最大峰上; 之后停在原频率, 每帧跟踪该处的幅度 */
    if (s->cur_init == 0u) {
        s->cur_hz   = s->peak_hz;
        s->cur_init = 1u;
    }
    {
        u16 mm;
        if (sp_mag_at_hz(s, s->cur_hz, &mm) != 0) {
            s->cur_mag = mm;
            s->cur_db = (s16)sp_mag_to_db10(mm, s->ref_mag);
            /* ★★ CZT 模式1 读数覆盖必须放在这里(光标跟踪之后):
               上面那段每帧用 full_mag 重算 cur_mag/cur_db, 放前面会被冲掉。 */
            if ((s->czt_mode == 1u) && (s->czt_fine_hz > 0)) {
                s->peak_hz = s->czt_fine_hz;
                s->peak_db = s->czt_fine_db;
                s->cur_hz  = s->czt_fine_hz;
                s->cur_mag = s->czt_fine_mag;
                s->cur_db  = (s16)s->czt_fine_db;
                if (s->mk_n > 0u) {
                    s->mk[0].hz   = s->czt_fine_hz;
                    s->mk[0].db10 = (s16)s->czt_fine_db;
                    s->mk[0].mag  = s->czt_fine_mag;
                }
            }
        } else {
            /* 没有全带谱线(软件算谱): 用显示谱在光标那一列的幅度 */
            int cc = sp_cur_col(s);
            if ((cc >= 0) && (cc < UI_SPEC_COLS)) {
                s->cur_mag = s->col_mag[cc];
                s->cur_db  = s->col_db[cc];
            }
        }
    }
    s->valid = 1u;
    s->frames++;
}

/* ---------------- 数据入口 1: 应用侧给幅度谱(线性, ADC 码值) ---------------- */
void ui_spec_set_spectrum(ui_spec_t *s, const u16 *mag, u32 nbins, s32 fs)
{
    static u16 raw[UI_SPEC_COLS];
    int x;

    if (fs > 0) {
        s->sample_rate = fs;
        s->full_span = fs / 2;
    }
    /* 采样率/满带变了: 显示区间 [f_start, f_start+span] 重新夹进 [0, 满带] */
    {
        s32 stop0 = s->f_start + s->span;

        sp_range_clamp(s);
        if ((s->f_start + s->span) != stop0) {
            s->chrome_req = 1u;         /* 区间被夹短了: 频率刻度/信息行也要跟着更新 */
        }
    }
    /* ★ 分辨率(nbins)变了 -> 信息栏里的"分辨率"必须重画。
       信息栏只在 chrome_req != 0 时由 ui_spec_sync_chrome() 重画, 而 nbins 变化
       (CZT 开: 4096 / 关: 8192)以前不会置这个标志 -> 屏幕上一直显示旧的 500Hz。
       注意必须"比较后再赋值", 否则每帧都会请求重画信息栏。 */
    if (s->nbins != (s32)nbins) {
        s->info_req = 1u;          /* ★ 只刷信息栏(用 chrome_req 会连频谱区一起重画 -> 闪) */
    }
    s->nbins = (s32)nbins;

    /* 峰值搜索要用到"全带"谱线(不只是当前跨度里显示的那些), 这里留一份 */
    if ((mag != 0) && (nbins > 0u)) {
        u32 k;
        u32 lim = (nbins > (u32)UI_SPEC_FULL_BINS) ? (u32)UI_SPEC_FULL_BINS : nbins;
        for (k = 0u; k < lim; k++) {
            s->full_mag[k] = mag[k];
        }
        s->full_n = (u16)lim;
    } else {
        s->full_n = 0u;
    }

    if ((mag == 0) || (nbins == 0u) || (s->full_span <= 0)) {
        for (x = 0; x < UI_SPEC_COLS; x++) {
            raw[x] = 0u;
        }
    } else {
        /* 只显示 0..span 这一段: 该段谱线数 = nbins * span / full_span */
        double bins_show = ((double)nbins * (double)s->span) / (double)s->full_span;
        double bins_lo = ((double)nbins * (double)s->f_start) / (double)s->full_span;

        if (bins_show > (double)nbins) {
            bins_show = (double)nbins;
        }
        /* ★ 只用 FFT 出来的谱线值, 不做任何插值:
           本列覆盖到的谱线取最大值(可能 1 条, 也可能多条) —— 不管放大到什么
           程度, 每一列画出来的高度都是某一条真实谱线的值。
           (原来"一列不到一条谱线就线性插值"会造出谱线之间根本不存在的值) */
        for (x = 0; x < UI_SPEC_COLS; x++) {
            double b0 = bins_lo + (((double)x * bins_show) / (double)(UI_SPEC_COLS - 1));
            double b1 = bins_lo + (((double)(x + 1) * bins_show) / (double)(UI_SPEC_COLS - 1));
            double v;

            if (b0 > (double)nbins) {
                b0 = (double)nbins;
            }
            if (b1 > (double)nbins) {
                b1 = (double)nbins;
            }

            {
                u32 k0 = (u32)b0;
                u32 k1 = (u32)b1;                   /* 末列会顶到 nbins, 下面夹住 */
                u32 k;

                if (k1 >= nbins) {
                    k1 = nbins - 1u;
                }
                if (k0 > k1) {
                    k0 = k1;
                }
                v = (double)mag[k0];
                for (k = k0 + 1u; k <= k1; k++) {
                    if ((double)mag[k] > v) {
                        v = (double)mag[k];
                    }
                }
            }
            if (v < 0.0) {
                v = 0.0;
            }
            if (v > 65535.0) {
                v = 65535.0;
            }
            raw[x] = (u16)v;
        }
    }
    sp_finish(s, raw);
}

/* ---------------- 默认值 / 按钮动作 ---------------- */
void ui_spec_default(ui_spec_t *s)
{
    /* ★ 采样率档位表是宿主登记的(clk_wiz1.h 的小数档), 不属于"默认值":
       memset 之前先留一份, 之后原样放回去, 免得每次切界面/复位都把表擦掉。 */
    u8  fs_n   = s->fs_n;
    u8  fs_idx = s->fs_idx;
    double fs_tab[UI_SPEC_FS_N];
    int  fi;

    for (fi = 0; fi < (int)UI_SPEC_FS_N; fi++) {
        fs_tab[fi] = s->fs_mhz[fi];
    }
    (void)memset(s, 0, sizeof(*s));
    for (fi = 0; fi < (int)UI_SPEC_FS_N; fi++) {
        s->fs_mhz[fi] = fs_tab[fi];
    }
    s->fs_n   = fs_n;
    s->fs_idx = (fs_idx < fs_n) ? fs_idx : 0u;
    s->fs_req = 0u;

    s->sample_rate = 8192000;
    s->full_span = s->sample_rate / 2;
    s->span = UI_SPEC_SPAN_DEF;
    s->ref_mag = UI_SPEC_REF_MAG;
    s->ch = 0u;
    s->run = 1u;
    s->db = 0u;   /* ★ 默认线性纵轴: 谱线长度 = 该谱线峰值电压 / 参考电压, dB 模式需手动切 */
    s->mv_fs = UI_SPEC_MV_FS;
    s->ref_mv = UI_SPEC_REF_MV_DEF;     /* 参考电压默认 3300mV = 3.300V */
    s->ref_unit = UI_SPEC_UNIT_V;       /* 默认按 V 显示 -> "3.300" */
    s->f_start      = 0;                /* 显示区间起点默认 0 -> 0 ~ 跨度(与以前完全一样) */
    s->f_start_unit = UI_SPEC_FU_KHZ;   /* 起始/终止频率默认按 kHz 显示 */
    s->f_stop_unit  = UI_SPEC_FU_KHZ;
    s->chrome_req   = 1u;
    /* ★ Chirp-Z 模式2 的默认点数: 与模式1 一样用 201(不设就是 0 -> 会被夹成 2, 曲线没法看) */
    s->czt_m        = 201u;
}

void ui_spec_ch_toggle(ui_spec_t *s)
{
    s->ch = (u8)((s->ch == 0u) ? 1u : 0u);
    s->cur_init = 0u;           /* 换通道: 光标下一帧重新放到最大峰 */
    s->mrk_n    = 0u;           /* 换通道: 另一路的数值框全部清掉 */
    sp_data_clear(s);           /* 换通道: 另一路的谱线作废, 免得残留 */
}

void ui_spec_run_stop(ui_spec_t *s)
{
    s->run = (u8)((s->run != 0u) ? 0u : 1u);
    s->chrome_req = 1u;      /* ★ 顶栏的"运行/停止"要重画: 这三块原来由触摸路径画, 改成 chrome_req 后必须自己置位 */
}

void ui_spec_db_toggle(ui_spec_t *s)
{
    s->db = (u8)((s->db != 0u) ? 0u : 1u);
    s->prev_valid = 0u;
    s->chrome_req = 1u;      /* ★ 信息行的"纵轴 dB/线性"要重画(同上) */
}

void ui_spec_span_step(ui_spec_t *s, int dir)
{
    s32 maxs = (s->full_span > 0) ? s->full_span : UI_SPEC_SPAN_DEF;
    s32 v;

    if (s->span <= 0) {
        s->span = UI_SPEC_SPAN_DEF;
    }
    /* ★ "跨度+/-"只改宽度 -> 也就是只动频率终止(区间起点不动) */
    v = sp_span_125(s->span, (dir > 0) ? 1 : 0);
    if (v > (maxs - s->f_start)) {
        v = maxs - s->f_start;
    }
    s->span = sp_clamp(v, UI_SPEC_SPAN_MIN, maxs);
    sp_range_clamp(s);
    sp_data_clear(s);           /* 换跨度: 旧区间的谱线作废, 免得残留 */
}

void ui_spec_span_reset(ui_spec_t *s)
{
    ui_spec_range_reset(s);     /* 区间复位: 起点回 0, 宽度回默认跨度 */
    s->cur_init = 0u;           /* 跨度复位: 光标重新放到最大峰 */
    sp_data_clear(s);           /* 跨度复位: 旧区间的谱线作废, 免得残留 */
}

/* 在标记表里找某个频率(容差 = 一列宽度, 足够区分相邻谱线) */
static int sp_mrk_find(const ui_spec_t *s, s32 hz)
{
    int i;
    s32 tol = (s->span > 0) ? (s->span / (UI_SPEC_COLS - 1)) : 1;

    if (tol < 1) {
        tol = 1;
    }
    for (i = 0; i < (int)s->mrk_n; i++) {
        s32 d = s->mrk_hz[i] - hz;
        if (d < 0) {
            d = -d;
        }
        if (d <= tol) {
            return i;
        }
    }
    return -1;
}

/* 光标那条谱线的数值框是否已经打开(给按钮栏显示状态用) */
int ui_spec_cur_marked(const ui_spec_t *s)
{
    return (sp_mrk_find(s, s->cur_hz) >= 0) ? 1 : 0;
}

/* Peak 按钮: 只切换"光标所在那一条谱线"的数值框, 别的谱线不受影响 */
void ui_spec_peak_val_toggle(ui_spec_t *s)
{
    int i = sp_mrk_find(s, s->cur_hz);

    if (i >= 0) {                               /* 已打开 -> 关掉它 */
        int k;
        for (k = i; (k + 1) < (int)s->mrk_n; k++) {
            s->mrk_hz[k] = s->mrk_hz[k + 1];
        }
        s->mrk_n = (u8)(s->mrk_n - 1u);
    } else if (s->mrk_n < (u8)UI_SPEC_NMARK) {  /* 未打开 -> 打开它 */
        s->mrk_hz[s->mrk_n] = s->cur_hz;
        s->mrk_n = (u8)(s->mrk_n + 1u);
    } else {
        return;                                 /* 已经开满了 */
    }
    s->prev_valid = 0u;         /* 整块重画一次, 免得旧框擦不干净 */
}

/* Next Peak / Last Peak: 光标跳到上/下一个谱峰(dir>0 往高频方向) */
void ui_spec_peak_next(ui_spec_t *s, int dir)
{
    int i;
    int idx = -1;
    s32 best = 0;

    for (i = 0; i < (int)s->mk_n; i++) {
        s32 hz = s->mk[i].hz;
        if (dir > 0) {
            if ((hz > s->cur_hz) && ((idx < 0) || (hz < best))) {
                best = hz;
                idx = i;
            }
        } else {
            if ((hz < s->cur_hz) && ((idx < 0) || (hz > best))) {
                best = hz;
                idx = i;
            }
        }
    }
    if (idx < 0) {
        return;                 /* 这个方向没有别的峰了 */
    }
    s->cur_hz   = s->mk[idx].hz;
    s->cur_mag  = s->mk[idx].mag;
    s->cur_db   = s->mk[idx].db10;
    s->cur_init = 1u;
}

/* ---------------- 文字帮手 ---------------- */
const char *ui_spec_ch_name(u8 ch)
{
    return (ch == 0u) ? "CH1" : "CH2";
}

static void sp_fmt(char *b, u32 ipart, u32 frac, u32 fdigits, const char *unit)
{
    char t[32];
    int n = 0;
    u32 d = 1;
    u32 k;

    while (((ipart / d) >= 10u) && (d < 100000000u)) {
        d *= 10u;
    }
    while (d > 0u) {
        t[n++] = (char)('0' + ((ipart / d) % 10u));
        d /= 10u;
    }
    if (fdigits > 0u) {
        t[n++] = '.';
        d = 1;
        for (k = 1; k < fdigits; k++) {
            d *= 10u;
        }
        while (d > 0u) {
            t[n++] = (char)('0' + ((frac / d) % 10u));
            d /= 10u;
        }
    }
    t[n] = '\0';
    (void)strcpy(b, t);
    (void)strcat(b, unit);
}

/* 线性纵轴: 把谱峰幅度换算成电压(峰峰值)显示。
   谱峰幅度 mag 是 ADC 码值(峰值), 对应正弦的峰峰值 = 2*mag 个码;
   mv_fs 是满量程 mVpp(对应 4096 个码), 所以 Vpp = 2*mag*mv_fs/4096 = mag*mv_fs/2048 */
static void sp_fmt_volt(char *b, u32 mag, s32 mv_fs)
{
    u32 mv;

    if (mv_fs < 1) {
        mv_fs = UI_SPEC_MV_FS;
    }
    mv = ((u32)mag * (u32)mv_fs) / 2048u;       /* mVpp */
    if (mv >= 1000u) {
        sp_fmt(b, mv / 1000u, (mv % 1000u) / 10u, 2u, "Vpp");
    } else if (mv >= 10u) {
        sp_fmt(b, mv, 0u, 0u, "mVpp");
    } else {
        sp_fmt(b, mv / 10u, mv % 10u, 1u, "mVpp");
    }
}

void ui_spec_fmt_freq(char *b, s32 hz)
{
    u32 v;

    if (hz < 0) {
        hz = 0;
    }
    v = (u32)hz;
    if (v < 1000u) {
        sp_fmt(b, v, 0u, 0u, "Hz");
    } else if (v < 1000000u) {
        sp_fmt(b, v / 1000u, v % 1000u, 3u, "kHz");        /* ★ 2->3 位小数: 分辨率 10Hz -> 1Hz(CZT 读数要多一位) */
    } else {
        sp_fmt(b, v / 1000000u, v % 1000000u, 6u, "MHz");   /* XXMHZ6: MHz 由 3 位小数(1kHz)改为 6 位小数 = 1Hz 分辨率 —— CZT 模式2 的读数用这个函数 */
    }
}

void ui_spec_fmt_db(char *b, s32 db10)
{
    char t[16];
    int n = 0;
    u32 a;

    if (db10 < 0) {
        t[n++] = '-';
        a = (u32)(-db10);
    } else {
        t[n++] = '+';
        a = (u32)db10;
    }
    t[n++] = (char)('0' + ((a / 100u) % 10u));
    t[n++] = (char)('0' + ((a / 10u) % 10u));
    t[n++] = '.';
    t[n++] = (char)('0' + (a % 10u));
    t[n++] = 'd';
    t[n++] = 'B';
    t[n] = '\0';
    (void)strcpy(b, t);
}

/* ---------------- 参考电压: 显示格式化 + 单位切换 ----------------
   ★ 需求: 单位 V/Vpp -> "3.300"(整数部分 + '.' + 3 位小数, 即 1mV 分辨率);
           单位 mV/mVpp -> "3300"(整数)。内部恒为整数 mV。
   ★ ★ 不许用浮点 printf ★ ★: 直接按 mV/1000(整数部分) 与 mV%1000(小数部分补 3 位)
      拆字符串, 所以 3300->"3.300"、5500->"5.500"、5->"0.005"、0->"0.000";
      mV/mVpp 档就是十进制整数(5500->"5500"、0->"0")。整个模块不引入浮点库/printf。 */

/* 无符号十进制(不用 printf): 写入 b, 末尾补 '\0', 返回字符数 */
static int sp_dec(char *b, u32 v)
{
    char t[12];
    int n = 0;
    int i;

    if (v == 0u) {
        b[0] = '0';
        b[1] = '\0';
        return 1;
    }
    while ((v > 0u) && (n < (int)sizeof(t))) {
        t[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    for (i = 0; i < n; i++) {
        b[i] = t[n - 1 - i];
    }
    b[n] = '\0';
    return n;
}

const char *ui_spec_ref_unit_name(u8 unit)
{
    switch (unit) {
    case UI_SPEC_UNIT_MV:   return "mV";
    case UI_SPEC_UNIT_VPP:  return "Vpp";
    case UI_SPEC_UNIT_MVPP: return "mVpp";
    default:                return "V";     /* UI_SPEC_UNIT_V */
    }
}

void ui_spec_ref_unit_toggle(ui_spec_t *s)
{
    s->ref_unit = (u8)((s->ref_unit + 1u) % UI_SPEC_UNIT_N);   /* 下一档 */
    /* ★ 只翻单位档: ref_mv 一个字都不动(内部恒为整数 mV), 单位只影响显示/输入解释 */
    s->czt2_relayout = 1u;    /* ★ 单位档(V/Vpp)改变 -> 模式2 纵轴口径变 -> 必须重铺重画 */
}

/* ★ FFT 窗: 只改状态(0=汉宁 1=汉明 2=布莱克曼), 真正的乘窗在 helloworld.c 里
   调用 fft_windows.c 的 hanning_windows()/hamming_windows()/blackman_windows()。 */
void ui_spec_win_toggle(ui_spec_t *s)
{
    s->win = (u8)((s->win + 1u) % 4u);      /* 汉宁 -> 汉明 -> 布莱克曼 -> 矩形 -> 汉宁 */
}

const char *ui_spec_win_name(u8 win)
{
    switch (win) {
    case 1u:  return "汉明";
    case 2u:  return "布莱克曼";
    case 3u:  return "矩形";           /* ★ 矩形窗 = 不加窗 */
    default:  return "汉宁";
    }
}

void ui_spec_fmt_ref_mv(char *b, s32 mv, u8 unit)
{
    s32 v = mv;
    int i;

    /* 不允许负值; 上限也夹住, 免得越界值被画到屏上 */
    if (v < UI_SPEC_REF_MV_MIN) {
        v = UI_SPEC_REF_MV_MIN;
    }
    if (v > UI_SPEC_REF_MV_MAX) {
        v = UI_SPEC_REF_MV_MAX;
    }
    if ((unit == UI_SPEC_UNIT_MV) || (unit == UI_SPEC_UNIT_MVPP)) {
        (void)sp_dec(b, (u32)v);                    /* mV / mVpp: 纯十进制整数 */
        return;
    }
    /* V / Vpp: 整数部分 + '.' + 3 位小数(mV%1000 逐位取, 不用浮点) */
    i = sp_dec(b, (u32)v / 1000u);                  /* b[i] 处是 '\0', 下面覆盖掉 */
    b[i++] = '.';
    b[i++] = (char)('0' + (((u32)v % 1000u) / 100u));
    b[i++] = (char)('0' + ((((u32)v % 1000u) / 10u) % 10u));
    b[i++] = (char)('0' + ((u32)v % 10u));
    b[i] = '\0';
}

/* 参考电压写入: 一律夹到 [UI_SPEC_REF_MV_MIN, UI_SPEC_REF_MV_MAX](不允许负值/超上限)。
   值一变, 线性纵轴的顶端跟着变(见 ui_spec_ref_mag), 调用方负责重画受影响的区域。 */
void ui_spec_ref_set_mv(ui_spec_t *s, s32 mv)
{
    if (mv < UI_SPEC_REF_MV_MIN) {
        mv = UI_SPEC_REF_MV_MIN;
    }
    if (mv > UI_SPEC_REF_MV_MAX) {
        mv = UI_SPEC_REF_MV_MAX;
    }
    s->ref_mv = mv;
    s->czt2_relayout = 1u;    /* ★ 参考电压改变 -> 模式2 纵轴刻度变 -> 必须重铺重画(原来被误放进上面的 if 块里, 正常改值根本不置它) */
}

/* 数字键盘回车值 -> mV:
   键盘的定点值 = 输入数字 x 1000, 所以 V/Vpp 档(值 = 输入数字 x1000)直接就是 mV,
   mV/mVpp 档要再除回 1000。 */
s32 ui_spec_ref_mv_from_kbd(s32 kbd_value, u8 unit)
{
    if ((unit == UI_SPEC_UNIT_MV) || (unit == UI_SPEC_UNIT_MVPP)) {
        return kbd_value / 1000;
    }
    return kbd_value;
}

/* ---------------- 采样率档位(设置窗第 2 项) ----------------
   档位表由宿主(helloworld.c)从 clk_wiz1.h 的表里挑"小数频率"登记进来;
   UI 侧只做三件事: 显示当前档 / 上下箭头换档 / 置 fs_req 让宿主去写 MMCM。 */
void ui_spec_fs_tab_set(ui_spec_t *s, const double *mhz, int n)
{
    int i;

    if (n < 0) {
        n = 0;
    }
    if (n > (int)UI_SPEC_FS_N) {
        n = (int)UI_SPEC_FS_N;
    }
    for (i = 0; i < n; i++) {
        s->fs_mhz[i] = ((mhz != 0) ? mhz[i] : 0.0);
    }
    for (i = n; i < (int)UI_SPEC_FS_N; i++) {
        s->fs_mhz[i] = 0.0;
    }
    s->fs_n = (u8)n;
    if ((int)s->fs_idx >= n) {
        s->fs_idx = 0u;
    }
    s->fs_req = 0u;
}

int ui_spec_fs_count(const ui_spec_t *s)
{
    return (int)s->fs_n;
}

double ui_spec_fs_mhz(const ui_spec_t *s)
{
    if (s->fs_n == 0u) {
        return 0.0;
    }
    return s->fs_mhz[(s->fs_idx < s->fs_n) ? s->fs_idx : 0u];
}

void ui_spec_fs_text(const ui_spec_t *s, char *b)
{
    double m = ui_spec_fs_mhz(s);

    if (m <= 0.0) {
        b[0] = '-';
        b[1] = '-';
        b[2] = '\0';
        return;
    }
    ui_spec_fmt_freq(b, (s32)((m * 1000000.0) + 0.5));    /* "8.192MHz" */
}

int ui_spec_fs_has_up(const ui_spec_t *s)
{
    return (((int)s->fs_idx + 1) < (int)s->fs_n) ? 1 : 0;
}

int ui_spec_fs_has_dn(const ui_spec_t *s)
{
    return (s->fs_idx > 0u) ? 1 : 0;
}

void ui_spec_fs_step(ui_spec_t *s, int dir)
{
    u8 old = s->fs_idx;

    if (s->fs_n == 0u) {
        return;                                 /* 宿主还没登记: 按不动 */
    }
    if (dir > 0) {
        if (((int)s->fs_idx + 1) < (int)s->fs_n) {
            s->fs_idx++;
        }
    } else if (dir < 0) {
        if (s->fs_idx > 0u) {
            s->fs_idx--;
        }
    }
    if (s->fs_idx != old) {
        s->fs_req = 1u;                         /* 换档了 -> 让宿主主循环去写时钟 */
    }
}

int ui_spec_fs_idx_of_hz(const ui_spec_t *s, u32 hz)
{
    int best = 0;
    double bd = 1.0e30;
    int i;

    for (i = 0; i < (int)s->fs_n; i++) {
        double d = (s->fs_mhz[i] * 1000000.0) - (double)hz;
        if (d < 0.0) {
            d = -d;
        }
        if (d < 0.5) {
            return i;                           /* 完全一致 */
        }
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}


/* 频率分辨率(谱线间隔): 采样率 / FFT 点数。nbins 是单边谱线数(FFT 点数的一半),
   所以 bin 间隔 = fs / (2 * nbins) —— 见 ui_spec.h 的说明。 */
s32 ui_spec_res_hz(const ui_spec_t *s)
{
    if ((s->nbins <= 0) || (s->sample_rate <= 0)) {
        return 0;
    }
    return s->sample_rate / (2 * s->nbins);
}


static void sp_paint_col(u32 fb, const ui_spec_t *s, int x, int t, int b)
{
    lcd_vline(fb, SP_WAVE_X + x, t, b, sp_trace_color(s));
}

void ui_spec_draw_plot(u32 fb, ui_spec_t *s)
{
    int x, prev_y = 0, have = 0;

    /* ★ Chirp-Z 模式2: 曲线区改画 CZT 曲线(横轴 = czt2_f0..f0+df*(n-1), 纵轴 = Vpp)。
       模式1 与 CZT 关闭时 czt2_n = 0 -> 走下面的原频谱绘制, 一行都不变。 */
    if ((s->czt_mode == 2u) && (s->czt2_n > 1u)) {
        ui_spec_draw_czt_curve(fb, s);
        s->czt2_dirty  = 0u;
        s->prev_valid  = 1u;
        return;
    }

    lcd_fill_rect(fb, SP_WAVE_X, SP_WAVE_Y, SP_WAVE_W, SP_WAVE_H, UI_OSC_COLOR_BG);
    ui_wave_grid(fb);
    /* ★ ui_wave_grid() 画的是示波器那块(高 UI_OSC_WAVE_H=264); 频谱的绘图区比它高一格,
       这里把多出来的那一格(SP_WAVE_Y+UI_OSC_WAVE_H .. SP_WAVE_Y+SP_WAVE_H)按同一套网格补上,
       颜色规则完全复用 sp_bg_at(), 所以和上面那部分无缝衔接。 */
    {
        int gy, gx;

        /* ★ 按行填色 + 只在网格列补像素:
           原来是 800x33 = 2.64 万次单像素写, 单帧写屏时间过长会跨过 LCD 帧边界 -> 撕裂/乱闪;
           现在每行一次 lcd_hline(33 次) + 竖直网格线交点(SP_DIV_W=80 => 实际 10 列 x 33 行 = 330 次 lcd_pixel)。 */
        for (gy = (UI_OSC_WAVE_Y + UI_OSC_WAVE_H - 1); gy < (SP_WAVE_Y + SP_WAVE_H); gy++) {   /* ★ 从 335 起: ui_draw_grid 在 335 画了示波器的白色底边, 频谱变高后它落在绘图区中间 -> 用正确的网格色盖掉 */
            lcd_hline(fb, SP_WAVE_X, SP_WAVE_X + SP_WAVE_W - 1, gy,
                      (((gy - SP_WAVE_Y) % SP_DIV_H) == 0) ? UI_OSC_COLOR_GRID : UI_OSC_COLOR_BG);
            for (gx = SP_WAVE_X; gx < (SP_WAVE_X + SP_WAVE_W); gx++) {
                if ((gx % SP_DIV_W) == 0) { lcd_pixel(fb, gx, gy, sp_bg_at(gx, gy)); }
            }
            /* ★ 右白框那一列(x=799)不是 SP_DIV_W 的倍数, 上面补不到 -> 单独补回, 否则 335..368 这段右边框会断 */
            lcd_pixel(fb, SP_WAVE_X + SP_WAVE_W - 1, gy, UI_OSC_COLOR_FRAME);
        }
    }
    s->cur_tw   = 0;            /* 整块重画已经把旧光标盖掉了 */

    for (x = 0; x < UI_SPEC_COLS; x++) {
        int y = sp_col_y(s, x);              /* 峰值 -> y(线性: 上端 = 参考电压, 下端 = 0V) */
        int t = y;
        int b = y;

        if (have != 0) {                     /* 细刺: 本列峰值与上一列之间连一段竖线 */
            if (prev_y < t) {
                t = prev_y;
            }
            if (prev_y > b) {
                b = prev_y;
            }
        }
        t = sp_clamp(t, SP_CLIP_Y0, SP_CLIP_Y1);
        b = sp_clamp(b, SP_CLIP_Y0, SP_CLIP_Y1);
        sp_paint_col(fb, s, x, t, b);
        prev_y = y;
        have = 1;

        s->prev[x].top = (s16)t;
        s->prev[x].bot = (s16)b;
        s->prev[x].on = 1u;
    }
    s->prev_valid = 1u;
    sp_mrk_draw(fb, s);         /* 每条谱线各自的数值框 */
    sp_cur_draw(fb, s);         /* 峰值光标: 倒三角 */
}

void ui_spec_draw_live(u32 fb, ui_spec_t *s)
{
    int x, prev_y = 0, have = 0;

    /* ★ 模式2: 只在数据变化(czt2_dirty)或刚标脏(prev_valid==0)时整块重画一次,
       然后按整块刷(一帧一次, 不会像窄带刷新那样撕裂); 平时一个像素都不写。 */
    if ((s->czt_mode == 2u) && (s->czt2_n > 1u)) {
        if ((s->czt2_dirty != 0u) || (s->prev_valid == 0u) || (s->czt2_relayout != 0u)) {   /* ★ 补 relayout: 改参考电压/单位档时它被置 1, 但原来进不了这个分支 -> 曲线留在旧刻度 */
            ui_spec_draw_plot(fb, s);
        }
        /* ★ 这里【不再】flush: 主循环在 ui_app_draw_live() 之后一定会调 ui_app_flush_live() -> ui_spec_flush(),
           原来两处刷同一段(883KB), 每帧白刷 1.77MB。保留帧边界那一处即可(也是本工程的约定)。 */
        return;
    }
    if ((s->valid == 0u) || (s->prev_valid == 0u)) {
        ui_spec_draw_plot(fb, s);
        /* ★ 子窗口必须"最后画": 频谱整块重画会把它盖掉。
           而且必须主动请求重画: 键盘按 ESC/点外面收起时(kbd.ok=0)没有 commit,
           也就没人置 czt_pop_draw -> 以前这里不画 -> 子窗口跟着消失。 */
        return;
    }

    sp_mrk_erase(fb, s);        /* 先擦掉上一帧所有数值框 */
    sp_cur_erase(fb, s);        /* 再擦掉光标倒三角 */

    for (x = 0; x < UI_SPEC_COLS; x++) {
        int y = sp_col_y(s, x);              /* 峰值 -> y(线性: 上端 = 参考电压, 下端 = 0V) */
        int t = y;
        int b = y;

        if (have != 0) {                     /* 细刺: 本列峰值与上一列之间连一段竖线 */
            if (prev_y < t) {
                t = prev_y;
            }
            if (prev_y > b) {
                b = prev_y;
            }
        }
        t = sp_clamp(t, SP_CLIP_Y0, SP_CLIP_Y1);
        b = sp_clamp(b, SP_CLIP_Y0, SP_CLIP_Y1);

        if ((s->prev[x].on != 0u) && (s->prev[x].top == (s16)t) &&
            (s->prev[x].bot == (s16)b)) {
            /* 这一列和上一帧一样: 不写屏 */
        } else {
            if (s->prev[x].on != 0u) {
                int yy;
                for (yy = s->prev[x].top; yy <= s->prev[x].bot; yy++) {
                    lcd_pixel(fb, SP_WAVE_X + x, yy, sp_bg_at(SP_WAVE_X + x, yy));
                }
            }
            sp_paint_col(fb, s, x, t, b);
            s->prev[x].top = (s16)t;
            s->prev[x].bot = (s16)b;
            s->prev[x].on = 1u;
        }
        /* ★ 必须与 ui_spec_draw_plot() 一模一样: "本列峰值 y"每列都要记住,
           不能只在真写屏的那一列记 —— 否则上一列是被跳过的列时, 这一列的
           "与左邻列连线"会拿一个过期的高度, 谱线底部/顶部就差 1 像素。 */
        prev_y = y;
        have = 1;
    }
    sp_mrk_draw(fb, s);         /* 再画这一帧的数值框 */
    sp_cur_draw(fb, s);         /* 最后画光标倒三角 */
    /* ★ 子窗口放最最后: 上面的增量刷新也会碰到它的区域(两条路都要放最后) */
    /* ★ 子窗口开着时每帧都要重画它: 增量刷新不认识子窗口, 会把谱线像素写进它的区域,
       而 czt_pop_draw 是一次性请求 -> 不重画就露出波形(实测: 波形盖在子窗口上)。 */

}

/* ---------------- 顶栏右上角"设置"齿轮按钮 ----------------
   图标是一个 1bpp 掩码(见下面 ui_spec_gear_mask): 细描边(1 像素线宽)、7 个圆弧齿、
   中心是圆环孔(不是实心圆再挖洞)。齿轮本体 28x28, 居中放进 40x32 的热区。
   掩码里为 0 的像素一个字节都不写 -> 按钮底色和按钮边框原样保留,
   所以齿间和中心孔能透出底下的颜色(空心效果)。
   配色与按钮栏一致: 平时"深灰底 + 白图标", 按下时反色"白底 + 黑图标"。
   ★ 这个函数只画按钮自己那 40x32 一块(不动任何别的像素), 所以在触摸处理里调用也
     不会被液晶扫描追上 —— 需求要求"按钮刷新只动它自己那一小块"。 */

/* 齿轮掩码: 32x32, 每行 4 字节, 每字节 MSB 在最左边(bit = 1 -> 画前景色)。
   生成方式(_hwsim 里的离屏小程序, 逐像素核对过):
     齿轮实体 = 齿盘(半径 10.0 的圆) ∪ 7 个齿(半径 3.0 的圆, 圆心距 11.0),
     两圆取平滑并集 -> 齿是圆的、齿根是圆角(圆弧齿, 没有方块棱角);
     再取这个实体的 1px 轮廓(8 邻域内边界) = 空心描边齿轮;
     中心另叠一个半径 5.0 的圆环孔(同样是 1px 轮廓)。
   结果: 前景 172/1024 像素(16.8%, 是细线不是实心盘), 外径 28px, 中心 (16,16) 为空。 */
static const u8 ui_spec_gear_mask[UI_SPEC_GEAR_W * ((UI_SPEC_GEAR_W + 7) / 8)] = {
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x03, 0xC0, 0x00,
    0x00, 0x06, 0x60, 0x00,
    0x00, 0x04, 0x20, 0x00,
    0x00, 0x04, 0x20, 0x00,
    0x03, 0x84, 0x21, 0xC0,
    0x06, 0xDC, 0x3B, 0x60,
    0x0C, 0x70, 0x0E, 0x30,
    0x0C, 0x00, 0x00, 0x30,
    0x06, 0x00, 0x00, 0x60,
    0x03, 0x03, 0xC0, 0xC0,
    0x01, 0x0E, 0x70, 0x80,
    0x03, 0x08, 0x10, 0xC0,
    0x02, 0x18, 0x18, 0x40,
    0x06, 0x10, 0x08, 0x60,
    0x1C, 0x10, 0x08, 0x38,
    0x30, 0x18, 0x18, 0x0C,
    0x20, 0x08, 0x10, 0x04,
    0x30, 0x0E, 0x70, 0x0C,
    0x1F, 0x83, 0xC1, 0xF8,
    0x00, 0x80, 0x01, 0x00,
    0x00, 0xC0, 0x03, 0x00,
    0x00, 0x40, 0x02, 0x00,
    0x00, 0x40, 0x02, 0x00,
    0x00, 0xC7, 0xE3, 0x00,
    0x00, 0xC4, 0x23, 0x00,
    0x00, 0x6C, 0x36, 0x00,
    0x00, 0x38, 0x1C, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00
};

void ui_spec_draw_set_btn(u32 fb, int pressed)
{
    u16 bg = (pressed != 0) ? UI_OSC_COLOR_TEXT : UI_OSC_COLOR_TABBG;
    u16 fg = (pressed != 0) ? UI_OSC_COLOR_BG   : UI_OSC_COLOR_TEXT;
    int gx = UI_SPEC_SETBTN_X + ((UI_SPEC_SETBTN_W - UI_SPEC_GEAR_W) / 2);
    int gy = UI_SPEC_SETBTN_Y + ((UI_SPEC_SETBTN_H - UI_SPEC_GEAR_H) / 2);

    lcd_fill_rect(fb, UI_SPEC_SETBTN_X, UI_SPEC_SETBTN_Y,
                  UI_SPEC_SETBTN_W, UI_SPEC_SETBTN_H, bg);
    lcd_rect(fb, UI_SPEC_SETBTN_X, UI_SPEC_SETBTN_Y,
             UI_SPEC_SETBTN_W, UI_SPEC_SETBTN_H, UI_OSC_COLOR_FRAME);
    /* 只写掩码里为 1 的像素: 底色/边框保留, 齿轮是空心的细描边 */
    lcd_blit_mask(fb, gx, gy, UI_SPEC_GEAR_W, UI_SPEC_GEAR_H, ui_spec_gear_mask, fg);
}

void ui_spec_flush_set_btn(u32 fb)
{
    /* 按整行刷(与 ui_spec_flush_chrome / ui_touch_flush_bar 同一套做法):
       D-Cache 维护以 cache line 为单位, 只刷按钮那么窄的一条既不满足对齐,
       也会漏掉同一行里其它被改过的像素。这里只刷按钮所在的 32 行。 */
    Xil_DCacheFlushRange((UINTPTR)(fb + ((u32)UI_SPEC_SETBTN_Y * LCD_GFX_WIDTH * 3u)),
                         (u32)UI_SPEC_SETBTN_H * LCD_GFX_WIDTH * 3u);
}

int ui_spec_setbtn_hit(int x, int y)
{
    return ((x >= UI_SPEC_SETBTN_X) && (x < (UI_SPEC_SETBTN_X + UI_SPEC_SETBTN_W)) &&
            (y >= UI_SPEC_SETBTN_Y) && (y < (UI_SPEC_SETBTN_Y + UI_SPEC_SETBTN_H))) ? 1 : 0;
}

/* ---------------- 绘制: 顶栏 / 信息行 / 读数区 ---------------- */
/* ---------------- ★ Chirp-Z 按钮(设置齿轮左侧) ----------------
   与齿轮同一套做法: 只重画按钮外框 + 文字; 刷新只刷按钮所在那几行。
   为什么不会闪: 不整屏重画, 也不碰频谱区; 启用时只多填一次色块。
   颜色沿用频谱仪既有配色(BG/FRAME/TEXT/LABEL), 不新增颜色。 */
/* ---------------- ★ Chirp-Z 模式子窗口 ----------------
   防闪三原则(与设置窗同一套做法):
     1) 只在帧边界画/收: 触摸处理只置 czt_pop_draw 标志(见 ui_touch.c), 这里每帧消费一次;
     2) 开窗前先把要盖住的像素存进静态底板, 关窗时原样贴回(不留残影、不整屏重画);
     3) 单次刷新量 = 292 x 104 x 3 ≈ 91KB ≈ 1ms, 远小于一帧 16.7ms -> 不会撕裂。 */

/* 画子窗口本体(只写屏, 不刷新; 刷新统一由 refresh 做) */
void ui_spec_draw_czt_popup(u32 fb, const ui_spec_t *s)
{
    static const char *row[3] = { "关闭", "模式1 峰值精测", "模式2 自定义频段" };
    int i;

    lcd_fill_rect(fb, UI_CZT_POP_X, UI_CZT_POP_Y, UI_CZT_POP_W, UI_CZT_POP_H, UI_OSC_COLOR_INFO);
    lcd_rect(fb, UI_CZT_POP_X, UI_CZT_POP_Y, UI_CZT_POP_W, UI_CZT_POP_H, UI_OSC_COLOR_FRAME);
    (void)ui_draw_text(fb, UI_CZT_POP_X + 8, UI_CZT_POP_Y + 4, "Chrip-Z", UI_OSC_COLOR_LABEL, 1);
    for (i = 0; i < 3; i++) {
        int ry = UI_CZT_POP_Y + 22 + (i * UI_CZT_POP_RH);
        u8  on = (u8)((s->czt_mode == (u8)i) ? 1u : 0u);

        /* ★ 括号 + 居中圆点: 选中=实心圆, 未选=空心圆。
           "(" 与 ")" 之间留 12px, 圆心取这段空隙的正中, 竖直方向取文字(16px)中线。 */
        {
            int x0 = UI_CZT_POP_X + 8;
            int wl = (int)ui_text_size("(", 1);
            int cx = x0 + wl + 6;                     /* 空隙正中 */
            int cy = ry + 6 + 8;                      /* 文字竖直中线 */

            (void)ui_draw_text(fb, x0, ry + 6, "(", UI_OSC_COLOR_LABEL, 1);
            (void)ui_draw_text(fb, x0 + wl + 12, ry + 6, ")", UI_OSC_COLOR_LABEL, 1);
            if (on != 0u) {
                lcd_fill_circle(fb, cx, cy, 3, UI_OSC_COLOR_CH1);   /* ★ 实心圆点 */
            } else {
                lcd_circle(fb, cx, cy, 3, UI_OSC_COLOR_DIM);        /* 空心 */
            }
        }
        (void)ui_draw_text(fb, UI_CZT_POP_X + 44, ry + 6, row[i], UI_OSC_COLOR_TEXT, 1);
        if (i == 1) {                       /* ★ 模式1 的频段宽度: 左右箭头可调 */
            char b[24];
            int  x0 = UI_CZT_POP_BIN_X;
            int  ay = ry + 6;

            (void)ui_draw_text(fb, x0, ay, "<", UI_OSC_COLOR_TEXT, 1);                      /* 左箭头 */
            (void)sprintf(b, "%d bin", (int)s->czt_peak_bins);
            (void)ui_draw_text(fb, x0 + UI_CZT_POP_VAL_DX, ay, b, UI_OSC_COLOR_LABEL, 1);
            (void)ui_draw_text(fb, x0 + UI_CZT_POP_ARW2_DX, ay, ">", UI_OSC_COLOR_TEXT, 1); /* 右箭头 */
        }
    }
    /* ★ 只有选到模式2 才出现的三个输入项(起始频率 / 终止频率 / 点数 M)。
       这三项是 CZT 自己的参数, 与频谱仪显示的"频率起始/终止"完全无关。
       点这一行会弹数字键盘(触摸处理见 ui_touch.c)。 */
    if (s->czt_mode == 2u) {
        static const char *nm[3] = { "起始频率", "终止频率", "点数 M" };
        int y0 = UI_CZT_POP_Y + 22 + (3 * UI_CZT_POP_RH) + UI_CZT_POP_GAP;

        lcd_hline(fb, UI_CZT_POP_X + 8, UI_CZT_POP_X + UI_CZT_POP_W - 9,
                  y0 - 4, UI_OSC_COLOR_FRAME);
        for (i = 0; i < 3; i++) {
            int ry = y0 + (i * UI_CZT_POP_RH);
            char b[32];

            if (i == 0)      { ui_spec_fmt_freq(b, s->czt_f_start); }   /* XXCZTFMT: 原来写死 "%d.%03d kHz"(1MHz 会显示成 1000.000kHz); 改用与全工程一致的 kHz/MHz 自适应 */
            else if (i == 1) { ui_spec_fmt_freq(b, s->czt_f_end); }     /* XXCZTFMT: 同上 */
            else             { (void)sprintf(b, "%d",     (int)s->czt_m); }

            (void)ui_draw_text(fb, UI_CZT_POP_X + 44, ry + 6, nm[i], UI_OSC_COLOR_LABEL, 1);
            lcd_rect(fb, UI_CZT_POP_X + 150, ry + 2, 240, 22, UI_OSC_COLOR_FRAME);  /* 数值框 */
            (void)ui_draw_text(fb, UI_CZT_POP_X + 158, ry + 6, b, UI_OSC_COLOR_TEXT, 1);
        }
    }
}

/* 每帧调用(由 ui_spec_draw_live 开头调用):
   开 -> 只在"刚打开"时存一次底板, 之后按请求只重画子窗口本体;
   关 -> 贴回底板(底板是"还没有子窗口"的画面, 所以关得干净、不留残影)。 */

int ui_spec_cztpop_hit(int x, int y)
{
    int i;

    if ((x < UI_CZT_POP_X) || (x >= (UI_CZT_POP_X + UI_CZT_POP_W))) { return -1; }
    if ((y < UI_CZT_POP_Y) || (y >= (UI_CZT_POP_Y + UI_CZT_POP_H))) { return -1; }
    i = (y - (UI_CZT_POP_Y + 22)) / UI_CZT_POP_RH;
    if ((i >= 0) && (i <= 2) && (y >= (UI_CZT_POP_Y + 22))) { return i; }
    /* mode2 input rows -> 3/4/5 (caller must also check czt_mode == 2) */
    {
        int y0 = UI_CZT_POP_Y + 22 + (3 * UI_CZT_POP_RH) + UI_CZT_POP_GAP;
        if ((y >= y0) && (y < (y0 + 3 * UI_CZT_POP_RH))) {
            int j = (y - y0) / UI_CZT_POP_RH;
            if ((j >= 0) && (j <= 2)) { return 3 + j; }
        }
    }
    return -1;
}

void ui_spec_draw_czt_btn(u32 fb, int pressed, u8 mode)
{
    u16 bg = (pressed != 0) ? UI_OSC_COLOR_LABEL : UI_OSC_COLOR_BG;
    u16 fg = (pressed != 0) ? UI_OSC_COLOR_BG    : UI_OSC_COLOR_TEXT;
    const char *lab = (mode == 1u) ? "Chrip-Z 1" : ((mode == 2u) ? "Chrip-Z 2" : "Chrip-Z");
    int tw = (int)ui_text_size(lab, 1);

    lcd_fill_rect(fb, UI_CZT_BTN_X, UI_CZT_BTN_Y, UI_CZT_BTN_W, UI_CZT_BTN_H, bg);
    lcd_rect(fb, UI_CZT_BTN_X, UI_CZT_BTN_Y, UI_CZT_BTN_W, UI_CZT_BTN_H, UI_OSC_COLOR_FRAME);
    (void)ui_draw_text(fb, UI_CZT_BTN_X + ((UI_CZT_BTN_W - tw) / 2), UI_CZT_BTN_Y + 8,
                       lab, fg, 1);
}

void ui_spec_flush_czt_btn(u32 fb)
{
    /* 只刷按钮覆盖的那几行(与齿轮按钮同一手法) */
    Xil_DCacheFlushRange((UINTPTR)(fb + (u32)UI_CZT_BTN_Y * LCD_GFX_WIDTH * 3u),
                         (u32)UI_CZT_BTN_H * LCD_GFX_WIDTH * 3u);
}

int ui_spec_cztbtn_hit(int x, int y)
{
    return ((x >= UI_CZT_BTN_X) && (x < (UI_CZT_BTN_X + UI_CZT_BTN_W)) &&
            (y >= UI_CZT_BTN_Y) && (y < (UI_CZT_BTN_Y + UI_CZT_BTN_H))) ? 1 : 0;
}

void ui_spec_draw_status(u32 fb, const ui_spec_t *s)
{
    u16 bg, fg;
    int x = 12;

    lcd_fill_rect(fb, 0, 0, LCD_GFX_WIDTH, UI_OSC_HEADER_H, UI_OSC_COLOR_BAR);
    (void)ui_draw_text(fb, x, 8, "频谱仪", UI_OSC_COLOR_TEXT, 2);
    x += (int)ui_text_size("频谱仪", 2) + 28;
    (void)ui_draw_text(fb, x, 12, ui_spec_ch_name(s->ch), sp_trace_color(s), 2);

    bg = (s->run != 0u) ? UI_OSC_COLOR_RUN_BG : UI_OSC_COLOR_STOP_BG;
    fg = (s->run != 0u) ? UI_OSC_COLOR_RUN_FG : UI_OSC_COLOR_STOP_FG;
    lcd_fill_rect(fb, LCD_GFX_WIDTH - 108, 10, 96, 28, bg);
    {
        const char *t = (s->run != 0u) ? "运行" : "停止";
        int tw = (int)ui_text_size(t, 1);
        (void)ui_draw_text(fb, LCD_GFX_WIDTH - 108 + ((96 - tw) / 2), 16, t, fg, 1);
    }

    /* 右上角"设置"齿轮按钮: 顶栏由本函数整体重画, 所以整屏重画/按钮动作后的
       顶栏刷新都会把它一起画回来(它在频谱绘图区之外, 不受原地刷新影响)。 */
    ui_spec_draw_set_btn(fb, 0);
    ui_spec_draw_czt_btn(fb, 0, s->czt_mode);   /* ★ Chirp-Z 按钮(齿轮左侧) */
}

void ui_spec_draw_info(u32 fb, const ui_spec_t *s)
{
    char t[24];
    char line[192];               /* ★ 160->192: 加一位小数后最坏 159 字符+\0=160, 原来正好越界 1 字节 */
    /* ★ 分辨 = 采样率/(2*单边谱线数): 原来写成 fs/nbins, 显示出来正好大一倍 */
    s32 res = ui_spec_res_hz(s);

    lcd_fill_rect(fb, 0, UI_OSC_INFO_Y, LCD_GFX_WIDTH, UI_OSC_INFO_H, UI_OSC_COLOR_INFO);
    (void)strcpy(line, "跨度 ");
    ui_spec_fmt_freq(t, ((s->czt_mode == 2u) && (s->czt2_n > 1u)) ? s->czt2_span : s->span);   /* XXINFO: 模式2 显示 CZT 频段(原来显示普通频谱跨度, 是错的) */
    (void)strcat(line, t);
    (void)strcat(line, "   纵轴 ");
    (void)strcat(line, (s->db != 0u) ? "10dB/格" : "线性");
    (void)strcat(line, "   采样 ");
    ui_spec_fmt_freq(t, s->sample_rate);
    (void)strcat(line, t);
    if ((s->czt_mode == 2u) && (s->czt2_n > 1u)) {
        /* XXINFO: 模式2 的分辨率 = 曲线栅格 czt2_df(存的是 mHz) */
        (void)strcat(line, "   分辨 ");
        if (s->czt2_df >= 1000) { ui_spec_fmt_freq(t, (s->czt2_df + 500) / 1000); }   /* 四舍五入到 Hz */
        else { (void)sprintf(t, "%dmHz", (int)s->czt2_df); }
        (void)strcat(line, t);
    } else if (res > 0) {
        (void)strcat(line, "   分辨 ");
        ui_spec_fmt_freq(t, res);
        (void)strcat(line, t);
    }
    /* ★ Chirp-Z 模式2: 曲线峰值频率(直接在本函数里扫一下 czt2_mv[], 不用额外状态) */
    if ((s->czt_mode == 2u) && (s->czt2_n > 1u)) {
        /* ★ 直接用 helloworld.c 在全精度 czt2_abs[] 上求出的峰频。
           原来这里在取整后的 czt2_mv[] 上再扫一次 argmax: 主瓣顶部相邻 bin 的幅值差
           只有零点几, 取整后谁大谁小由舍入决定 -> 峰频会在相邻十几个 bin(上百 Hz)之间乱跳。
           显示格式本来就是 2 位小数(10Hz), 所以问题出在"峰的位置"而不是格式。 */
        (void)strcat(line, "   Z2峰 ");
        ui_spec_fmt_freq(t, s->czt2_peak_hz);
        (void)strcat(line, t);
    }
    /* ★ Chirp-Z 模式1 的精测频率: CZT 开且本帧算出来才显示(czt_fine_hz=0 表示无效) */
    if ((s->czt_fine_hz > 0) && (s->czt_mode != 2u)) {   /* XXINFO: Z精测 是模式1 的量, 模式2 下不显示 */
        (void)strcat(line, "   Z精测 ");
        ui_spec_fmt_freq(t, s->czt_fine_hz);
        (void)strcat(line, t);
    }

    /* ★ 当前 FFT 窗(在频谱仪"设置"子窗口第 5 行可切换: 汉宁/汉明/布莱克曼) */
    (void)strcat(line, "   ");
    (void)strcat(line, ui_spec_win_name(((s->czt_mode == 2u) && (s->czt2_n > 1u)) ? 3u : s->win));   /* XXINFO: 模式2 的 CZT 输入不加窗 -> 显示"矩形" */
    (void)ui_draw_text(fb, 8, UI_OSC_INFO_Y + 4, line, UI_OSC_COLOR_LABEL, 1);
}

void ui_spec_draw_params(u32 fb, const ui_spec_t *s)
{
    char t[24];
    int k;

    lcd_fill_rect(fb, 0, SP_BOTTOM_Y, LCD_GFX_WIDTH, SP_BOTTOM_H, UI_OSC_COLOR_PANEL);   /* ★ 只覆盖新的刻度带(信息栏已删) */
    lcd_hline(fb, 0, LCD_GFX_WIDTH - 1, SP_BOTTOM_Y, UI_OSC_COLOR_FRAME);


    /* 频率刻度: 起始 / 1/4 / 1/2 / 3/4 / 终止(显示区间是 [f_start, f_start+span])
       ★ 5 个刻度用同一个单位(按终止频率选 kHz/MHz): 否则跨 1MHz 的区间会出现
         "950.00kHz" 和 "1.100MHz" 混排, 看着乱 */
    {
        /* ★ Chirp-Z 模式2: 底部刻度的起始/终止频率跟着子窗口的 czt_f_start/czt_f_end 走。
           只改这里的【局部变量】, 绝不动频谱仪自己的 f_start/span(界面通用设置)。 */
        s32 f0 = ((s->czt_mode == 2u) && (s->czt2_n > 1u)) ? s->czt2_f0 : s->f_start;
        s32 fw = ((s->czt_mode == 2u) && (s->czt2_n > 1u)) ? ((s->czt2_span > 0) ? s->czt2_span : 1) : s->span;   /* ★ 用未取整的带宽 */
        u8 lu = ((f0 + fw) >= 1000000) ? UI_SPEC_FU_MHZ : UI_SPEC_FU_KHZ;

        for (k = 0; k <= 4; k++) {
            s32 f = f0 + (s32)(((s64)fw * (s64)k) / 4);
            int px = (k * (LCD_GFX_WIDTH - 1)) / 4;
            int tw;

            ui_spec_fmt_freq_u(t, f, lu);
            tw = (int)ui_text_size(t, 1);
            px -= tw / 2;
            px = sp_clamp(px, 2, (LCD_GFX_WIDTH - 2) - tw);
            (void)ui_draw_text(fb, px, SP_BOTTOM_Y + 30, t, UI_OSC_COLOR_LABEL, 1);
        }
    }
}

/* ---------------- 显示区间(设置窗第 3/4 项) + 频率刻度补画 ---------------- */
s32 ui_spec_f_start(const ui_spec_t *s)
{
    return s->f_start;
}

s32 ui_spec_f_stop(const ui_spec_t *s)
{
    return s->f_start + s->span;
}

/* 改"频率起始": 终止(终点)保持不动 -> 区间变成 起始~终止, 宽度跟着变 */
void ui_spec_set_f_start(ui_spec_t *s, s32 hz)
{
    s32 stop = s->f_start + s->span;

    if (hz < 0) {
        hz = 0;
    }
    if (hz > (stop - UI_SPEC_SPAN_MIN)) {
        hz = stop - UI_SPEC_SPAN_MIN;               /* 至少要留出最小跨度 */
    }
    if (hz < 0) {
        hz = 0;
    }
    s->f_start = hz;
    s->span    = stop - hz;
    sp_range_clamp(s);
    sp_data_clear(s);               /* 区间变了: 旧区间的谱线作废 + 置"刻度要补画" */
}

/* 改"频率终止": 起始保持不动 */
void ui_spec_set_f_stop(ui_spec_t *s, s32 hz)
{
    s32 maxs = (s->full_span > 0) ? s->full_span : UI_SPEC_SPAN_DEF;

    if (hz > maxs) {
        hz = maxs;
    }
    if (hz < (s->f_start + UI_SPEC_SPAN_MIN)) {
        hz = s->f_start + UI_SPEC_SPAN_MIN;
    }
    if (hz > maxs) {
        hz = maxs;
    }
    s->span = hz - s->f_start;
    sp_range_clamp(s);
    sp_data_clear(s);
}

void ui_spec_range_reset(ui_spec_t *s)
{
    s32 maxs = (s->full_span > 0) ? s->full_span : UI_SPEC_SPAN_DEF;

    s->f_start = 0;
    s->span = sp_clamp(UI_SPEC_SPAN_DEF, UI_SPEC_SPAN_MIN, maxs);
    sp_range_clamp(s);
    s->chrome_req = 1u;
}

const char *ui_spec_freq_unit_name(u8 unit)
{
    return (unit == UI_SPEC_FU_MHZ) ? "MHz" : "kHz";
}

void ui_spec_freq_unit_toggle(u8 *unit)
{
    if (unit != 0) {
        *unit = (u8)((*unit == UI_SPEC_FU_MHZ) ? UI_SPEC_FU_KHZ : UI_SPEC_FU_MHZ);
    }
}

/* 指定单位的频率显示: kHz -> "12.34kHz"(2 位小数 = 10Hz), MHz -> "1.234MHz"(3 位 = 1kHz) */
void ui_spec_fmt_freq_u(char *b, s32 hz, u8 unit)
{
    if (hz < 0) {
        hz = 0;
    }
    if (unit == UI_SPEC_FU_MHZ) {
        sp_fmt(b, (u32)(hz / 1000000), (u32)(hz % 1000000), 6u, "MHz");   /* XXMHZ6: 6 位小数 = 1Hz(同 fmt_freq) */
    } else {
        sp_fmt(b, (u32)(hz / 1000), (u32)(hz % 1000), 3u, "kHz");                 /* ★ 2->3 位小数 */
    }
}

/* 数字键盘回车值 -> Hz: 键盘定点值 = 输入数字 x 1000,
   所以 kHz 档(value = kHz x 1000)直接就是 Hz, MHz 档还要再乘 1000 */
s32 ui_spec_freq_hz_from_kbd(s32 kbd_value, u8 unit)
{
    if (unit == UI_SPEC_FU_MHZ) {
        return kbd_value * 1000;
    }
    return kbd_value;
}

int ui_spec_chrome_need(const ui_spec_t *s)
{
    return (s->chrome_req != 0u) ? 1 : 0;
}

/* 频率刻度(读数区)与信息行都跟显示区间有关, 但它们不在"频谱区原地刷新"范围里:
   区间一变就置 chrome_req, 由调用方在"没有弹窗"的帧里补画一次。
   ★ 弹窗(设置窗/键盘)开着时别调: 会被弹窗或底板贴回盖掉。 */
void ui_spec_sync_chrome(u32 fb, ui_spec_t *s)
{
    /* ★ 轻量请求: 只重画信息栏那一行(分辨率变了之类), 不碰频率刻度/边框/频谱区。
       以前借用 chrome_req -> 连频谱区一起 flush, 看起来就是闪一下。 */
    if (s->info_req != 0u) {
        s->info_req = 0u;
        ui_spec_draw_info(fb, s);
        ui_spec_flush_chrome(fb);       /* 只刷信息栏那几行 */
    }
    if (s->chrome_req == 0u) {
        return;
    }
    s->chrome_req = 0u;
    ui_spec_draw_status(fb, s);     /* ★ 顶栏也要重画: 触摸路径原来自己画顶栏, 改成置 chrome_req 后这里必须补上(纵轴 dB/线性 等会被按钮改掉) */
    ui_spec_draw_info(fb, s);
    ui_spec_draw_params(fb, s);
    ui_spec_flush(fb);              /* 读数区在频谱区下面, 同一个 flush 覆盖 */
    ui_spec_flush_chrome(fb);       /* 信息行 */
}

/* ---------------- 整屏 / 刷新 ---------------- */
void ui_spec_draw(u32 fb, ui_spec_t *s)
{
    ui_spec_draw_status(fb, s);
    ui_spec_draw_info(fb, s);
    ui_spec_draw_plot(fb, s);
    ui_spec_draw_params(fb, s);
    /* XXS1: 子窗口必须【最后画】: draw_plot 的 lcd_fill_rect(72..368) 会把 y120..359 的子窗口整块擦掉,
       而 czt_popup 仍为 1 -> 窗口"开了看不见"且吃掉触摸。只在 czt_popup 置位时执行, 平时零开销。 */
    if (s->czt_popup != 0u) {
        ui_spec_draw_czt_popup(fb, s);
        /* 不在这里 flush: ui_win_flush() 是 ui_app.c 的 static, 这里调不到; 而本函数只被整屏重画路径调用,
           其后 ui_app_draw() 的调用者会 lcd_flush() 整屏刷, 子窗口自然被写回内存。 */
    }
    /* ★★ 子窗口必须是【最后一个写屏的】——所以放在 draw_params() 之后。
       之前放在它前面: 子窗口 y=120..360 压到底部读数面板上, 而面板在子窗口之后画,
       把子窗口下半部盖掉 -> 切模式后关闭子窗口, 贴回底板时和面板内容打架 -> 底部残留(实测)。
       与 ui_spec_draw_live() 的整块重画分支保持完全一致。 */
}

void ui_spec_flush(u32 fb)
{
    Xil_DCacheFlushRange((UINTPTR)(fb + ((u32)SP_WAVE_Y * LCD_GFX_WIDTH * 3u)),
                         (u32)SP_WAVE_H * LCD_GFX_WIDTH * 3u);
    Xil_DCacheFlushRange((UINTPTR)(fb + ((u32)SP_BOTTOM_Y * LCD_GFX_WIDTH * 3u)),
                         (u32)SP_BOTTOM_H * LCD_GFX_WIDTH * 3u);
}

void ui_spec_flush_chrome(u32 fb)
{
    Xil_DCacheFlushRange((UINTPTR)fb,
                         (u32)(UI_OSC_INFO_Y + UI_OSC_INFO_H) * LCD_GFX_WIDTH * 3u);
}
/* ★ CZT 幅度 -> 0.1dB(与频谱仪 peak_db 同一把标定: sp_mag_to_db10 + ref_mag) */
s32 ui_spec_czt_to_db10(const ui_spec_t *s, u32 czt_mag)
{
    u16 mm = (czt_mag > 65535u) ? 65535u : (u16)czt_mag;

    return (s32)sp_mag_to_db10(mm, s->ref_mag);
}
/* ★ 让子窗口底板作废: 下一帧重新存一次"当前画面"的底板。
   用途: 切换模式/CZT 点数后, 屏上内容已换, 旧底板(带旧谱线)若被贴回就是残迹。 */
void ui_spec_czt_popup_invalidate(void)
{
    /* ★ 这里以前清的是 czt_pop_saved —— 一套【没有任何调用者】的独立底板缓存,
       所以本函数实际是空操作(注释却写着"底板作废", 属于谎报)。
       Chirp-Z 子窗口现在由窗口层负责(ui_app.c: ui_win_kind()==4 + ui_win_save/restore),
       真正让曲线重画的是紧随本调用之后的 app.spec.prev_valid = 0u。
       保留空函数只为不动 helloworld.c 的调用处。 */
}
/* ★ 只重画"模式1 那一行的右半部"(点箭头专用): 不填整块、不碰边框/其它行/圆点。
   点箭头时若走整块重画(ui_spec_draw_czt_popup)会把三行文字+圆点+边框全重绘 -> 旁边那行闪。 */
void ui_spec_draw_czt_bin_row(u32 fb, const ui_spec_t *s)
{
    int ry = UI_CZT_POP_Y + 22 + UI_CZT_POP_RH;          /* i = 1 那一行 */
    int xl = UI_CZT_POP_BIN_X - 6;
    int w  = (UI_CZT_POP_X + UI_CZT_POP_W - 8) - xl;
    char b[24];
    int  x0 = UI_CZT_POP_BIN_X;
    int  ay = ry + 6;

    if (w <= 0) { return; }
    lcd_fill_rect(fb, xl, ry + 1, w, UI_CZT_POP_RH - 2, UI_OSC_COLOR_INFO);
    (void)ui_draw_text(fb, x0, ay, "<", UI_OSC_COLOR_TEXT, 1);
    (void)sprintf(b, "%d bin", (int)s->czt_peak_bins);
    (void)ui_draw_text(fb, x0 + UI_CZT_POP_VAL_DX, ay, b, UI_OSC_COLOR_LABEL, 1);
    (void)ui_draw_text(fb, x0 + UI_CZT_POP_ARW2_DX, ay, ">", UI_OSC_COLOR_TEXT, 1);
}
/* ★ CZT 模式2 的曲线绘制: 复用频谱那套零件(清区/网格/画列/文字), 数据自成一格 ——
   横轴 = czt2_f0 + i*czt2_df, 纵轴 = czt2_mv[](mVpp)。不碰 mag[]/col_mag/span/f_start/peak_hz。 */
void ui_spec_draw_czt_curve(u32 fb, ui_spec_t *s)
{
    int x, prev_y = 0, have = 0;
    int n = (int)s->czt2_n;

    if (n < 2) { return; }
    /* ★ 纵轴直接用频谱那套线性纵轴 ui_spec_lin_y(): 它会按参考电压的【单位档】
       区分峰峰口径(Vpp/mVpp)与峰值口径(V/mV, 谱线取 Vpp/2) —— 这才是与频谱模式
       完全同一套坐标。以前这里自己算 mx(先是"本帧峰值 x1.1", 后改成 ref_mag 却漏了 /2)
       都会让模式2 的曲线高度与频谱不一致。 */

    /* ★ 只在"布局失效"时铺底+画网格: 原来每帧无条件铺 800x297(712KB) + 网格, 是模式2 最大的写屏开销。
       参数/量程/模式变化时上层会置 czt2_relayout, 那时才整体重画; 平时只重画发生变化的列。 */
    if ((s->prev_valid == 0u) || (s->czt2_relayout != 0u)) {
    s->czt2_relayout = 0u;
    lcd_fill_rect(fb, SP_WAVE_X, SP_WAVE_Y, SP_WAVE_W, SP_WAVE_H, UI_OSC_COLOR_BG);
    ui_wave_grid(fb);
    /* ★ ui_wave_grid() 画的是示波器那块(高 UI_OSC_WAVE_H=264); 频谱的绘图区比它高一格,
       这里把多出来的那一格(SP_WAVE_Y+UI_OSC_WAVE_H .. SP_WAVE_Y+SP_WAVE_H)按同一套网格补上,
       颜色规则完全复用 sp_bg_at(), 所以和上面那部分无缝衔接。 */
    {
        int gy, gx;

        /* ★ 按行填色 + 只在网格列补像素:
           原来是 800x33 = 2.64 万次单像素写, 单帧写屏时间过长会跨过 LCD 帧边界 -> 撕裂/乱闪;
           现在每行一次 lcd_hline(33 次) + 竖直网格线交点(SP_DIV_W=80 => 实际 10 列 x 33 行 = 330 次 lcd_pixel)。 */
        for (gy = (UI_OSC_WAVE_Y + UI_OSC_WAVE_H - 1); gy < (SP_WAVE_Y + SP_WAVE_H); gy++) {   /* ★ 从 335 起: ui_draw_grid 在 335 画了示波器的白色底边, 频谱变高后它落在绘图区中间 -> 用正确的网格色盖掉 */
            lcd_hline(fb, SP_WAVE_X, SP_WAVE_X + SP_WAVE_W - 1, gy,
                      (((gy - SP_WAVE_Y) % SP_DIV_H) == 0) ? UI_OSC_COLOR_GRID : UI_OSC_COLOR_BG);
            for (gx = SP_WAVE_X; gx < (SP_WAVE_X + SP_WAVE_W); gx++) {
                if ((gx % SP_DIV_W) == 0) { lcd_pixel(fb, gx, gy, sp_bg_at(gx, gy)); }
            }
            /* ★ 右白框那一列(x=799)不是 SP_DIV_W 的倍数, 上面补不到 -> 单独补回, 否则 335..368 这段右边框会断 */
            lcd_pixel(fb, SP_WAVE_X + SP_WAVE_W - 1, gy, UI_OSC_COLOR_FRAME);
        }
    }
    {   int xx; for (xx = 0; xx < UI_SPEC_COLS; xx++) { s->prev[xx].on = 0u; } }   /* XXFIX: 铺底后必须让 prev[] 全部失效 —— 否则下一帧会按【陈旧的 top/bot】去擦, 擦错位置 -> 旧波形擦不掉、和新波形叠着显示。注意: 只在铺底时清(在 if 内), 平时不清, 这样增量擦除才是活的。 */
    }   /* ★ 条件铺底结束 */
    /* ★ 已删除: 这一行原来在 2046 的 if 之外, 每帧把 800 列的 on 清 0 -> 2086-2095 的"增量擦旧段"永远不执行 -> 曲线只画不擦、逐帧累积成拖影。铺底分支本身就会重画所有列, 不需要它。 */
    s->cur_tw = 0;

    for (x = 0; x < UI_SPEC_COLS; x++) {
        /* XXMAX: 本列覆盖的 bin 区间 [i2, i2n] 取【最大值】—— 与频谱路径(858-891)同一工程约定:
           原来每列只取 1 个 bin, 当 m 远大于 800 列时(最多 10.24 bin/列)会漏掉真实峰值 ->
           曲线画低/顶部抖动, 且 m>800 时曲线最高点与左上角读数(全精度 argmax)对不上。
           n <= 800 时 i2n==i2, 行为与原来完全一致(零副作用)。 */
        int i2  = (int)(((double)x * (double)(n - 1)) / (double)(UI_SPEC_COLS - 1));
        int i2n = (int)(((double)(x + 1) * (double)(n - 1)) / (double)(UI_SPEC_COLS - 1));
        u32 v2;
        int y;
        if (i2n >= n)  { i2n = n - 1; }
        if (i2n < i2)  { i2n = i2; }
        v2 = (u32)s->czt2_mv[i2];   /* XXDBL: czt2_mv 已是 double, 这里取整到 1 码(比原来的 0.033 码细 30 倍) */
        {   int kk; for (kk = i2 + 1; kk <= i2n; kk++) { if ((u32)s->czt2_mv[kk] > v2) { v2 = (u32)s->czt2_mv[kk]; } } }   /* XXDBL: double 源, 取区间最大 */
        y = ui_spec_lin_y(s, v2);      /* ★ 用你写好的线性纵轴(自动处理 V/mV 的峰值口径) */
        int t, b;

        if (y < SP_CLIP_Y0) { y = SP_CLIP_Y0; }
        if (y > SP_CLIP_Y1) { y = SP_CLIP_Y1; }
        t = y; b = y;
        if (have != 0) {
            if (prev_y < t) { t = prev_y; }
            if (prev_y > b) { b = prev_y; }
        }
        /* ★ 增量: 上一帧这一列画过的范围若比这次高/低, 先把多出来的部分抹成背景(sp_bg_at 保住网格/边框), 再画新范围 */
        if (s->prev[x].on != 0u) {
            if ((int)s->prev[x].top < t) {   /* ★ 逐像素用 sp_bg_at: 一整段用同一个颜色会把跨过的横网格线擦出缺口(且累积) */
                int yy;
                for (yy = (int)s->prev[x].top; yy < t; yy++) { lcd_pixel(fb, SP_WAVE_X + x, yy, sp_bg_at(SP_WAVE_X + x, yy)); }
            }
            if ((int)s->prev[x].bot > b) {
                int yy;
                for (yy = b + 1; yy <= (int)s->prev[x].bot; yy++) { lcd_pixel(fb, SP_WAVE_X + x, yy, sp_bg_at(SP_WAVE_X + x, yy)); }
            }
        }
        s->prev[x].top = (s16)t;
        s->prev[x].bot = (s16)b;
        s->prev[x].on  = 1u;
        sp_paint_col(fb, s, x, t, b);
        prev_y = y; have = 1;
    }

    {   /* 左上角读数: 峰值频率 + 峰值 Vpp */
        int  kp = 0;
        char t1[24], t2[24], ln[64];
        s32  hz;

        /* ★ 峰位置用全精度求出的那个(见上): 这里只保留用 kp 取 Vpp */
        kp = (int)s->czt2_peak_i;
        hz = s->czt2_peak_hz;
        ui_spec_fmt_freq(t1, hz);
        sp_fmt_volt(t2, (u32)s->czt2_mv[kp], s->mv_fs);   /* XXDBL: double 源取整显示 */
        (void)sprintf(ln, "CZT %s  %s", t1, t2);
        /* ★ 先把这一行抹成背景再画字: 读数长度变化(如 1.234kHz -> 12.345kHz)时旧字会露出来。 */
        /*   注意: 现在只按【文字宽度】填, 并把该范围内的竖网格列用 sp_bg_at 补回(见下面), 不再是整块纯色填。 */
        {   /* ★ 只按【文字实际宽度】抹背景, 再把这几个网格列补回来:
               原来固定填 420x20 —— 既会横向抹掉曲线左段(该区域 y=74..93 在曲线可达范围内),
               也会盖掉 x=80/160/240/320/400 这 5 条竖网格线(列循环只画曲线色, 不补网格, 且是增量刷新 -> 不会自愈)。 */
            int tw = (int)ui_text_size(ln, 1) + 8;
            int xx;

            if (tw > (SP_WAVE_W - 8)) { tw = SP_WAVE_W - 8; }
            lcd_fill_rect(fb, SP_WAVE_X + 4, SP_WAVE_Y + 2, tw, 18, UI_OSC_COLOR_BG);
            for (xx = SP_WAVE_X + 4; xx < (SP_WAVE_X + 4 + tw); xx++) {
                if ((xx % SP_DIV_W) == 0) {
                    int yy;

                    for (yy = SP_WAVE_Y + 2; yy < (SP_WAVE_Y + 20); yy++) { lcd_pixel(fb, xx, yy, sp_bg_at(xx, yy)); }
                }
            }
        }
        (void)ui_draw_text(fb, SP_WAVE_X + 6, SP_WAVE_Y + 4, ln, UI_OSC_COLOR_TEXT, 1);
    }
}
/******************************************************************************
 * ui_awg.c - 信号发生器界面实现(DAC1 / DAC2 两路输出)
 *
 * 预览: 普通波形按解析式画 2 个周期(只看形状, 按本格铺满);
 *       BRAM 波形直接画 g_awg_tab[] 里的真实一周期(每列取覆盖样点的 min/max,
 *       这样 AM/FM 的包络也看得见)。预览只在参数变化时重画, 不每帧写屏。
 * 参数区: 3 行可见, 可上下滚动(到顶/到底停住), 右侧滚动条显示位置。
 ******************************************************************************/
#include "ui_awg.h"
#include "ui_screens.h"
#include "lcd_gfx.h"
#include "xil_cache.h"
#include "dds_wave.h"          /* g_awg_tab[] / g_awg_tab_ok / ch_data_len */
#include <string.h>

/* ---------------- 布局 ---------------- */
#define AWG_PANE_CY     (UI_AWG_PREV_Y + (UI_AWG_PREV_H / 2))
#define AWG_PANE_HALF   150     /* 半高: 右栏 368 高, 留边 */
#define AWG_ROW_Y0      UI_AWG_PANEL_Y
#define AWG_ROW_H       UI_AWG_ROW_H
#define AWG_NAME_X      6
#define AWG_BOX_X       118     /* 框与数字整体右移(行名位置不动), 118+110=228 仍留 12px 余量 */
#define AWG_TEXT_DY     (3 + (((int)UI_AWG_ROW_H - 7 - 16) / 2))   /* 文字在行内的垂直居中偏移 */
#define AWG_BOX_W       110     /* 100..210, 左栏 240 内 */
#define AWG_UNIT_X      380
#define AWG_UNIT_W      72
#define AWG_ARR_W       28
#define AWG_SB_X        782
#define AWG_SB_W        16
#define AWG_NO_FRAC     0xFFFFFFFFu

/* ---------------- 小工具 ---------------- */
static s32 awg_clamp(s32 v, s32 lo, s32 hi)
{
    if (v < lo) { return lo; }
    if (v > hi) { return hi; }
    return v;
}

static int awg_u2s(char *b, u32 v)
{
    char t[12];
    int n = 0, i;

    if (v == 0u) { b[0] = '0'; b[1] = '\0'; return 1; }
    while ((v > 0u) && (n < 11)) { t[n++] = (char)('0' + (int)(v % 10u)); v /= 10u; }
    for (i = 0; i < n; i++) { b[i] = t[n - 1 - i]; }
    b[n] = '\0';
    return n;
}

static void awg_put_unit(char *b, int n, const char *unit)
{
    int i = 0;
    while ((unit[i] != '\0') && (n < 30)) { b[n++] = unit[i++]; }
    b[n] = '\0';
}

/* 整数部分 + 3 位小数(frac3 已按 3 位四舍五入, >=1000 要进位) + 单位 */
static void awg_fmt3(char *b, u32 whole, u32 frac3, const char *unit)
{
    int n;
    if (frac3 >= 1000u) { frac3 -= 1000u; whole++; }
    n = awg_u2s(b, whole);
    b[n++] = '.';
    b[n++] = (char)('0' + (int)((frac3 / 100u) % 10u));
    b[n++] = (char)('0' + (int)((frac3 / 10u) % 10u));
    b[n++] = (char)('0' + (int)(frac3 % 10u));
    awg_put_unit(b, n, unit);
}

/* 整数部分 + 1 位小数(frac1 0..9, 进位同理) + 单位 */
static void awg_fmt1(char *b, u32 whole, u32 frac1, const char *unit)
{
    int n;
    if (frac1 > 9u) { frac1 -= 10u; whole++; }
    n = awg_u2s(b, whole);
    b[n++] = '.';
    b[n++] = (char)('0' + (int)frac1);
    awg_put_unit(b, n, unit);
}

static void awg_fmt_i(char *b, u32 whole, const char *unit)
{
    int n = awg_u2s(b, whole);
    awg_put_unit(b, n, unit);
}

/* 归一化正弦 -1000..1000 (Bhaskara 近似, 误差 <0.2%, 只用于预览画形状)
   phase: 0..1023 对应 0..360° */
static s32 awg_sin_q(u32 phase)
{
    u32 t = phase & 1023u;
    u32 deg, k;
    s32 s;

    if (t < 512u) { deg = (t * 180u) / 512u; }
    else          { deg = ((t - 512u) * 180u) / 512u; }
    k = deg * (180u - deg);
    s = (s32)((4u * k * 1000u) / (40500u - k));
    return (t < 512u) ? s : -s;
}

/* 普通 DDS 的 4 种波形(归一化 -1000..1000), phase: 0..1023 一个周期 */
static s32 awg_shape_val(u8 wave, u32 phase)
{
    u32 ph = phase & 1023u;

    switch (wave) {
    case UI_AWG_W_SQUARE:   return (ph < 512u) ? 1000 : -1000;
    case UI_AWG_W_TRIANGLE:
        return (ph < 512u) ? ((s32)ph * 2000 / 512 - 1000)
                           : (3000 - (s32)ph * 2000 / 512);
    case UI_AWG_W_SAW:      return ((s32)ph * 2000 / 1024) - 1000;
    case UI_AWG_W_SINE:
    default:                return awg_sin_q(ph);
    }
}

/* ---------------- 名字 / 清单 / 范围 ---------------- */
const char *ui_awg_wave_name(u8 wave)
{
    switch (wave) {
    case UI_AWG_W_SINE:     return "正弦";
    case UI_AWG_W_SQUARE:   return "方波";
    case UI_AWG_W_TRIANGLE: return "三角";
    case UI_AWG_W_SAW:      return "锯齿";
    case UI_AWG_W_PULSE:    return "方波占空比";
    case UI_AWG_W_SINC:     return "Sinc";
    case UI_AWG_W_HARM:     return "谐波合成";
    case UI_AWG_W_AM:       return "AM";
    case UI_AWG_W_DSB:      return "DSB";
    case UI_AWG_W_FM:       return "FM";
    case UI_AWG_W_PM:       return "PM";
    case UI_AWG_W_ASK:      return "2ASK";
    default:                return "?";
    }
}

/* 按钮栏用的短名: 7 个按钮每个只有 ~110px, 长名字会被下一个按钮盖掉(看起来"缺字") */
const char *ui_awg_wave_short(u8 wave)
{
    switch (wave) {
    case UI_AWG_W_SINE:     return "正弦";
    case UI_AWG_W_SQUARE:   return "方波";
    case UI_AWG_W_TRIANGLE: return "三角";
    case UI_AWG_W_SAW:      return "锯齿";
    case UI_AWG_W_PULSE:    return "占空比";
    case UI_AWG_W_SINC:     return "Sinc";
    case UI_AWG_W_HARM:     return "谐波";
    case UI_AWG_W_AM:       return "AM";
    case UI_AWG_W_DSB:      return "DSB";
    case UI_AWG_W_FM:       return "FM";
    case UI_AWG_W_PM:       return "PM";
    case UI_AWG_W_ASK:      return "2ASK";
    default:                return "?";
    }
}

const char *ui_awg_shape_name(u8 shape) { return ui_awg_wave_name(shape); }

u8 ui_awg_is_bram(u8 wave) { return (wave >= UI_AWG_W_PULSE) ? 1u : 0u; }

s32 ui_awg_freq_max(u8 wave)
{
    /* ★ 谐波(AWG_W_HARM)单独放开到 5MHz:
       谐波是软件合成一张 4096 点表再由 BRAM-DDS 播放, 硬件是 32 位相位累加器 + 120MHz 采样,
       5MHz 时频率字 = 5e6*35.7913941333 = 178,956,970, 远小于 int32 上限(溢出临界约 60MHz),
       每周期仍有 24 个相位码, 8 次谐波(40MHz)也在 Nyquist 内 ⇒ 可以放到 5MHz。
       只放开谐波: AM/DSB/FM/PM/ASK 的表内含 fmfc(最多 256)个载波周期, 基波超过约 234kHz
       载波就会越 Nyquist 混叠 ⇒ 它们必须保留 100kHz 的限制。 */
    if (wave == UI_AWG_W_HARM) {
        return (s32)UI_AWG_FREQ_MAX_HARM_HZ;
    }
    return (ui_awg_is_bram(wave) != 0u) ? (s32)UI_AWG_FREQ_MAX_BRAM_HZ
                                        : (s32)UI_AWG_FREQ_MAX_HZ;
}

u8 ui_awg_wave_list(u8 ch, u8 *out)
{
    u8 n = 0u, i;

    for (i = 0u; i < UI_AWG_W_CNT; i++) {
        if ((ch != 0u) && (ui_awg_is_bram(i) != 0u)) { continue; }   /* DAC2 只能普通 DDS */
        out[n++] = i;
    }
    return n;
}

/* ---------------- 参数行表 ---------------- */
static int awg_add_row(ui_awg_row_t *out, int n, int max, u8 id, u8 kind, u8 unit)
{
    if (n < max) {
        out[n].id = id; out[n].kind = kind; out[n].unit = unit;
        n++;
    }
    return n;
}

int ui_awg_rows(const ui_awg_t *a, ui_awg_row_t *out, int max)
{
    const ui_awg_ch_t *c = &a->ch[a->sel & 1u];
    int n = 0;

    /* 公共 3 行: 频率 / 幅度 / 相位 */
    n = awg_add_row(out, n, max, UI_AWG_P_FREQ,  UI_AWG_K_NUM, UI_AWG_U_HZ);
    n = awg_add_row(out, n, max, UI_AWG_P_VPP,   UI_AWG_K_NUM, UI_AWG_U_VPP);
    n = awg_add_row(out, n, max, UI_AWG_P_PHASE, UI_AWG_K_NUM, UI_AWG_U_DEG);

    /* 各波形自己的形状参数 */
    switch (c->wave) {
    case UI_AWG_W_PULSE:
        n = awg_add_row(out, n, max, UI_AWG_P_DUTY, UI_AWG_K_NUM, UI_AWG_U_PCT);
        break;
    case UI_AWG_W_SINC:
        n = awg_add_row(out, n, max, UI_AWG_P_WIDE, UI_AWG_K_NUM, UI_AWG_U_INT);
        break;
    case UI_AWG_W_HARM:
        n = awg_add_row(out, n, max, UI_AWG_P_HARM_N, UI_AWG_K_ENUM, 0u);
        n = awg_add_row(out, n, max, UI_AWG_P_HARM_H, UI_AWG_K_ENUM, 0u);
        n = awg_add_row(out, n, max, UI_AWG_P_HARM_A, UI_AWG_K_NUM, UI_AWG_U_VPP);
        n = awg_add_row(out, n, max, UI_AWG_P_HARM_P, UI_AWG_K_NUM, UI_AWG_U_DEG);
        break;
    case UI_AWG_W_AM:
        n = awg_add_row(out, n, max, UI_AWG_P_AM_M, UI_AWG_K_NUM, UI_AWG_U_PCT);
        n = awg_add_row(out, n, max, UI_AWG_P_FMFC, UI_AWG_K_NUM, UI_AWG_U_INT);
        break;
    case UI_AWG_W_DSB:
        n = awg_add_row(out, n, max, UI_AWG_P_FMFC, UI_AWG_K_NUM, UI_AWG_U_INT);
        break;
    case UI_AWG_W_FM:
        n = awg_add_row(out, n, max, UI_AWG_P_FMFC,  UI_AWG_K_NUM, UI_AWG_U_INT);
        n = awg_add_row(out, n, max, UI_AWG_P_FM_KF, UI_AWG_K_NUM, UI_AWG_U_X10);
        break;
    case UI_AWG_W_PM:
        n = awg_add_row(out, n, max, UI_AWG_P_FMFC,  UI_AWG_K_NUM, UI_AWG_U_INT);
        n = awg_add_row(out, n, max, UI_AWG_P_PM_KP, UI_AWG_K_NUM, UI_AWG_U_X10);
        break;
    case UI_AWG_W_ASK:
        n = awg_add_row(out, n, max, UI_AWG_P_ASK_CYC,  UI_AWG_K_ENUM, 0u);
        n = awg_add_row(out, n, max, UI_AWG_P_ASK_BITS, UI_AWG_K_ENUM, 0u);
        n = awg_add_row(out, n, max, UI_AWG_P_ASK_TYPE, UI_AWG_K_ENUM, 0u);
        if (c->ask_type == 2) {      /* ★ 自定义码型: 逐个码元设置 */
            n = awg_add_row(out, n, max, UI_AWG_P_ASK_SEL, UI_AWG_K_ENUM, 0u);
            n = awg_add_row(out, n, max, UI_AWG_P_ASK_VAL, UI_AWG_K_ENUM, 0u);
        }
        break;
    default:
        break;
    }
    return n;
}

int ui_awg_row_count(const ui_awg_t *a)
{
    ui_awg_row_t tmp[8];
    return ui_awg_rows(a, tmp, 8);
}

const char *ui_awg_row_name(const ui_awg_t *a, u8 id, char *tmp, int tmpsz)
{
    const ui_awg_ch_t *c = &a->ch[a->sel & 1u];

    if (tmpsz > 0) { tmp[0] = '\0'; }
    switch (id) {
    case UI_AWG_P_FREQ:  return "频率";
    case UI_AWG_P_VPP:   return "幅度";
    case UI_AWG_P_PHASE: return "相位";
    case UI_AWG_P_DUTY:  return "占空比";
    case UI_AWG_P_WIDE:  return "宽度";
    case UI_AWG_P_HARM_N: return "谐波次数";
    case UI_AWG_P_HARM_H: return "编辑序号";
    case UI_AWG_P_HARM_A:
        if ((tmp != 0) && (tmpsz >= 10)) {
            tmp[0] = (char)('0' + (int)(c->harm_h % 10u));
            tmp[1] = '\0';
            (void)strcat(tmp, "次幅度");   /* 行名限 1 位序号(谐波次数 <= 8) */
            return tmp;
        }
        return "谐波幅度";
    case UI_AWG_P_HARM_P:
        if ((tmp != 0) && (tmpsz >= 10)) {
            tmp[0] = (char)('0' + (int)(c->harm_h % 10u));
            tmp[1] = '\0';
            (void)strcat(tmp, "次相位");
            return tmp;
        }
        return "谐波相位";
    case UI_AWG_P_AM_M:  return "调制比";
    case UI_AWG_P_FMFC:  return "载波比";
    case UI_AWG_P_FM_KF: return "调频灵敏度";
    case UI_AWG_P_PM_KP: return "峰值相偏";
    case UI_AWG_P_ASK_CYC:  return "每码元周期数";
    case UI_AWG_P_ASK_BITS: return "码元数";
    case UI_AWG_P_ASK_TYPE: return "码型";
    case UI_AWG_P_ASK_SEL:  return "码元序号";
    case UI_AWG_P_ASK_VAL:  return "该码元值";
    default: return "";
    }
}

s32 ui_awg_row_get(const ui_awg_t *a, u8 id)
{
    const ui_awg_ch_t *c = &a->ch[a->sel & 1u];

    switch (id) {
    case UI_AWG_P_WAVE:  return (s32)c->wave;
    case UI_AWG_P_FREQ:  return c->freq_hz;
    case UI_AWG_P_VPP:   return c->vpp_mv;
    case UI_AWG_P_PHASE: return c->phase_mdeg;
    case UI_AWG_P_OUT:   return (s32)c->on;
    case UI_AWG_P_DUTY:  return c->duty_x10;
    case UI_AWG_P_WIDE:  return c->wide;
    case UI_AWG_P_HARM_N: return (s32)c->harm_n;
    case UI_AWG_P_HARM_H: return (s32)c->harm_h;
    case UI_AWG_P_HARM_A: return (s32)c->harm_a[c->harm_h - 1u];
    case UI_AWG_P_HARM_P: return c->harm_p[c->harm_h - 1u];
    case UI_AWG_P_AM_M:  return c->am_x10;
    case UI_AWG_P_FMFC:  return c->fmfc;
    case UI_AWG_P_FM_KF: return c->kf_x10;
    case UI_AWG_P_PM_KP: return c->kp_x10;
    case UI_AWG_P_ASK_CYC:  return c->ask_cyc;
    case UI_AWG_P_ASK_BITS: return c->ask_bits;
    case UI_AWG_P_ASK_TYPE: return c->ask_type;
    case UI_AWG_P_ASK_SEL:  return (s32)c->ask_sel;
    case UI_AWG_P_ASK_VAL:  return (s32)c->ask_code[(c->ask_sel > 0u) ? (c->ask_sel - 1u) : 0u];
    default: return 0;
    }
}

void ui_awg_row_set(ui_awg_t *a, u8 id, s32 v)
{
    ui_awg_ch_t *c = &a->ch[a->sel & 1u];

    switch (id) {
    case UI_AWG_P_FREQ:
        c->freq_hz = awg_clamp(v, UI_AWG_FREQ_MIN_HZ, ui_awg_freq_max(c->wave));
        break;
    case UI_AWG_P_VPP:
        c->vpp_mv = awg_clamp(v, 0, UI_AWG_VPP_MAX_MV);
        break;
    case UI_AWG_P_PHASE:
        c->phase_mdeg = awg_clamp(v, 0, 3600);      /* ★ 0.1 度单位: 0..3600(=360.0 度) */
        break;
    case UI_AWG_P_OUT:
        c->on = (v != 0) ? 1u : 0u;
        break;
    case UI_AWG_P_DUTY:
        c->duty_x10 = awg_clamp(v, 0, 1000);
        c->tab_dirty = 1u;
        break;
    case UI_AWG_P_WIDE:
        c->wide = awg_clamp(v, 1, 1000);
        c->tab_dirty = 1u;
        break;
    case UI_AWG_P_HARM_N:
        c->harm_n = (u8)awg_clamp(v, 2, UI_AWG_HARM_MAX);   /* 基波由"幅度"行给定, 次数从 2 起 */
        if (c->harm_h > c->harm_n) { c->harm_h = c->harm_n; }
        c->tab_dirty = 1u;
        break;
    case UI_AWG_P_HARM_H:
        c->harm_h = (u8)awg_clamp(v, 2, (s32)c->harm_n);   /* 序号也从 2 起 */
        break;
    case UI_AWG_P_HARM_A:
        c->harm_a[c->harm_h - 1u] = (u16)awg_clamp(v, 0, UI_AWG_VPP_MAX_MV);   /* 电压幅值(mV) */
        c->tab_dirty = 1u;
        break;
    case UI_AWG_P_HARM_P:
        c->harm_p[c->harm_h - 1u] = awg_clamp(v, 0, 3600);
        c->tab_dirty = 1u;
        break;
    case UI_AWG_P_AM_M:
        c->am_x10 = awg_clamp(v, 0, 1000);
        c->tab_dirty = 1u;
        break;
    case UI_AWG_P_FMFC:
        c->fmfc = awg_clamp(v, 1, 256);
        c->tab_dirty = 1u;
        break;
    case UI_AWG_P_FM_KF:
        c->kf_x10 = awg_clamp(v, 0, 1000);
        c->tab_dirty = 1u;
        break;
    case UI_AWG_P_PM_KP:
        c->kp_x10 = awg_clamp(v, 0, 100);
        c->tab_dirty = 1u;
        break;
    case UI_AWG_P_ASK_CYC:
        c->ask_cyc = awg_clamp(v, 1, 8);
        c->tab_dirty = 1u;
        break;
    case UI_AWG_P_ASK_BITS:
        c->ask_bits = awg_clamp(v, 8, 64);
        c->tab_dirty = 1u;
        break;
    case UI_AWG_P_ASK_TYPE:
        c->ask_type = awg_clamp(v, 0, 2);      /* 0=固定码 1=m序列 2=自定义 */
        c->tab_dirty = 1u;
        break;
    case UI_AWG_P_ASK_SEL:
        c->ask_sel = (u8)awg_clamp(v, 1, c->ask_bits);
        break;
    case UI_AWG_P_ASK_VAL:
        c->ask_code[(c->ask_sel > 0u) ? (c->ask_sel - 1u) : 0u] = (u8)awg_clamp(v, 0, 1);
        c->tab_dirty = 1u;
        break;
    default:
        break;
    }
    a->need_apply = 1u;
}

/* 枚举行切档: 步进表里的相邻档(到顶/到底停住) */
static s32 awg_enum_step(s32 cur, const s32 *tab, int n, int dir)
{
    int i, idx = 0;

    for (i = 0; i < n; i++) { if (tab[i] == cur) { idx = i; break; } }
    idx += (dir > 0) ? 1 : -1;
    if (idx < 0) { idx = 0; }
    if (idx >= n) { idx = n - 1; }
    return tab[idx];
}

void ui_awg_row_step(ui_awg_t *a, u8 id, int dir)
{
    static const s32 cyc_tab[4]  = { 1, 2, 4, 8 };
    static const s32 bits_tab[4] = { 8, 16, 32, 64 };
    s32 v = ui_awg_row_get(a, id);

    switch (id) {
    case UI_AWG_P_HARM_N:
    case UI_AWG_P_HARM_H:
        v += (dir > 0) ? 1 : -1;
        break;
    case UI_AWG_P_ASK_CYC:
        v = awg_enum_step(v, cyc_tab, 4, dir);
        break;
    case UI_AWG_P_ASK_BITS:
        v = awg_enum_step(v, bits_tab, 4, dir);
        break;
    case UI_AWG_P_ASK_TYPE:
        v = (v >= 2) ? 0 : (v + 1);
        break;
    case UI_AWG_P_ASK_SEL:
        v += (dir > 0) ? 1 : -1;
        break;
    case UI_AWG_P_ASK_VAL:
        v = (v != 0) ? 0 : 1;
        break;
    default:
        return;
    }
    ui_awg_row_set(a, id, v);
}

/* 数值行的键盘参数: unit(UI_AWG_U_xxx), dec(小数位), vmin/vmax(内部量纲) */
/* 数值行的键盘配置。频率按当前值选档: <1kHz 用 Hz(整数) / <1MHz 用 kHz(3 位小数,
   精确到 1Hz) / >=1MHz 用 MHz(6 位小数, 才能精确到 1Hz)。 */
static void awg_kbd_init_d(char *b, s32 scaled, int dec)
{
    u32 a = (scaled < 0) ? 0u : (u32)scaled;
    u32 p = 1u;
    int i, n;

    for (i = 0; i < dec; i++) { p *= 10u; }
    n = awg_u2s(b, a / p);
    if (dec > 0) {
        u32 f = a % p;
        b[n++] = '.';
        for (i = dec - 1; i >= 0; i--) {
            u32 d = 1u;
            int k;
            for (k = 0; k < i; k++) { d *= 10u; }
            b[n++] = (char)('0' + (int)((f / d) % 10u));
        }
    }
    b[n] = '\0';
}

void ui_awg_row_kbd(const ui_awg_t *a, u8 id, u8 *ku, u8 *dec, u8 *len_max, char *init)
{
    const ui_awg_ch_t *c = &a->ch[a->sel & 1u];
    s32 v = ui_awg_row_get(a, id);

    /* ★ init 是"键盘定点值" = 输入数字 x 1000 = 显示值 x 10^dec:
         频率: kHz 档 3 位小数(1Hz, 与 x1000 定点正好对上); <1kHz 用 Hz 整数档
         幅度: 内部 mV -> 显示 V, 1 位小数   => init 传 v/100
         相位/百分比/灵敏度: 内部就是 x10 的量 => init 直接传 v
       (之前这里把 mV 当 V、把 x10 当 x0.1 传: 键盘一打开初值就是错的,
        回车后幅度会直接顶到满幅, 所以"实际幅值与设置不同") */
    *ku = UI_AWG_KU_INT; *dec = 0; *len_max = 6;
    switch (id) {
    case UI_AWG_P_FREQ:
        if (v >= 1000) {
            *ku = UI_AWG_KU_KHZ; *dec = 3; *len_max = 9;     /* "20000.000" 最长 9 字符 */
            init[0] = '\0';                    /* XXAWGFREQ: 不预填(3位小数已满 -> 按数字没反应; len_max=9 保留) */
        } else {
            *ku = UI_AWG_KU_HZ;  *dec = 0; *len_max = 8;
            awg_kbd_init_d(init, v, 0);
        }
        return;
    case UI_AWG_P_VPP:    *ku = UI_AWG_KU_VPP; *dec = 1; *len_max = 6; init[0] = '\0'; return;   /* ★ 不预填 + 6 字符: 原来预填 "11.5"(4 字符) 正好占满 len_max=4 -> kbd_push_char 把新数字全丢掉(按键没反应) */
    case UI_AWG_P_PHASE:  *ku = UI_AWG_KU_DEG; *dec = 1; *len_max = 5; init[0] = '\0';   /* XXNOPRE: 不预填(初值正好占满 dec=1 -> 按数字没反应) */ return;
    case UI_AWG_P_DUTY:   *ku = UI_AWG_KU_PCT; *dec = 1; *len_max = 5; init[0] = '\0';   /* XXNOPRE: 不预填(初值正好占满 dec=1 -> 按数字没反应) */ return;
    case UI_AWG_P_WIDE:   *ku = UI_AWG_KU_INT; *dec = 0; *len_max = 4; awg_kbd_init_d(init, v, 0); return;
    case UI_AWG_P_HARM_A: *ku = UI_AWG_KU_VPP; *dec = 1; *len_max = 6; init[0] = '\0'; return;   /* ★ 同上(谐波幅度行) */
    case UI_AWG_P_HARM_P: *ku = UI_AWG_KU_DEG; *dec = 1; *len_max = 5; init[0] = '\0';   /* XXNOPRE: 不预填(初值正好占满 dec=1 -> 按数字没反应) */ return;
    case UI_AWG_P_AM_M:   *ku = UI_AWG_KU_PCT; *dec = 1; *len_max = 5; init[0] = '\0';   /* XXNOPRE: 不预填(初值正好占满 dec=1 -> 按数字没反应) */ return;
    case UI_AWG_P_FMFC:   *ku = UI_AWG_KU_INT; *dec = 0; *len_max = 3; awg_kbd_init_d(init, v, 0); return;
    case UI_AWG_P_FM_KF:  *ku = UI_AWG_KU_X10; *dec = 1; *len_max = 5; init[0] = '\0';   /* XXNOPRE: 不预填(初值正好占满 dec=1 -> 按数字没反应) */ return;
    case UI_AWG_P_PM_KP:  *ku = UI_AWG_KU_X10; *dec = 1; *len_max = 4; init[0] = '\0';   /* XXNOPRE: 不预填(初值正好占满 dec=1 -> 按数字没反应) */ return;
    default: break;
    }
    (void)c;
    init[0] = '0'; init[1] = '\0';
}

/* 键盘定点值(输入数字 x 1000) -> 参数内部量纲 */
s32 ui_awg_kbd_to_val(const ui_awg_t *a, u8 id, u8 ku, s32 kv)
{
    (void)a;
    switch (id) {
    case UI_AWG_P_FREQ:
        if (ku == UI_AWG_KU_MHZ) { return kv * 1000; }   /* MHz x1000 = kHz -> x1000 = Hz */
        if (ku == UI_AWG_KU_KHZ) { return kv; }          /* kHz x1000 = Hz */
        return kv / 1000;                                /* Hz 整数 */
    case UI_AWG_P_VPP:    return kv;                     /* V x1000 = mV */
    case UI_AWG_P_HARM_A: return kv;                     /* ★ 谐波幅度: 输入 Vpp x1000 = mV(缺这一行会落到 default 被 /1000) */
    case UI_AWG_P_PHASE:
    case UI_AWG_P_DUTY:
    case UI_AWG_P_HARM_P:
    case UI_AWG_P_AM_M:
    case UI_AWG_P_FM_KF:
    case UI_AWG_P_PM_KP:  return kv / 100;               /* 显示 0.1 步进 -> 内部 x10 */
    case UI_AWG_P_WIDE:
    case UI_AWG_P_FMFC:   return kv / 1000;              /* 整数 */
    default:              return kv / 1000;
    }
}
void ui_awg_fmt_freq(char *b, s32 hz)
{
    u32 a = (hz < 0) ? 0u : (u32)hz;

    if (a >= 1000000u) {
        /* 3 位小数 = 1kHz 分辨率, 所以要把第 4 位之后的四舍五入上来 */
        awg_fmt3(b, a / 1000000u, ((a % 1000000u) + 500u) / 1000u, "MHz");
    } else if (a >= 1000u) {
        awg_fmt3(b, a / 1000u, a % 1000u, "kHz");      /* 3 位小数 = 1Hz, 精确 */
    } else {
        awg_fmt_i(b, a, "Hz");
    }
}

void ui_awg_fmt_vpp(char *b, s32 mv)
{
    u32 a = (mv < 0) ? 0u : (u32)mv;

    if (a >= 1000u) {
        awg_fmt3(b, a / 1000u, a % 1000u, "Vpp");
    } else {
        awg_fmt_i(b, a, "mVpp");
    }
}

void ui_awg_row_text(const ui_awg_t *a, u8 id, char *buf){
    s32 v = ui_awg_row_get(a, id);
    u32 av = (v < 0) ? 0u : (u32)v;

    switch (id) {
    case UI_AWG_P_FREQ:  ui_awg_fmt_freq(buf, v); break;
    case UI_AWG_P_VPP:   ui_awg_fmt_vpp(buf, v); break;
    case UI_AWG_P_PHASE: awg_fmt1(buf, av / 10u, av % 10u, "\xC2\xB0"); break;          /* 度 */
    case UI_AWG_P_DUTY:  awg_fmt1(buf, av / 10u, av % 10u, "%"); break;
    case UI_AWG_P_WIDE:  awg_fmt_i(buf, av, ""); break;
    case UI_AWG_P_HARM_N: awg_fmt_i(buf, av, ""); break;
    case UI_AWG_P_HARM_H: awg_fmt_i(buf, av, ""); break;
    case UI_AWG_P_HARM_A: awg_fmt3(buf, av / 1000u, av % 1000u, "Vpp"); break;   /* 直接显示电压幅值 */
    case UI_AWG_P_HARM_P: awg_fmt1(buf, av / 10u, av % 10u, "\xC2\xB0"); break;
    case UI_AWG_P_AM_M:  awg_fmt1(buf, av / 10u, av % 10u, "%"); break;
    case UI_AWG_P_FMFC:  awg_fmt_i(buf, av, ""); break;
    case UI_AWG_P_FM_KF: awg_fmt1(buf, av / 10u, av % 10u, ""); break;
    case UI_AWG_P_PM_KP: awg_fmt1(buf, av / 10u, av % 10u, ""); break;
    case UI_AWG_P_ASK_CYC:  awg_fmt_i(buf, av, ""); break;
    case UI_AWG_P_ASK_BITS: awg_fmt_i(buf, av, ""); break;
    case UI_AWG_P_ASK_TYPE: (void)strcpy(buf, (v == 2) ? "自定义" : ((v != 0) ? "m序列" : "固定码")); break;
    case UI_AWG_P_ASK_SEL:  awg_fmt_i(buf, av, ""); break;
    case UI_AWG_P_ASK_VAL:  (void)strcpy(buf, (v != 0) ? "1" : "0"); break;
    default: buf[0] = '\0'; break;
    }
}

/* ---------------- 动作 ---------------- */
void ui_awg_default(ui_awg_t *a)
{
    int i;

    (void)memset(a, 0, sizeof(*a));
    a->sel = 0u;
    a->scroll = 0u;

    for (i = 0; i < 2; i++) {
        ui_awg_ch_t *c = &a->ch[i];

        c->on = (i == 0) ? 1u : 0u;          /* DAC1 开, DAC2 关(与原来一致) */
        c->wave = (i == 0) ? UI_AWG_W_SINE : UI_AWG_W_SQUARE;
        c->freq_hz = (i == 0) ? 1000 : 2000;
        c->vpp_mv = 3300;
        c->phase_mdeg = 0;
        c->duty_x10 = 500;                   /* 50.0% */
        c->wide = 10;
        c->am_x10 = 500;                     /* 调制比 50.0% */
        c->fmfc = 20;
        c->kf_x10 = 50;                      /* Kf = 5.0 */
        c->kp_x10 = 5;                       /* Kp = 0.5 周 */
        {
            int k;

            for (k = 0; k < 64; k++) { c->ask_code[k] = (k == 0) ? 1u : 0u; }   /* 默认: 只有第 1 个码元为 1, 其余全 0 */
        }
        c->ask_sel = 1u;
        c->ask_cyc = 2;
        c->ask_bits = 16;
        c->ask_type = 2;   /* 默认"自定义", 用户可直接编 */
        c->harm_n = 2u;                      /* 默认只到 2 次(基波由幅度行) */
        c->harm_h = 2u;
        c->harm_a[0] = 0;                    /* 1 次=基波, 幅度由"幅度"行给定, 这里不用 */
        c->harm_p[0] = 0;
        c->tab_dirty = 1u;
    }
    a->need_apply = 1u;
}

void ui_awg_wave_next(ui_awg_t *a)
{
    ui_awg_ch_t *c = &a->ch[a->sel & 1u];
    u8 list[UI_AWG_W_CNT];
    u8 n, i, cur = 0u;

    n = ui_awg_wave_list(a->sel, list);
    if (n == 0u) { return; }
    for (i = 0u; i < n; i++) { if (list[i] == c->wave) { cur = i; break; } }
    c->wave = list[(u8)((cur + 1u) % n)];
    a->scroll = 0u;
    if (ui_awg_is_bram(c->wave) != 0u) { c->tab_dirty = 1u; }
    if (c->freq_hz > ui_awg_freq_max(c->wave)) { c->freq_hz = ui_awg_freq_max(c->wave); }
    a->need_apply = 1u;
}

/* 直接指定波形(波形子界面选中某一项时调用):
   当前通道不允许的波形(比如 DAC2 选 BRAM 那 8 种)会被忽略。 */
void ui_awg_wave_set(ui_awg_t *a, u8 wave)
{
    ui_awg_ch_t *c = &a->ch[a->sel & 1u];
    u8 list[UI_AWG_W_CNT];
    u8 n = ui_awg_wave_list(a->sel, list);
    u8 i, ok = 0u;

    if (wave >= UI_AWG_W_CNT) {
        return;
    }
    for (i = 0u; i < n; i++) {
        if (list[i] == wave) {
            ok = 1u;
            break;
        }
    }
    if (ok == 0u) {
        return;
    }
    c->wave = wave;
    a->scroll = 0u;
    if (ui_awg_is_bram(wave) != 0u) {
        c->tab_dirty = 1u;                      /* 需要重新生成 BRAM 表 */
    }
    if (c->freq_hz > ui_awg_freq_max(wave)) {
        c->freq_hz = ui_awg_freq_max(wave);      /* 非谐波的 BRAM 波形(AM/FM/PM/ASK)上限更低: 表内含多个载波周期, 高了会混叠 */
    }
    a->need_apply = 1u;
}

void ui_awg_shape_next(ui_awg_t *a) { ui_awg_wave_next(a); }

/* 切当前编辑通道: 只换编辑对象, 不改任何输出状态 */
void ui_awg_sel_toggle(ui_awg_t *a)
{
    a->sel = (a->sel == 0u) ? 1u : 0u;
    a->scroll = 0u;
}

void ui_awg_output_toggle(ui_awg_t *a)
{
    ui_awg_ch_t *c = &a->ch[a->sel & 1u];
    c->on = (c->on != 0u) ? 0u : 1u;
    a->need_apply = 1u;
}

void ui_awg_scroll(ui_awg_t *a, int dir)
{
    int n = ui_awg_row_count(a);
    int maxs = (n > (int)UI_AWG_ROW_VIS) ? (n - (int)UI_AWG_ROW_VIS) : 0;
    int s = (int)a->scroll + dir;

    if (s < 0) { s = 0; }              /* 到顶停住 */
    if (s > maxs) { s = maxs; }        /* 到底停住 */
    a->scroll = (u8)s;
    a->redraw_params = 1u;
}

void ui_awg_freq_step(ui_awg_t *a, int dir)
{
    ui_awg_ch_t *c = &a->ch[a->sel & 1u];
    s32 f = c->freq_hz;
    s32 base = 1, d;

    /* 1-2-5 档: 先找不超过当前值的档, 再进/退一档 */
    while ((base * 10) <= f) { base *= 10; }
    for (;;) {
        s32 c1 = base, c2 = base * 2, c5 = base * 5, c10 = base * 10;
        if ((f >= c10) && (base < 10000000)) { base *= 10; continue; }
        d = (dir > 0) ? ((f < c1) ? c1 : ((f < c2) ? c2 : ((f < c5) ? c5 : c10)))
                      : ((f > c5) ? c5 : ((f > c2) ? c2 : ((f > c1) ? c1 : base / 10)));
        break;
    }
    if (d < UI_AWG_FREQ_MIN_HZ) { d = UI_AWG_FREQ_MIN_HZ; }
    if (d > ui_awg_freq_max(c->wave)) { d = ui_awg_freq_max(c->wave); }
    c->freq_hz = d;
    a->need_apply = 1u;
}

void ui_awg_freq_decade(ui_awg_t *a, int dir)
{
    ui_awg_ch_t *c = &a->ch[a->sel & 1u];
    s32 f = c->freq_hz;

    if (dir > 0) { f *= 10; } else { f /= 10; }
    c->freq_hz = awg_clamp(f, UI_AWG_FREQ_MIN_HZ, ui_awg_freq_max(c->wave));
    a->need_apply = 1u;
}

void ui_awg_amp_step(ui_awg_t *a, int dir)
{
    ui_awg_ch_t *c = &a->ch[a->sel & 1u];
    s32 v = c->vpp_mv;
    static const s32 tab[9] = { 0, 10, 20, 50, 100, 200, 500, 1000, 10000 };
    int i, idx = 0;

    for (i = 0; i < 9; i++) { if (tab[i] <= v) { idx = i; } }
    idx += dir;
    if (idx < 0) { idx = 0; }
    if (idx > 8) { idx = 8; }
    c->vpp_mv = tab[idx];
    a->need_apply = 1u;
}

/* ---------------- 绘制 ---------------- */
void ui_awg_draw_status(u32 fb, const ui_awg_t *a)
{
    const ui_awg_ch_t *c = &a->ch[a->sel & 1u];
    int x, y = UI_OSC_INFO_Y + 4;

    /* 顶栏: 标题 + CH1/CH2 输出状态(不显示"源") */
    lcd_fill_rect(fb, 0, 0, LCD_GFX_WIDTH, UI_OSC_HEADER_H, UI_OSC_COLOR_BAR);
    (void)ui_draw_text(fb, 12, 8, "信号发生器", UI_OSC_COLOR_TEXT, 2);
    x = 470;
    x = ui_draw_text(fb, x, 16, "CH1 ", UI_OSC_COLOR_CH1, 1);
    x = ui_draw_text(fb, x, 16, a->ch[0].on ? "开" : "关",
                     a->ch[0].on ? UI_OSC_COLOR_TEXT : UI_OSC_COLOR_BAD, 1);
    x = ui_draw_text(fb, x + 24, 16, "CH2 ", UI_OSC_COLOR_CH2, 1);
    (void)ui_draw_text(fb, x, 16, a->ch[1].on ? "开" : "关",
                       a->ch[1].on ? UI_OSC_COLOR_TEXT : UI_OSC_COLOR_BAD, 1);
    lcd_hline(fb, 0, LCD_GFX_WIDTH - 1, UI_OSC_HEADER_H - 1, UI_OSC_COLOR_FRAME);

    /* 信息行: 通道 / 波形 / 输出 */
    lcd_fill_rect(fb, 0, UI_OSC_INFO_Y, LCD_GFX_WIDTH, UI_OSC_INFO_H, UI_OSC_COLOR_INFO);
    x = 12;
    x = ui_draw_text(fb, x, y, "通道 ", UI_OSC_COLOR_LABEL, 1);
    x = ui_draw_text(fb, x, y, (a->sel == 0u) ? "CH1 " : "CH2 ",
                     (a->sel == 0u) ? UI_OSC_COLOR_CH1 : UI_OSC_COLOR_CH2, 1);
    x = ui_draw_text(fb, x + 20, y, "波形 ", UI_OSC_COLOR_LABEL, 1);
    x = ui_draw_text(fb, x, y, ui_awg_wave_name(c->wave), UI_OSC_COLOR_TEXT, 1);
    x = ui_draw_text(fb, x + 20, y, "输出 ", UI_OSC_COLOR_LABEL, 1);
    (void)ui_draw_text(fb, x, y, c->on ? "开" : "关",
                       c->on ? UI_OSC_COLOR_TEXT : UI_OSC_COLOR_BAD, 1);
    lcd_hline(fb, 0, LCD_GFX_WIDTH - 1, UI_OSC_INFO_Y + UI_OSC_INFO_H - 1,
              UI_OSC_COLOR_FRAME);
}


/* 参数行起始 y: 行数少时(普通波形 3 行等)整体在左栏内垂直居中, 不挤在顶部。
   绘制与命中必须用同一个值, 否则点的位置和显示对不上。 */
static int awg_rows_y0(int n)
{
    int h = n * AWG_ROW_H;

    if (h > (int)UI_AWG_PANEL_H) {
        h = (int)UI_AWG_PANEL_H;
    }
    return AWG_ROW_Y0 + (((int)UI_AWG_PANEL_H - h) / 2);
}

void ui_awg_draw_params(u32 fb, const ui_awg_t *a)
{
    ui_awg_row_t rows[8];
    int n = ui_awg_rows(a, rows, 8);
    int i;

    lcd_fill_rect(fb, UI_AWG_PANEL_X, UI_AWG_PANEL_Y, UI_AWG_PANEL_W, UI_AWG_PANEL_H, UI_OSC_COLOR_PANEL);

    for (i = 0; i < (int)UI_AWG_ROW_VIS; i++) {
        int idx = (int)a->scroll + i;
        int y = awg_rows_y0(n) + i * AWG_ROW_H;
        char name[24];
        char val[24];
        const char *nm;
        int tw;

        if (idx >= n) { break; }
        nm = ui_awg_row_name(a, rows[idx].id, name, (int)sizeof(name));
        (void)ui_draw_text(fb, AWG_NAME_X, y + AWG_TEXT_DY, nm, UI_OSC_COLOR_LABEL, 1);

        /* 一行只有一个框: 值和单位拼在一起(单位已经写在 ui_awg_row_text() 里);
           枚举行把左右箭头画在同一个框的两端 */
        lcd_rect(fb, AWG_BOX_X, y + 3, AWG_BOX_W, AWG_ROW_H - 7, UI_OSC_COLOR_FRAME);
        if (rows[idx].kind == UI_AWG_K_ENUM) {
            (void)ui_draw_text(fb, AWG_BOX_X + 8, y + AWG_TEXT_DY, "<", UI_OSC_COLOR_TEXT, 1);
            (void)ui_draw_text(fb, AWG_BOX_X + AWG_BOX_W - 16, y + AWG_TEXT_DY, ">", UI_OSC_COLOR_TEXT, 1);
        }
        ui_awg_row_text(a, rows[idx].id, val);
        tw = (int)ui_text_size(val, 1);
        /* ★ 度数符号的补偿: '°'(U+00B0) 在字库里占 16px 宽, 但它的墨迹只在
           格子左上角(度数符号本来就该在上方), 可见宽度只有约 6px。按 16px 居中
           会让整串看着偏左, 这里把它按 6px 计, 正好把可见重心摆正。
           只影响以 '度' 结尾的相位行(其余行的单位都是 ASCII, 宽度准确)。 */
        if ((tw >= 16) && ((u8)val[strlen(val) - 2u] == 0xC2u) &&
            ((u8)val[strlen(val) - 1u] == 0xB0u)) {
            tw -= 10;
        }
        (void)ui_draw_text(fb, AWG_BOX_X + ((AWG_BOX_W - tw) / 2), y + AWG_TEXT_DY, val,
                           UI_OSC_COLOR_TEXT, 1);
    }

        lcd_hline(fb, UI_AWG_PANEL_X, UI_AWG_PANEL_X + UI_AWG_PANEL_W - 1, AWG_ROW_Y0 - 1, UI_OSC_COLOR_FRAME);
}

/* ---------------- 预览触发: 上升沿过零点 ----------------
   跟示波器一样, 从"由负变正穿过零点"的地方开始画, 换波形/换参数时波形不会左右乱跳。 */
static u32 awg_zero_phase(u8 wave)
{
    switch (wave) {
    case UI_AWG_W_SQUARE:   return 512u;    /* 0..511 为高, 512 处由低变高 */
    case UI_AWG_W_TRIANGLE: return 256u;    /* -1000 升到 +1000, 过零在 1/4 周期 */
    case UI_AWG_W_SAW:      return 512u;    /* 线性上升, 过零在中点 */
    case UI_AWG_W_SINE:
    default:                return 0u;      /* 正弦本身就是从 0 上升 */
    }
}

static u32 awg_bram_trigger(void)
{
    u32 n = (u32)ch_data_len;
    u32 i;

    if (g_awg_tab_ok == 0u) {
        return 0u;
    }
    for (i = 0u; i < n; i++) {
        u16 v0 = g_awg_tab[i];
        u16 v1 = g_awg_tab[(i + 1u) % n];

        if ((v0 < 8191u) && (v1 >= 8191u)) {    /* 中点下方升到上方 = 上升沿过零 */
            return i;
        }
    }
    return 0u;
}

void ui_awg_draw_preview(u32 fb, const ui_awg_t *a)
{
    const ui_awg_ch_t *c = &a->ch[a->sel & 1u];
    const u16 color = (a->sel == 0u) ? UI_OSC_COLOR_CH1 : UI_OSC_COLOR_CH2;
    int x, prev_y = -1;
    int ytop = UI_AWG_PREV_Y + 1;
    int ybot = UI_AWG_PREV_Y + UI_AWG_PREV_H - 2;
    u32 bstart = 0u;

    lcd_fill_rect(fb, UI_AWG_PREV_X, UI_AWG_PREV_Y, UI_AWG_PREV_W, UI_AWG_PREV_H,
                  UI_OSC_COLOR_BG);
    lcd_hline(fb, UI_AWG_PREV_X, UI_AWG_PREV_X + UI_AWG_PREV_W - 1, AWG_PANE_CY,
              UI_OSC_COLOR_GRID);


    if (ui_awg_is_bram(c->wave) != 0u) {
        bstart = awg_bram_trigger();            /* 上升沿过零触发点 */
        if (g_awg_tab_ok == 0u) {
            (void)ui_draw_text(fb, UI_AWG_PREV_X + 12, UI_AWG_PREV_Y + 12, "表未生成",
                               UI_OSC_COLOR_BAD, 1);
        } else {
            /* ★ 每列只取一个样点(从触发点起算), 再和上一列连成线。
               以前是每列取 min/max 画包络: AM/FM/PM/2ASK 这类载波周期多的波形
               (4096 点里几十个周期) 每列跨度都很大, 结果整片都是竖线 = 花屏。
               取点画线就和平常示波器看波形一样, 载波形状和调制包络都看得出来。 */
            for (x = 0; x < UI_AWG_PREV_W; x++) {
                u32 i0 = ((u32)x * (u32)ch_data_len) / (u32)UI_AWG_PREV_W;
                u32 idx = (bstart + i0) % (u32)ch_data_len;
                s32 v = (s32)g_awg_tab[idx];
                int y = AWG_PANE_CY - (int)(((v - 8191) * AWG_PANE_HALF) / 8191);
                int a0, b0, k;

                y = awg_clamp(y, ytop, ybot);
                if (prev_y >= 0) {
                    a0 = (prev_y < y) ? prev_y : y;
                    b0 = (prev_y < y) ? y : prev_y;
                    for (k = a0; k <= b0; k++) {
                        lcd_pixel(fb, UI_AWG_PREV_X + x, k, color);
                    }
                } else {
                    lcd_pixel(fb, UI_AWG_PREV_X + x, y, color);
                }
                prev_y = y;
            }
        }
    } else {
        /* 普通 DDS: 解析画 2 个周期, 从上升沿过零点开始 */
        for (x = 0; x < UI_AWG_PREV_W; x++) {
            u32 ph = (awg_zero_phase(c->wave) + (((u32)x * 2048u) / (u32)UI_AWG_PREV_W)) & 1023u;
            s32 v = awg_shape_val(c->wave, ph);
            int y = AWG_PANE_CY - (int)((v * AWG_PANE_HALF) / 1000);
            int a0, b0, k;

            y = awg_clamp(y, ytop, ybot);
            if (prev_y >= 0) {
                a0 = (prev_y < y) ? prev_y : y;
                b0 = (prev_y < y) ? y : prev_y;
                for (k = a0; k <= b0; k++) {
                    lcd_pixel(fb, UI_AWG_PREV_X + x, k, color);
                }
            } else {
                lcd_pixel(fb, UI_AWG_PREV_X + x, y, color);
            }
            prev_y = y;
        }
    }
    /* ★ 通道开关状态: 预览里一个显眼的角标(波形本身与开关无关, 一直显示)。
       开 = 深灰底白字; 关 = 红底黑字(一眼就能看到)。 */
    {
        const char *st = (c->on != 0u) ? "输出 开" : "输出 关";
        u16 bg = (c->on != 0u) ? UI_OSC_COLOR_TABBG : UI_OSC_COLOR_BAD;
        u16 fg = (c->on != 0u) ? UI_OSC_COLOR_TEXT : UI_OSC_COLOR_BG;
        int bw = (int)ui_text_size(st, 1) + 18;
        int bx = UI_AWG_PREV_X + UI_AWG_PREV_W - 10 - bw;   /* ★ 角标靠右 */

        lcd_fill_rect(fb, bx, UI_AWG_PREV_Y + 10, bw, 30, bg);
        lcd_rect(fb, bx, UI_AWG_PREV_Y + 10, bw, 30, UI_OSC_COLOR_FRAME);
        (void)ui_draw_text(fb, bx + 9, UI_AWG_PREV_Y + 17, st, fg, 1);
    }

    lcd_rect(fb, UI_AWG_PREV_X, UI_AWG_PREV_Y, UI_AWG_PREV_W, UI_AWG_PREV_H,
             UI_OSC_COLOR_FRAME);
}

void ui_awg_draw_live(u32 fb, ui_awg_t *a)
{
    /* 触摸/下发只打标志, 到帧边界才写屏(不会被液晶扫描追上)。
       redraw_preview: BRAM 表刚重新生成 -> 预览必须重画, 否则一直显示上一张波形。 */
    u8 dirty = 0u;

    /* ★ 每帧最多做一件重活:
       参数区便宜, 先做; 预览是"整格清屏+重画"(约 20 万像素), 留到下一帧。
       两件挤在同一帧里(尤其是数字键盘关闭那一帧还要贴回键盘底板)会超出帧时间,
       液晶扫描追上来就是"闪一下"。 */
    if (a->redraw_params != 0u) {
        a->redraw_params = 0u;
        ui_awg_draw_params(fb, a);
        dirty = 1u;
    } else if (a->redraw_preview != 0u) {
        a->redraw_preview = 0u;
        ui_awg_draw_preview(fb, a);
        dirty = 1u;
    }
    if (dirty != 0u) {
        ui_awg_flush(fb);
    }
}

void ui_awg_draw(u32 fb, ui_awg_t *a)
{
    lcd_clear(fb, UI_OSC_COLOR_BG);
    ui_awg_draw_status(fb, a);
    ui_awg_draw_preview(fb, a);
    ui_awg_draw_params(fb, a);
}

void ui_awg_flush(u32 fb)
{
    /* 左栏(参数区) + 右栏(预览) 两条整宽横带: 72..439 */
    Xil_DCacheFlushRange((UINTPTR)(fb + ((u32)UI_AWG_PANEL_Y * LCD_GFX_WIDTH * 3u)),
                         ((u32)UI_AWG_PANEL_H * LCD_GFX_WIDTH * 3u));
}

void ui_awg_flush_chrome(u32 fb)
{
    ui_osc_flush_chrome(fb);    /* 顶栏 + 信息行 */
}

/* ---------------- 参数区命中(触摸用) ---------------- */
int ui_awg_hit(const ui_awg_t *a, int x, int y, int *vis, u8 *id)
{
    ui_awg_row_t rows[8];
    int n = ui_awg_rows(a, rows, 8);
    int i, idx;

    if (vis != 0) { *vis = -1; }
    if (id != 0) { *id = UI_AWG_P_FREQ; }

    /* 3 行可见区 */
    if ((y < awg_rows_y0(n)) || (y >= awg_rows_y0(n) + ((n * AWG_ROW_H)))) {
        return UI_AWG_HIT_NONE;
    }
    i = (y - awg_rows_y0(n)) / AWG_ROW_H;
    idx = (int)a->scroll + i;
    if ((i < 0) || (i >= (int)UI_AWG_ROW_VIS) || (idx >= n)) {
        return UI_AWG_HIT_NONE;
    }
    if (vis != 0) { *vis = i; }
    if (id != 0) { *id = rows[idx].id; }

    if (rows[idx].kind == UI_AWG_K_ENUM) {
        if ((x >= AWG_BOX_X) && (x < AWG_BOX_X + AWG_ARR_W)) { return UI_AWG_HIT_LT; }
        if ((x >= AWG_BOX_X + AWG_BOX_W - AWG_ARR_W) && (x < AWG_BOX_X + AWG_BOX_W)) { return UI_AWG_HIT_GT; }
        return UI_AWG_HIT_NONE;
    }
    if ((x >= AWG_BOX_X) && (x < AWG_BOX_X + AWG_BOX_W)) { return UI_AWG_HIT_BOX; }
    return UI_AWG_HIT_NONE;
}



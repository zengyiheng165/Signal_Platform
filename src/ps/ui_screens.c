/******************************************************************************
 * ui_screens.c - LCD 界面层实现(中文 + 大字号)
 *
 * 双路示波器界面:
 *   y=  0..47   顶栏: "双路示波器"(32px) + 已同步/未同步 + 运行/停止药丸
 *   y= 48..71   信息行: 时基 1.000ms/div | 触发 CH1 上升沿 自动 电平 1.650V
 *   y= 72..407  波形区: 10x8 网格, CH1 黄 / CH2 青, 0V 标记, 触发箭头+虚线
 *   y=408..479  测量区: 通道1/通道2 面板(峰峰值/有效值/频率/周期)
 *
 * 中文点阵来自 ui_cn_font.h (16x16 界面文字, 32x32 标题)。
 ******************************************************************************/
#include "ui_screens.h"
#include "lcd_gfx.h"
#include "ui_cn_font.h"
#include "xil_cache.h"
#include <string.h>

/* ---------------- 内部常量 ---------------- */
#define UI_TITLE_SCALE  2                                   /* 标题: 16*2=32px */
#define UI_ROW_H        20
#define UI_WAVE_CY      (UI_OSC_WAVE_Y + UI_OSC_WAVE_H / 2)
#define UI_CLIP_X0      2
#define UI_CLIP_X1      (UI_OSC_WAVE_W - 3)
#define UI_CLIP_Y0      (UI_OSC_WAVE_Y + 2)
#define UI_CLIP_Y1      (UI_OSC_WAVE_Y + UI_OSC_WAVE_H - 3)

/* ---------------- 数学工具 ---------------- */
static const s32 ui_pow10[10] = {
    1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000
};

static u32 ui_isqrt64(u64 n)
{
    u64 x, y;
    if (n < 2u) {
        return (u32)n;
    }
    x = n;
    y = (x + 1u) >> 1;
    while (y < x) {
        x = y;
        y = (x + n / x) >> 1;
    }
    return (u32)x;
}

static s32 ui_snap125_up(s32 v)
{
    static const s32 f[3] = { 1, 2, 5 };
    int k, j;
    if (v <= 1) {
        return 1;
    }
    for (k = 0; k < 10; k++) {
        for (j = 0; j < 3; j++) {
            s32 cand = f[j] * ui_pow10[k];
            if (cand >= v) {
                return cand;
            }
        }
    }
    return 1000000000;
}

static s32 ui_snap125_down(s32 v)
{
    static const s32 f[3] = { 5, 2, 1 };
    int k, j;
    if (v <= 1) {
        return 1;
    }
    for (k = 9; k >= 0; k--) {
        for (j = 0; j < 3; j++) {
            s32 cand = f[j] * ui_pow10[k];
            if (cand < v) {
                return cand;
            }
        }
    }
    return 1;
}

static s32 ui_clamp(s32 v, s32 lo, s32 hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static s32 ui_code_to_mv(const ui_osc_t *s, s32 code)
{
    s64 d;
    if (s->adc_bits <= 0 || s->adc_bits > 30) {
        return 0;
    }
    d = (s64)1 << s->adc_bits;
    return (s32)(((s64)(code - s->adc_zero) * (s64)s->adc_mv_fs) / d);
}

static s32 ui_span_to_mv(const ui_osc_t *s, s32 span)
{
    s64 d;
    if (s->adc_bits <= 0 || s->adc_bits > 30) {
        return 0;
    }
    d = (s64)1 << s->adc_bits;
    return (s32)(((s64)span * (s64)s->adc_mv_fs) / d);
}

/* ---------------- 数值格式化 ---------------- */
static int ui_u2s(char *b, u32 v)
{
    char t[12];
    int  n = 0, i;
    if (v == 0u) {
        b[0] = '0'; b[1] = '\0';
        return 1;
    }
    while (v > 0u && n < 11) {
        t[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    for (i = 0; i < n; i++) {
        b[i] = t[n - 1 - i];
    }
    b[n] = '\0';
    return n;
}

/* 按指定小数位(0~3)组装 "整数.小数 单位" */
static void ui_fmt_d(char *b, u32 whole, u32 frac, int digits, const char *unit)
{
    int n = ui_u2s(b, whole);
    if (digits >= 3) {
        b[n++] = '.';
        b[n++] = (char)('0' + ((frac / 100u) % 10u));
        b[n++] = (char)('0' + ((frac / 10u) % 10u));
        b[n++] = (char)('0' + (frac % 10u));
    } else if (digits == 2) {
        b[n++] = '.';
        b[n++] = (char)('0' + ((frac / 10u) % 10u));
        b[n++] = (char)('0' + (frac % 10u));
    } else if (digits == 1) {
        b[n++] = '.';
        b[n++] = (char)('0' + (frac % 10u));
    }
    while (*unit != '\0') {
        b[n++] = *unit++;
    }
    b[n] = '\0';
}

/* 小数位数随整数部分自适应, 保证总宽 <= 8 字符(16px 字宽下 128px) */
static int ui_dec_digits(u32 whole)
{
    /* ★ 档位放宽一档: 原来 whole>=1000 就 0 位小数, 于是 >=1MHz 只能显示整数 kHz。
       现在 <10000 都还能有 0.1kHz 分辨率, 字符串最长仍 <=9 字符(9*16=144px)。 */
    /* ★ 每档都保证 "整数部分+小数点+小数位+单位" <= 8 个字符:
       底部数值区一个半栏只有 200px(12.5 字符), 名称占 3 字 + 间隔, 数值只能放 8 字符,
       放不下就会顶到右边那一对去。 */
    if (whole < 10u) {
        return 3;
    }
    if (whole < 100u) {
        return 2;
    }
    if (whole < 1000u) {
        return 1;
    }
    return 0;
}

/* 10^k (k = 0..9): 给"按显示位数四舍五入"用 */
static u32 ui_pow10u(int k)
{
    u32 v = 1u;

    while (k > 0) {
        v *= 10u;
        k--;
    }
    return v;
}

/* 把 a 四舍五入到 step 的整数倍(显示位数不足时不要截断) */
static u64 ui_round_to(u64 a, u32 step)
{
    return (((a + (u64)(step / 2u)) / (u64)step) * (u64)step);
}

static void ui_fmt_mv(char *b, s32 mv)
{
    u32 a, w, frac, step;
    int off = 0;
    int dg;

    if (mv < 0) {
        b[0] = '-';
        off = 1;
        a = (u32)(-(mv + 1)) + 1u;
    } else {
        a = (u32)mv;
    }
    if (a >= 1000u) {
        /* ★ 显示位数不足时四舍五入(原来是直接截断), 且 frac 要缩放到当前小数位:
           ui_fmt_d() 要的是"已缩放到 dg 位"的值, 传 3 位会在 dg=2/1 时印错数字 */
        w    = a / 1000u;                       /* V */
        dg   = ui_dec_digits(w);
        step = ui_pow10u(3 - dg);               /* 显示分辨率(毫伏) */
        a    = (u32)ui_round_to((u64)a, step);
        w    = a / 1000u;
        frac = (a % 1000u) / step;
        ui_fmt_d(b + off, w, frac, dg, "V");
    } else {
        ui_fmt_d(b + off, a, 0u, 0, "mV");
    }
}

static void ui_fmt_ns(char *b, s32 ns)
{
    u32 a = (ns < 0) ? ((u32)(-(ns + 1)) + 1u) : (u32)ns;
    u32 w, frac, step;
    int dg;

    /* ★ 同 ui_fmt_freq()/ui_fmt_mv(): 先按显示分辨率四舍五入, 再缩放到当前小数位 */
    if (a >= 1000000000u) {
        w    = a / 1000000000u;                 /* s */
        dg   = ui_dec_digits(w);
        step = ui_pow10u(9 - dg);
        a    = (u32)ui_round_to((u64)a, step);
        w    = a / 1000000000u;
        frac = (a % 1000000000u) / step;
        ui_fmt_d(b, w, frac, dg, "s");
    } else if (a >= 1000000u) {
        w    = a / 1000000u;                    /* ms */
        dg   = ui_dec_digits(w);
        step = ui_pow10u(6 - dg);
        a    = (u32)ui_round_to((u64)a, step);
        w    = a / 1000000u;
        frac = (a % 1000000u) / step;
        ui_fmt_d(b, w, frac, dg, "ms");
    } else if (a >= 1000u) {
        w    = a / 1000u;                       /* us */
        dg   = ui_dec_digits(w);
        step = ui_pow10u(3 - dg);
        a    = (u32)ui_round_to((u64)a, step);
        w    = a / 1000u;
        frac = (a % 1000u) / step;
        ui_fmt_d(b, w, frac, dg, "us");
    } else {
        ui_fmt_d(b, a, 0u, 0, "ns");
    }
}

/* 采样率: Hz -> "8.192MHz" */
static void ui_fmt_fs(char *b, s32 hz)
{
    u32 a = (hz < 0) ? ((u32)(-(hz + 1)) + 1u) : (u32)hz;
    u32 w;

    if (a == 0u) {
        b[0] = '-'; b[1] = '-'; b[2] = '-'; b[3] = '\0';
        return;
    }
    if (a >= 1000000u) {
        w = a / 1000000u;
        ui_fmt_d(b, w, (a % 1000000u) / 1000u, 3, "MHz");
    } else if (a >= 1000u) {
        w = a / 1000u;
        ui_fmt_d(b, w, a % 1000u, 3, "kHz");
    } else {
        ui_fmt_d(b, a, 0u, 0, "Hz");
    }
}

static void ui_fmt_freq(char *b, s64 mhz)
{
    u64 a = (mhz < 0) ? ((u64)(-(mhz + 1)) + 1u) : (u64)mhz;   /* ★ 64 位取绝对值 */
    u32 w, frac;

    if (a == 0u) {
        b[0] = '-'; b[1] = '-'; b[2] = '-'; b[3] = '\0';
        return;
    }
    /* ★ 显示位数不够时必须"四舍五入", 不能截断:
       1.234567kHz 只显示 3 位小数 -> 1.235kHz (原来印 1.234);
       10.099999kHz 只显示 2 位小数 -> 10.10kHz (原来印 10.99)。
       做法: 先把毫赫兹值按"显示分辨率"四舍五入(允许向上进位), 再拆整数/小数;
       frac 仍按 ui_fmt_d() 的约定给"已缩放到当前小数位"的值。 */
    if (a >= 1000000u) {
        int dg;
        u32 step;

        w    = (u32)(a / 1000000u);                   /* kHz */
        dg   = ui_dec_digits(w);
        step = ui_pow10u(6 - dg);                     /* 显示分辨率(毫赫兹) */
        a    = ui_round_to(a, step);                  /* ★ 四舍五入(含进位) */
        w    = (u32)(a / 1000000u);
        frac = (u32)((a % 1000000u) / step);          /* 当前小数位上的数字 */
        ui_fmt_d(b, w, frac, dg, "kHz");
    } else {
        int dg;
        u32 step;

        w    = (u32)(a / 1000u);                      /* Hz */
        dg   = ui_dec_digits(w);
        step = ui_pow10u(3 - dg);
        a    = ui_round_to(a, step);                  /* ★ 四舍五入(含进位) */
        w    = (u32)(a / 1000u);
        frac = (u32)((a % 1000u) / step);
        ui_fmt_d(b, w, frac, dg, "Hz");
    }
}

static void ui_append_div(char *b)
{
    int n = (int)strlen(b);
    b[n] = '/'; b[n + 1] = 'd'; b[n + 2] = 'i'; b[n + 3] = 'v'; b[n + 4] = '\0';
}

/* ---------------- 中文点阵渲染 ---------------- */
static const u8 *ui_cn_lookup16(u32 code)
{
    u32 i;
    for (i = 0; i < UI_CN_FONT16_COUNT; i++) {
        if (ui_cn_font16[i].code == code) {
            return ui_cn_font16[i].data;
        }
    }
    return 0;
}

static const u8 *ui_cn_lookup32(u32 code)
{
    u32 i;
    for (i = 0; i < UI_CN_FONT32_COUNT; i++) {
        if (ui_cn_font32[i].code == code) {
            return ui_cn_font32[i].data;
        }
    }
    return 0;
}

/* ASCII(0x20~0x7E) SimSun 字模: 码值连续, 直接下标 */
static const ui_ascii_glyph_t *ui_ascii_lookup(u32 code)
{
    if (code < 0x20u || code > 0x7Eu) {
        return 0;
    }
    if ((code - 0x20u) >= (u32)UI_ASCII_FONT_COUNT) {
        return 0;
    }
    return &ui_ascii_font[code - 0x20u];
}

/* 画 size x size 点阵, scale 放大 */
static void ui_glyph(u32 fb, int x, int y, const u8 *d, int size, u16 color,
                     int scale)
{
    int r, c, sx, sy;
    int step = size / 8;
    for (r = 0; r < size; r++) {
        for (c = 0; c < size; c++) {
            u8 bit = (u8)((d[r * step + (c >> 3)] >> (7 - (c & 7))) & 1u);
            if (bit != 0u) {
                for (sy = 0; sy < scale; sy++) {
                    for (sx = 0; sx < scale; sx++) {
                        lcd_pixel(fb, x + c * scale + sx, y + r * scale + sy,
                                  color);
                    }
                }
            }
        }
    }
}

/* UTF-8 解码, 返回消耗字节数 */
static int ui_utf8_next(const char **pp, u32 *code)
{
    const u8 *p = (const u8 *)*pp;
    u32 c = p[0];
    if (c == 0u) {
        return 0;
    }
    if (c < 0x80u) {
        *pp += 1; *code = c;
        return 1;
    }
    if (((c & 0xE0u) == 0xC0u) && ((p[1] & 0xC0u) == 0x80u)) {
        *code = ((c & 0x1Fu) << 6) | (p[1] & 0x3Fu);
        *pp += 2;
        return 2;
    }
    if (((c & 0xF0u) == 0xE0u) && ((p[1] & 0xC0u) == 0x80u) &&
        ((p[2] & 0xC0u) == 0x80u)) {
        *code = ((c & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu);
        *pp += 3;
        return 3;
    }
    *pp += 1; *code = (u32)'?';
    return 1;
}

/* 混合字符串: ASCII 用 8x8 字库按 2*scale 放大, 汉字用 16x16/32x32 点阵 */
static int ui_text(u32 fb, int x, int y, const char *s, u16 color, int scale)
{
    const char *p = s;
    while (*p != '\0') {
        u32 code = 0;
        if (*p == '\r' || *p == '\n') {
            p++;
            continue;
        }
        if (ui_utf8_next(&p, &code) <= 0) {
            break;
        }
        if (code < 0x80u) {
            const ui_ascii_glyph_t *ag = ui_ascii_lookup(code);
            if (ag != 0) {                    /* ASCII 也用 SimSun 点阵 */
                ui_glyph(fb, x, y, ag->data, 16, color, scale);
                x += (int)ag->w * scale;
            } else {
                x += lcd_char(fb, x, y, (char)code, color, (u8)(2 * scale));
            }
        } else {
            const u8 *g = 0;
            if (scale >= UI_TITLE_SCALE) {
                g = ui_cn_lookup32(code);
                if (g != 0) {
                    ui_glyph(fb, x, y, g, 32, color, 1);
                }
            }
            if (g == 0) {
                g = ui_cn_lookup16(code);
                if (g != 0) {
                    ui_glyph(fb, x, y, g, 16, color, scale);
                }
            }
            if (g == 0) {
                lcd_rect(fb, x + 2, y + 2, 12 * scale, 12 * scale, color);
            }
            x += 16 * scale;
        }
    }
    return x;
}

/* 计算字符串显示宽度(像素) */
static u32 ui_text_w(const char *s, int scale)
{
    const char *p = s;
    u32 w = 0, code = 0;
    while (*p != '\0') {
        if (*p == '\r' || *p == '\n') {
            p++;
            continue;
        }
        if (ui_utf8_next(&p, &code) <= 0) {
            break;
        }
        if (code < 0x80u) {
            const ui_ascii_glyph_t *ag = ui_ascii_lookup(code);
            w += (ag != 0) ? ((u32)ag->w * (u32)scale) : (u32)(16 * scale);
        } else {
            w += (u32)(16 * scale);
        }
    }
    return w;
}

/* ---------------- 绘图小工具 ---------------- */
static void ui_dashed_hline(u32 fb, int x0, int x1, int y, u16 color)
{
    int x;
    for (x = x0; x <= x1; x++) {
        if (((x - x0) % 10) < 6) {
            lcd_pixel(fb, x, y, color);
        }
    }
}

/* 圆角药丸(运行/停止状态) */
static void ui_pill(u32 fb, int x, int y, int w, int h, u16 color)
{
    int r = h / 2;
    lcd_fill_circle(fb, x + r, y + r, r, color);
    lcd_fill_circle(fb, x + w - r, y + r, r, color);
    lcd_fill_rect(fb, x + r, y, w - 2 * r, h, color);
}

/* 0V 基准标记: 菱形描边(通道色)。
   原来是 12x3 的实心横条, 和波形同色、又在同一高度, 很容易被看成一截"错开/断掉"的波形。
   菱形描边的像素集合就是 {|dx|+|dy| == 5}, 这里用这个闭式表示,
   为的是"原地刷新"能逐列精确地擦掉/重画它 */
static int ui_zero_rows(int x, int cx, int cy, int *pa, int *pb)
{
    int dx = x - cx;
    int r;

    if (dx < 0) {
        dx = -dx;
    }
    if (dx > 5) {
        return 0;
    }
    r = 5 - dx;
    *pa = cy - r;
    *pb = cy + r;
    return 1;
}

static void ui_zero_mark(u32 fb, int cx, int cy, u16 color)
{
    int dx;

    for (dx = -5; dx <= 5; dx++) {
        int a, b;
        if (ui_zero_rows(cx + dx, cx, cy, &a, &b) == 0) {
            continue;
        }
        lcd_pixel(fb, cx + dx, a, color);
        if (b != a) {
            lcd_pixel(fb, cx + dx, b, color);
        }
    }
}

static int ui_y_of_code(const ui_osc_ch_t *c, s32 code)
{
    return UI_WAVE_CY - (int)(((s64)(code - c->center) * UI_OSC_DIV_H) / c->vdiv);
}

/* ---------------- 背景层(网格 / 触发电平线 / 0V 标记)的位置 ----------------
   单缓冲"原地刷新"必须知道每个像素在"没有波形"时该是什么颜色, 才能把旧波形擦掉
   又不破坏网格。触发线和 0V 标记都用闭式几何描述, 于是逐列可以精确地擦/画。 */
static s16 ui_cur_trig_y;              /* 本帧触发线 y / 是否显示 */
static u8  ui_cur_trig_on;
static u16 ui_cur_trig_color;          /* 触发源通道颜色 */
static s16 ui_cur_zero_y[2];           /* 本帧 0V 标记 */
static u8  ui_cur_zero_on[2];
static u16 ui_cur_zero_color[2];

static int ui_zero_x_anchor(int ch)
{
    /* 位置: CH1 靠左, CH2 靠右; 离边缘 20px, 避开左边缘的触发三角标记 */
    return (ch == 0) ? (UI_OSC_WAVE_X + 20)
                     : (UI_OSC_WAVE_X + UI_OSC_WAVE_W - 20);
}

/* 触发电平虚线: 该列有没有虚线段(与 ui_dashed_hline 的 6/10 占空一致) */
static int ui_trig_dash_on(int x)
{
    return ((x >= (UI_CLIP_X0 + 8)) && (x <= UI_CLIP_X1) &&
            (((x - (UI_CLIP_X0 + 8)) % 10) < 6)) ? 1 : 0;
}

/* 左边缘三角标记: 返回该列斜边的高度偏移(0..6); -1 = 不在三角上, -2 = 左边那条竖线 */
static int ui_trig_tri_dy(int x)
{
    if ((x < UI_CLIP_X0) || (x > (UI_CLIP_X0 + 8))) {
        return -1;
    }
    if (x == UI_CLIP_X0) {
        return -2;
    }
    return (((x - UI_CLIP_X0) * 6) + 4) / 8;
}

/* 三角标记在列 x 上的像素(a=上斜边, b=下斜边); 返回 0 = 该列不在三角上 */
static int ui_trig_rows(int x, int ty, int *pa, int *pb)
{
    int dy = ui_trig_tri_dy(x);

    if (dy == -1) {
        return 0;
    }
    if (dy == -2) {                     /* 左边那条竖线: 整段 */
        *pa = ty - 6;
        *pb = ty + 6;
        return 1;
    }
    *pa = ty - 6 + dy;
    *pb = ty + 6 - dy;
    return 1;
}

/* 三角标记 = 左边竖线 + 上下两条斜边(逐列描边, 和 ui_bg_at 的判定完全同一套几何) */
static void ui_trig_triangle(u32 fb, int ty, u16 color)
{
    int x;

    for (x = UI_CLIP_X0; x <= (UI_CLIP_X0 + 8); x++) {
        int a, b;
        if (ui_trig_rows(x, ty, &a, &b) == 0) {
            continue;
        }
        if (ui_trig_tri_dy(x) == -2) {
            lcd_vline(fb, x, a, b, color);          /* 竖边: 整段 */
            continue;
        }
        lcd_pixel(fb, x, a, color);                 /* 上斜边 */
        if (b != a) {
            lcd_pixel(fb, x, b, color);             /* 下斜边 */
        }
    }
}

/* 该列是不是网格竖线(波形区左右边框不算) */
static int ui_is_vgrid(int x)
{
    return (((x % UI_OSC_DIV_W) == 0) && (x > UI_OSC_WAVE_X) &&
            (x < (UI_OSC_WAVE_X + UI_OSC_WAVE_W))) ? 1 : 0;
}

/* 计算本帧触发线 / 0V 标记的位置(只算不画) */
static void ui_bg_calc(const ui_osc_t *s)
{
    const ui_osc_ch_t *tc = &s->ch[s->trig_src & 1u];
    int ch, y;

    ui_cur_trig_on = 0;
    ui_cur_trig_color = tc->color;
    if ((tc->vdiv > 0) && (tc->on != 0u)) {
        y = ui_y_of_code(tc, s->trig_level);
        if ((y >= UI_CLIP_Y0) && (y <= UI_CLIP_Y1)) {
            ui_cur_trig_y = (s16)y;
            ui_cur_trig_on = 1;
        }
    }
    for (ch = 0; ch < 2; ch++) {
        const ui_osc_ch_t *c = &s->ch[ch];
        ui_cur_zero_on[ch] = 0;
        ui_cur_zero_color[ch] = c->color;
        if ((c->vdiv > 0) && (c->on != 0u)) {
            y = ui_y_of_code(c, s->adc_zero);
            if ((y >= UI_CLIP_Y0) && (y <= UI_CLIP_Y1)) {
                ui_cur_zero_y[ch] = (s16)y;
                ui_cur_zero_on[ch] = 1;
            }
        }
    }
}

/* 波形区某像素"没有波形时"的颜色: 0V 标记 > 触发线 > 网格(横线优先) > 底色。
   顺序与整片重画时的画法一致(网格 → 触发线 → 0V 标记), 所以两种刷新结果完全相同 */
static u16 ui_bg_at(int x, int y)
{
    int iy = y - UI_OSC_WAVE_Y;
    int ch;

    /* 波形区左右边框(白色), 整列都是边框色 */
    if ((x == UI_OSC_WAVE_X) || (x == (UI_OSC_WAVE_X + UI_OSC_WAVE_W - 1))) {
        return UI_OSC_COLOR_FRAME;
    }
    for (ch = 0; ch < 2; ch++) {
        int a, b;
        if ((ui_cur_zero_on[ch] != 0u) &&
            (ui_zero_rows(x, ui_zero_x_anchor(ch), (int)ui_cur_zero_y[ch], &a, &b) != 0) &&
            ((y == a) || (y == b))) {
            return ui_cur_zero_color[ch];
        }
    }
    if (ui_cur_trig_on != 0u) {
        int a, b;
        if (ui_trig_rows(x, (int)ui_cur_trig_y, &a, &b) != 0) {
            if ((ui_trig_tri_dy(x) == -2) && (y >= a) && (y <= b)) {
                return ui_cur_trig_color;           /* 三角左边那条竖线: 整段 */
            }
            if ((y == a) || (y == b)) {
                return ui_cur_trig_color;           /* 三角的上下斜边 */
            }
        }
        if ((y == (int)ui_cur_trig_y) && (ui_trig_dash_on(x) != 0)) {
            return UI_OSC_COLOR_DIM;
        }
    }
    if ((iy % UI_OSC_DIV_H) == 0) {
        return (iy == (4 * UI_OSC_DIV_H)) ? UI_OSC_COLOR_MID : UI_OSC_COLOR_GRID;
    }
    if (ui_is_vgrid(x) != 0) {
        return (x == (UI_OSC_WAVE_X + 5 * UI_OSC_DIV_W)) ? UI_OSC_COLOR_MID
                                                         : UI_OSC_COLOR_GRID;
    }
    return UI_OSC_COLOR_BG;
}

/* 把一列的一段区间补成"没有波形"的样子(网格/触发线/0V 标记都保留) */
static void ui_bg_fill_col(u32 fb, int x, int y0, int y1)
{
    int y;

    if (y0 < UI_CLIP_Y0) {
        y0 = UI_CLIP_Y0;
    }
    if (y1 > UI_CLIP_Y1) {
        y1 = UI_CLIP_Y1;
    }
    for (y = y0; y <= y1; y++) {
        lcd_pixel(fb, x, y, ui_bg_at(x, y));
    }
}

/* ---------------- 波形区 ---------------- */
static void ui_draw_grid(u32 fb)
{
    int i;
    int gy1 = UI_OSC_WAVE_Y + UI_OSC_WAVE_H - 1;   /* 波形区最后一行(335) */

    for (i = 0; i <= 10; i++) {
        int x = UI_OSC_WAVE_X + i * UI_OSC_DIV_W;
        lcd_vline(fb, x, UI_OSC_WAVE_Y, gy1,
                  (i == 5) ? UI_OSC_COLOR_MID : UI_OSC_COLOR_GRID);
    }
    for (i = 0; i <= 8; i++) {
        int y = UI_OSC_WAVE_Y + i * UI_OSC_DIV_H;
        if (y > gy1) {
            y = gy1;          /* i=8 算出来是 336, 会压到测量区顶行 */
        }
        lcd_hline(fb, UI_OSC_WAVE_X, UI_OSC_WAVE_X + UI_OSC_WAVE_W, y,
                  (i == 4) ? UI_OSC_COLOR_MID : UI_OSC_COLOR_GRID);
    }
    lcd_rect(fb, UI_OSC_WAVE_X, UI_OSC_WAVE_Y, UI_OSC_WAVE_W, UI_OSC_WAVE_H,
             UI_OSC_COLOR_FRAME);
}

/* 单通道最大样本数(与采样缓冲一致) */
#define UI_OSC_SNAP_MAX  16384     /* 不小于应用侧的单通道采样点数(本工程 16384) */

/* 强制内联: 每帧要过 8k 个样本, -O0 下按函数调用展开会很慢 */
#if defined(__GNUC__)
#define UI_MED3_INLINE  static inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define UI_MED3_INLINE  static __inline
#else
#define UI_MED3_INLINE  static
#endif

/* 3 点中值滤波: 去掉 ADC 偶发错码(单点尖峰)。
   中值只压单点冲激, 不像平均那样把陡边变缓, 所以测量值/触发点既稳又不失真。 */
UI_MED3_INLINE s32 ui_med3(const s16 *d, u32 n, u32 i)
{
    s32 a = d[(i > 0u) ? (i - 1u) : 0u];
    s32 b = d[i];
    s32 c = d[((i + 1u) < n) ? (i + 1u) : (n - 1u)];

    if (a > b) { s32 t = a; a = b; b = t; }
    if (b > c) { s32 t = b; b = c; c = t; }
    if (a > b) { b = a; }
    return b;
}

/* 本帧中值后的样本: 测量 / 找触发 / 画波形都读它。
   每帧每通道只滤一遍, 而不是每个分析循环各滤一遍(否则一个点要被滤 4 次) */
static s16 ui_filt[2][UI_OSC_SNAP_MAX];
static const s16 *ui_filt_ptr[2];

/* ★ 是否跳过 3 点中值滤波。
   中值把"峰/谷那一拍"换成相邻的较低(高)值, 削掉的深度 = 1 - cos(2π/spp)
   (spp = 每周期样点数):  2MHz@64MSPS(32) 1.9% 看不出; 5MHz(12.8) 11.8%;
   6MHz(10.7) 16.9%; 10MHz(6.4) 37%。而且"削多少"随采样相位漂 ->
   屏幕上每个周期的峰/谷都凹陷并来回跳, 频率越高越深。
   中值本意是滤单点坏码, 那种场合(低频、样点密)继续保留; 样点稀了就跳过,
   宁可放过单点坏码, 也不能削掉所有峰谷。 */
#define UI_OSC_MED3_MIN_SPP  32.0

static int ui_med3_skip(const ui_osc_t *s, int ch)
{
    double spp;

    if ((s->sample_rate <= 0) || (s->meas[ch & 1].freq_mhz <= 0)) {
        return 0;                                  /* 还没测到频率: 照旧滤波 */
    }
    spp = (double)s->sample_rate / ((double)s->meas[ch & 1].freq_mhz / 1000.0);
    return (spp < UI_OSC_MED3_MIN_SPP) ? 1 : 0;
}

static void ui_filter_build(const ui_osc_t *s, int ch)
{
    const ui_osc_ch_t *c = &s->ch[ch & 1];
    u32 n = c->len;
    u32 i;
    int skip;

    ui_filt_ptr[ch & 1] = 0;
    if ((c->data == 0) || (n == 0u)) {
        return;
    }
    if (n > (u32)UI_OSC_SNAP_MAX) {
        n = (u32)UI_OSC_SNAP_MAX;
    }
    /* ★ 判据只跟"采样率 / 测到的频率"有关, 整帧不变 —— 提到循环外只算一次,
       省掉每通道 16384 次 double 除法+比较(16384 点时约 0.5~0.7ms/通道);
       判断结果与逐点计算完全相同, 输出逐像素一致。 */
    skip = ui_med3_skip(s, ch);
    for (i = 0; i < n; i++) {
        ui_filt[ch & 1][i] = (skip != 0) ? c->data[i]      /* ★ 样点稀: 不削峰 */
                                         : (s16)ui_med3(c->data, n, i);
    }
    ui_filt_ptr[ch & 1] = ui_filt[ch & 1];
}

/* ★ 波形加粗: 1 = 3 像素宽(默认), 0 = 原来的 1 像素。
   在 ui_trace_scan() 末尾把每列的纵向范围"左右各扩一列",
   这样画出来是 3 像素宽, 而且"擦旧迹/整片重画/逐像素对比"用的都是同一份加粗记录,
   不会留下残影。 */
#define UI_OSC_TRACE_BOLD  1      /* 1 = 上下左右各扩 1 像素(3x3, 默认); 0 = 1 像素细线 */

/* ---------------- 波形重建: 加窗 sinc 插值(全频段, 每个像素都插满) ----------------
   原来每列取"该列样点的 min/max"画成实心竖条: 高频时一列覆盖很多样点, 竖条很高,
   条高随触发相位抖动, 条与条之间就露出背景(看着有黑边、也不稳)。
   现在在"分数样点位置"上做加窗 sinc 重建, 每一个像素列都取点, 画出连续曲线:
     核   : sinc(x) = sin(pi*x)/(pi*x), 纯 sinc 不加窗
     参数 : 只取 |x| <= SINC_HALF(=4) 的抽头(共 9 个), 无相位表
     每列 : 在本像素覆盖的样点区间内"均布" SINC_SUB 个重建点, 取它们的 min/max
            放大时这些点挤在像素内 -> 细而连续的曲线;
            缩小时它们摊满整列 -> 得到重建波形的峰包络(不假频, 也不撑成实心柱)
     点数 : 800 列 x SINC_SUB = 每通道每帧的重建点总数, SINC_SUB 越大越"满"
   记录格式仍是每列 [top, bot], 擦除/整片重画/加粗全都不用改;
   列与列之间由"并上上一列的 y"那条竖线补齐, 所以屏幕上是连着的。
   UI_OSC_SINC = 0 可退回原来的 min/max 包络(A/B 对比用)。 */
#ifndef UI_OSC_SINC
#define UI_OSC_SINC      1
#endif
#define SINC_HALF        16          /* ★ 截断半径: 只累加 |pos-i| <= 8 的样点(共 17 个) */
#define SINC_SUB         4          /* 每个像素列的最少重建点数 */
#define SINC_SUB_MAX     32         /* 每个像素列的最多重建点数(缩小时按 span 自动加密) */
#define SINC_PI          3.14159265358979323846

#if UI_OSC_SINC
#include <math.h>

/* ★ 插值核: sinc(i) = sin(pi*i)/(pi*i), i=0 时为 1
   (即公式 x(t)=Σ x(nT)·sinc[(pi/T)(t-nT)] 里的 sinc) */

/* ======================================================================
   sinc 插值 —— 按 MATLAB 参考实现的结构重写
   ----------------------------------------------------------------------
   MATLAB:
       Ts = 1/fs;  T = N*Ts;  Ta = T/128;
       tn = n*Ts;                            % 采样时刻
       ta = 0 : Ta : T;                      % 重建时刻
       for k = 1:length(ta)
           temp_sinc = sinc((1/Ts)*((k-1)*Ta - tn));   % matlab 的 sinc 含 PI
           xa(k) = sum(xn .* temp_sinc);
       end
   把时刻都除以 Ts 换成"样点单位"后, 上面就是:
       pos = t/Ts;   u = pos - i;   x(pos) = Σ_i x[i]·sinc(u)
   ★ 与 MATLAB 的唯一差别: MATLAB 对全部 N 点求和(N=32 很便宜); 这里 N=16384,
     全和太慢, 所以只累加 |u| <= SINC_HALF 的样点(截断)。
     截断的单位分解偏差: ±4 -> 0.45%, ±16 -> 0.03%, ±64 -> 0.002%。
   ====================================================================== */

/* MATLAB 的 sinc(u) = sin(pi*u)/(pi*u), u = 0 时为 1
   ★ PI 只在这里出现一次 —— 调用处传【样点单位】的自变量, 外面不要再乘 PI
     (若改成 sin(u)/u 而自变量仍是 pos-i, 整数样点处核值不再是 1, 曲线会整体塌掉) */
static double sinc_kernel(double u)
{
    if (u == 0.0) {
        return 1.0;
    }
    return sin(SINC_PI * u) / (SINC_PI * u);
}

/* 重建一个点: pos = t/Ts(单位=样点, 允许小数) —— 对应 MATLAB 里的 xa(k) */
static s32 sinc_at(const s16 *xn, u32 n, double pos)
{
    int    i, i0, i1;
    double xa = 0.0;                       /* <=> xa(k) */

    if ((xn == 0) || (n == 0u)) {
        return 0;
    }
    /* 截断区间 |pos - i| <= SINC_HALF(MATLAB 是全和, 这里只取核的有效部分) */
    i0 = (int)ceil(pos - (double)SINC_HALF);
    i1 = (int)floor(pos + (double)SINC_HALF);
    if (i0 < 0)          { i0 = 0; }
    if (i1 > (int)n - 1) { i1 = (int)n - 1; }

    for (i = i0; i <= i1; i++) {
        int idx = i;

        /* ★ 边界: 抽头越界时按【镜像】取样(不是补零)。
           补零的话记录首尾各 SINC_HALF 个样点处重建值会往里衰减 ——
           MATLAB 那个 demo 只在记录内部画(碰不到边界), 但示波器显示窗口会越出记录。 */
        if (idx < 0)       { idx = -idx; }
        if (idx >= (int)n) { idx = 2 * (int)n - 2 - idx; }
        if (idx < 0)       { idx = 0; }
        if (idx >= (int)n) { idx = (int)n - 1; }

        xa += (double)xn[idx] * sinc_kernel(pos - (double)i);   /* <=> xa += xn(i)*temp_sinc(i) */
    }
    /* 夹到 s16 码值范围(Gibbs 过冲可能超出) */
    if (xa < -32768.0) { xa = -32768.0; }
    if (xa >  32767.0) { xa =  32767.0; }
    return (s32)(xa + ((xa >= 0.0) ? 0.5 : -0.5));
}
#endif  /* UI_OSC_SINC */

/* ---------------- 亚样点触发: 求过零点的小数位置 ----------------
   触发搜索只能给出"第一个越过触发电平的整数样点下标" i, 真正的过零点在 d[i-1] 与 d[i]
   之间。这个小数位置每帧都在 [0,1) 里漂(ADC 采样时钟与被测信号不同步), 不补偿的话波形
   会被量化成 i 或 i-1 -> 整屏左右抖 1 个样点 = 80/每格样点数 个像素
   (62MSPS 下 1MHz 抖 6px、5MHz 抖 32px、10MHz 抖 64px, 频率越高抖得越远)。
   ① 先在触发沿两侧样点之间线性内插到触发电平(升/降沿同一个公式);
   ② 触发点离记录开头足够远时, 再用带限重建(sinc)在 ±1/4 样点的小窗口里修一次,
      把残差再降一个量级(实测 10MHz 时 2.2px -> 0.18px)。
      离得太近不做: sinc_at 在索引 0 附近走的是镜像分支, 那里的重建值不准。
   ★ 这里只改"锚点的小数位置", 不动"选中哪个过零点"(搜索起点/armed 一律照旧),
     否则整周期地改选相邻过零点, 屏幕上会整段乱跳。
   返回相对 trig_pos(=i) 的偏移, 取值 (-1, 0]。 */
#define UI_TRIG_REFINE_MIN  16     /* 触发点离记录开头小于这么多点就不做带限修正; 必须 >= SINC_HALF+2 */

static double ui_trig_cross_off(const s16 *d, u32 n, u32 i, s32 level)
{
    double y0, y1, t;

    if (i == 0u) {
        return 0.0;
    }
    y0 = (double)d[i - 1u];
    y1 = (double)d[i];
    if (y1 == y0) {
        return 0.0;                    /* 两点同电平: 内插不出来, 退回整数触发 */
    }
    t = (double)i - 1.0 + ((double)level - y0) / (y1 - y0);
    if (t < (double)i - 1.0) { t = (double)i - 1.0; }
    if (t > (double)i)       { t = (double)i;       }
#if UI_OSC_SINC
    /* 亚样点触发精修: ① 先在两侧样点间直线内插到触发电平(上面的 t);
       ② 触发点离记录开头足够远时, 再用带限重建(sinc)在 ±1/4 样点的小窗口里修一次。
       离得太近不做: sinc_at 在记录开头缺抽头(镜像区), 那里的重建值不准。 */
    if (i >= (u32)UI_TRIG_REFINE_MIN) {
        double a = t - 0.25, b = t + 0.25;
        double va = (double)sinc_at(d, n, a);
        double vb = (double)sinc_at(d, n, b);

        if (vb != va) {
            double t2 = a + ((double)level - va) * (b - a) / (vb - va);

            if ((t2 > (double)i - 1.0) && (t2 < (double)i)) {
                t = t2;
            }
        }
    }
#else
    (void)n;
#endif
    return t - (double)i;
}
/* 逐列纵向范围: 供"原地刷新"比较新旧两帧用 */
typedef struct {
    s16 top;
    s16 bot;
    u8  on;
} ui_trace_col_t;

static ui_trace_col_t ui_prev[2][UI_OSC_WAVE_W];   /* 此刻屏幕上的轨迹 */
static ui_trace_col_t ui_next[2][UI_OSC_WAVE_W];   /* 本帧算出来的轨迹 */
static u8 ui_prev_valid = 0;

/* ★ 切界面/贴底板后调用: 让下一帧 ui_osc_draw_live() 整块重画。
   否则它会拿"切换前那套旧迹坐标"去擦像素(擦错地方) -> 从别的界面回到示波器时花屏一帧。 */
void ui_osc_invalidate(void)
{
    ui_prev_valid = 0u;
}

/* 算出某通道本帧每一列的纵向范围(不画屏); 返回 1 表示有内容
   范围 = 本列样本 min~max, 再并上"上一列的 y"(连线部分)。
   关键: 连线也在本列内画成竖线段, 不让它跨到相邻列去 ——
   否则陡峭的跳变沿会被 Bresenham 分摊到两列, 分摊点正好在沿的中间高度,
   跨零线的信号那里就是零线, 看起来就是"过零点处波形错开 1 像素、断一下"。 */
static int ui_trace_scan(const ui_osc_t *s, int ch, ui_trace_col_t *out)
{
    const ui_osc_ch_t *c = &s->ch[ch & 1];
    const s16 *d = ui_filt_ptr[ch & 1];
    s32 spd = ui_osc_samples_per_div(s, ch);
    int x, prev_y = 0, have_prev = 0;
#if UI_OSC_SINC
    double vv[SINC_SUB_MAX];      /* 本列各重建点(供峰值细化用) */
    int    imax = 0, imin = 0;
#endif

    for (x = 0; x < UI_OSC_WAVE_W; x++) {
        out[x].top = 0;
        out[x].bot = 0;
        out[x].on  = 0;
    }
    if ((d == 0) || (c->len == 0u) || (c->vdiv <= 0) || (c->on == 0u)) {
        return 0;
    }

    for (x = 0; x < UI_OSC_WAVE_W; x++) {
        s64 i0 = (s64)s->trig_pos + ((s64)x * spd) / UI_OSC_DIV_W;
        s64 i1 = (s64)s->trig_pos + ((s64)(x + 1) * spd) / UI_OSC_DIV_W;
        s32 lo, hi;
        s64 i;
        int ytop, ybot, t, b;

        if (i0 >= (s64)c->len) {
            break;
        }
        if (i1 <= i0) {
            i1 = i0 + 1;
        }
        if (i1 > (s64)c->len) {
            i1 = (s64)c->len;
        }

#if UI_OSC_SINC
        (void)i;
        {
            /* ★ 全频段插值: 本像素覆盖 span 个样点, 在这段区间里均布 SINC_SUB 个重建点,
               取 min/max 作为本列的 [top,bot]。
               点间距必须按"像素宽度"(spd/80)算, 不能按 (i1-i0) 算 —— 每列样点 < 1 时
               i1-i0 会被夹成 1, 4 个点就会摊到整整一个样点间隔(比像素宽数倍)上,
               重建结果被摊平、还带半个样点的横向偏移。改成恒等于像素宽度后,
               放大缩小都严格落在本像素的时间范围内, 加上列间连线就是插满的连续曲线。 */
            double span = (double)spd / (double)UI_OSC_DIV_W;   /* 每像素多少个样点 */
            int    sub;                                        /* 本像素插几个点(自适应) */
            double dp;
            double p0;
            s32 lo2 = 0, hi2 = 0;
            int s2;

            /* ★ 每像素的插值点数必须跟着"每像素样点数"走, 不能固定在 4 个:
               缩小时一个像素覆盖好几个样点, 4 个点跨的相位角 = 360°·span/(4·spp),
               自动设置时小到看不见, 但缩到整条记录(span≈10)时能到 86° —— 那么在这
               个像素里重建出来的最大值会比真实峰顶低最多 1-cos(43°)=27%, 屏幕上就是
               每个周期的峰/谷都凹一块, 而且随采样相位漂 -> 凹陷+跳动, 频率越高越深。
               这里按"每个样点宽插 4 个点"定, 最少 SINC_SUB、最多 SINC_SUB_MAX,
               把像素插满: dp<=0.32 样点时, 10 样点/周期下峰值误差 <=0.4%。 */
            sub = (int)(span * 4.0) + 1;
            if (sub < SINC_SUB)     { sub = SINC_SUB; }
            if (sub > SINC_SUB_MAX) { sub = SINC_SUB_MAX; }
            dp = span / (double)sub;                           /* 相邻重建点间隔 */
            p0 = (double)s->trig_pos + s->trig_off + (double)x * span + dp * 0.5;

            for (s2 = 0; s2 < sub; s2++) {
                s32 v2 = sinc_at(d, c->len, p0 + dp * (double)s2);

                vv[s2] = (double)v2;                   /* ★ 存下来, 供峰值细化用 */
                if (s2 == 0) {
                    lo2 = hi2 = v2;
                    imax = imin = 0;
                } else {
                    if (v2 < lo2) { lo2 = v2; imin = s2; }
                    if (v2 > hi2) { hi2 = v2; imax = s2; }
                }
            }
            /* ★ 峰值细化: 粗采样只能保证"有点落在真峰附近", 真峰一般在两点之间,
               于是每列的峰/谷会随采样相位摆动(幅度约 (pi*nu*dp)^2/2 —— 低频看不出来,
               高频才跨过一个像素的门槛)。这里用极值点两侧共三点做抛物线拟合,
               直接解出顶点值 => 误差从 O(dp^2) 降到 O(dp^3), 且"只增不减/只减不增",
               保证包络不会因此缩水。 */
            if (sub >= 3) {
                double lv, cv, rv, den, dlt, pk;

                if ((imax > 0) && (imax < (sub - 1))) {
                    lv = vv[imax - 1]; cv = vv[imax]; rv = vv[imax + 1];
                    den = lv - 2.0 * cv + rv;
                    if (den < -1e-9) {                 /* 开口向下: 有极大值 */
                        dlt = 0.5 * (lv - rv) / den;   /* 顶点相对 imax 的偏移(dp 为单位) */
                        if ((dlt > -1.0) && (dlt < 1.0)) {
                            pk = cv - 0.25 * (lv - rv) * dlt;
                            if (pk > (double)hi2) {
                                hi2 = (s32)(pk + ((pk >= 0.0) ? 0.5 : -0.5));
                            }
                        }
                    }
                }
                if ((imin > 0) && (imin < (sub - 1))) {
                    lv = vv[imin - 1]; cv = vv[imin]; rv = vv[imin + 1];
                    den = lv - 2.0 * cv + rv;
                    if (den > 1e-9) {                  /* 开口向上: 有极小值 */
                        dlt = 0.5 * (lv - rv) / den;
                        if ((dlt > -1.0) && (dlt < 1.0)) {
                            pk = cv - 0.25 * (lv - rv) * dlt;
                            if (pk < (double)lo2) {
                                lo2 = (s32)(pk + ((pk >= 0.0) ? 0.5 : -0.5));
                            }
                        }
                    }
                }
            }
            if (s->meas[ch & 1].sq_wave != 0u) {
                /* ★ 方波/脉冲: 不做 sinc 插值, 直接用本列样点的 min/max。
                   理由: sinc 重建对理想阶跃必然过冲(Gibbs), 屏幕上就是"方波峰值上方
                   跳动的竖线"; 而方波本身用 min/max 画就完全正确(平坦段+竖直边沿)。
                   判据 m->sq_wave 由 ui_osc_measure() 统计"样点贴在 min/max 的比例"得出。
                   注意: 只跳过【插值】, 不改任何测量量(正弦等其它波形路径完全不受影响)。 */
                s64 ii;

                lo = hi = d[i0];
                for (ii = i0; ii < i1; ii++) {
                    s32 code = d[ii];

                    if (code < lo) { lo = code; }
                    if (code > hi) { hi = code; }
                }
            } else {
            lo = lo2;
            hi = hi2;
            }
        }
#else
        /* UI_OSC_SINC = 0 的 A/B 对比路径: 每列直接取该列样点的 min/max(缩小时不丢峰,
           读的是中值后的样本, 单点错码不进包络) */
        lo = hi = d[i0];
        for (i = i0; i < i1; i++) {
            s32 code = d[i];

            if (code < lo) {
                lo = code;
            }
            if (code > hi) {
                hi = code;
            }
        }
#endif
        /* 超量程的样本"贴边"显示: 先把 y 夹进裁剪框, 再画、再记录。
           否则 y 落在框外时那一整段就不画了(平顶消失, 只剩跳变竖线);
           夹过之后"绘制值"和"擦除用记录值"完全一致, 原地刷新仍然像素级精确。 */
        ytop = ui_clamp(ui_y_of_code(c, hi), UI_CLIP_Y0, UI_CLIP_Y1);
        ybot = ui_clamp(ui_y_of_code(c, lo), UI_CLIP_Y0, UI_CLIP_Y1);

        t = ytop;
        b = ybot;
        if ((have_prev != 0) && (prev_y < t)) {
            t = prev_y;
        }
        if ((have_prev != 0) && (prev_y > b)) {
            b = prev_y;
        }
        out[x].top = (s16)t;
        out[x].bot = (s16)b;
        out[x].on  = 1;

        prev_y = ybot;
        have_prev = 1;
    }
#if UI_OSC_TRACE_BOLD
    {   /* 左右各扩一列 -> 3 像素宽(用临时表算, 免得沿轨迹连锁扩散) */
        static ui_trace_col_t tmp[UI_OSC_WAVE_W];
        int k, nx, nt, nb;

        for (k = 0; k < UI_OSC_WAVE_W; k++) {
            tmp[k] = out[k];
        }
        for (x = 0; x < UI_OSC_WAVE_W; x++) {
            for (k = -1; k <= 1; k++) {
                nx = x + k;
                if ((nx < 0) || (nx >= UI_OSC_WAVE_W)) {
                    continue;
                }
                if (tmp[nx].on == 0u) {
                    continue;
                }
                /* ★ 上下也各扩 1 像素(3x3 方形膨胀): 不然水平段(余弦峰值那种)
                   只被加宽、没有被加厚, 看起来峰值处还是 1 像素细线 */
                nt = (int)tmp[nx].top - 1;
                nb = (int)tmp[nx].bot + 1;
                if (nt < UI_CLIP_Y0) {
                    nt = UI_CLIP_Y0;
                }
                if (nb > UI_CLIP_Y1) {
                    nb = UI_CLIP_Y1;
                }
                if (out[x].on == 0u) {
                    out[x].top = (s16)nt;
                    out[x].bot = (s16)nb;
                    out[x].on  = 1;
                } else {
                    if (nt < (int)out[x].top) {
                        out[x].top = (s16)nt;
                    }
                    if (nb > (int)out[x].bot) {
                        out[x].bot = (s16)nb;
                    }
                }
            }
        }
    }
#endif
    return have_prev;
}

/* 按范围表把某通道画到屏上 */
static void ui_trace_paint(u32 fb, const ui_osc_t *s, int ch, const ui_trace_col_t *cols)
{
    int x;

    for (x = 0; x < UI_OSC_WAVE_W; x++) {
        if (cols[x].on != 0u) {
            lcd_vline(fb, UI_OSC_WAVE_X + x, cols[x].top, cols[x].bot,
                      s->ch[ch & 1].color);
        }
    }
}





/* 整片重画时的单通道波形: 记录直接写进 ui_prev, 后面的原地刷新就能接着做增量 */
static void ui_draw_trace(u32 fb, const ui_osc_t *s, int ch)
{
    (void)ui_trace_scan(s, ch, ui_prev[ch & 1]);
    ui_trace_paint(fb, s, ch, ui_prev[ch & 1]);
    ui_prev_valid = 1;
}

/* 上一帧的触发电平线 / 0V 标记位置: 供下一帧擦除(单缓冲原地刷新用) */
static s16 ui_prev_trig_y;
static u8  ui_prev_trig_on;
static u16 ui_prev_trig_color;         /* 触发源通道颜色(三角标记用的就是它) */
static s16 ui_prev_zero_y[2];
static u8  ui_prev_zero_on[2];

/* 触发电平虚线 + 左边缘三角标记 */
static void ui_draw_trigger(u32 fb, const ui_osc_t *s)
{
    ui_bg_calc(s);
    ui_prev_trig_on = 0;
    if (ui_cur_trig_on == 0u) {
        return;                     /* 触发源通道被关掉: 触发线/三角一起隐藏 */
    }
    ui_dashed_hline(fb, UI_CLIP_X0 + 8, UI_CLIP_X1, ui_cur_trig_y, UI_OSC_COLOR_DIM);
    ui_trig_triangle(fb, ui_cur_trig_y, ui_cur_trig_color);

    ui_prev_trig_y = ui_cur_trig_y;
    ui_prev_trig_color = ui_cur_trig_color;
    ui_prev_trig_on = 1;
}

/* 0V 基准标记: CH1 在左, CH2 在右 */
static void ui_draw_zero_marks(u32 fb, const ui_osc_t *s)
{
    int ch;

    ui_bg_calc(s);
    for (ch = 0; ch < 2; ch++) {
        ui_prev_zero_on[ch] = 0;
        if (ui_cur_zero_on[ch] == 0u) {
            continue;                   /* 通道关闭时不画 0V 标记 */
        }
        ui_zero_mark(fb, ui_zero_x_anchor(ch), (int)ui_cur_zero_y[ch],
                     ui_cur_zero_color[ch]);

        ui_prev_zero_y[ch] = ui_cur_zero_y[ch];
        ui_prev_zero_on[ch] = 1;
    }
}

/* ---------------- 顶栏 / 信息行 ---------------- */
static void ui_draw_header(u32 fb, const ui_osc_t *s)
{
    int tx;

    lcd_fill_rect(fb, 0, 0, LCD_GFX_WIDTH, UI_OSC_HEADER_H, UI_OSC_COLOR_BAR);
    ui_text(fb, 12, 8, "双通道示波器", UI_OSC_COLOR_TEXT, UI_TITLE_SCALE);

    ui_text(fb, 560, 16, s->trig_found ? "已同步" : "未同步",
            s->trig_found ? UI_OSC_COLOR_OK : UI_OSC_COLOR_BAD, 1);

    /* 黑白主题: 运行=白底黑字, 停止=深灰底白字 */
    ui_pill(fb, 676, 10, 108, 28,
            s->run ? UI_OSC_COLOR_RUN_BG : UI_OSC_COLOR_STOP_BG);
    tx = 676 + (int)((108 - (int)ui_text_w(s->run ? "运行" : "停止", 1)) / 2);
    ui_text(fb, tx, 16, s->run ? "运行" : "停止",
            s->run ? UI_OSC_COLOR_RUN_FG : UI_OSC_COLOR_STOP_FG, 1);

    lcd_hline(fb, 0, LCD_GFX_WIDTH - 1, UI_OSC_HEADER_H - 1,
              UI_OSC_COLOR_FRAME);
}

static void ui_draw_info(u32 fb, const ui_osc_t *s)
{
    char v[24];
    int  x = 12, y = UI_OSC_INFO_Y + 4;

    lcd_fill_rect(fb, 0, UI_OSC_INFO_Y, LCD_GFX_WIDTH, UI_OSC_INFO_H,
                  UI_OSC_COLOR_INFO);

    x = ui_text(fb, x, y, "时基 ", UI_OSC_COLOR_LABEL, 1);
    ui_fmt_ns(v, s->ch[0].time_per_div_ns);
    ui_append_div(v);
    x = ui_text(fb, x, y, "CH1 ", UI_OSC_COLOR_CH1, 1);
    x = ui_text(fb, x, y, v, UI_OSC_COLOR_TEXT, 1);
    ui_fmt_ns(v, s->ch[1].time_per_div_ns);
    ui_append_div(v);
    x = ui_text(fb, x + 8, y, "CH2 ", UI_OSC_COLOR_CH2, 1);
    x = ui_text(fb, x, y, v, UI_OSC_COLOR_TEXT, 1);

    x = ui_text(fb, x + 16, y, "触发 ", UI_OSC_COLOR_LABEL, 1);
    x = ui_text(fb, x, y, (s->trig_src == 0) ? "CH1 " : "CH2 ",
                (s->trig_src == 0) ? UI_OSC_COLOR_CH1 : UI_OSC_COLOR_CH2, 1);
    x = ui_text(fb, x, y,
                (s->trig_edge == UI_OSC_EDGE_RISE) ? "上升沿 " : "下降沿 ",
                UI_OSC_COLOR_TEXT, 1);
    x = ui_text(fb, x, y,
                (s->trig_mode == UI_OSC_MODE_AUTO) ? "自动 " :
                ((s->trig_mode == UI_OSC_MODE_NORMAL) ? "普通 " : "单次 "),
                UI_OSC_COLOR_TEXT, 1);
    x = ui_text(fb, x, y, "电平 ", UI_OSC_COLOR_LABEL, 1);
    ui_fmt_mv(v, ui_code_to_mv(s, s->trig_level));
    x = ui_text(fb, x, y, v, UI_OSC_COLOR_TEXT, 1);

    /* 当前 ADC 采样率(自动变速后由应用调 ui_osc_draw_info 重画本行) */
    x = ui_text(fb, x + 16, y, "fs ", UI_OSC_COLOR_LABEL, 1);
    ui_fmt_fs(v, s->sample_rate);
    (void)ui_text(fb, x, y, v, UI_OSC_COLOR_TEXT, 1);

    lcd_hline(fb, 0, LCD_GFX_WIDTH - 1, UI_OSC_INFO_Y + UI_OSC_INFO_H - 1,
              UI_OSC_COLOR_FRAME);
}

/* ---------------- 测量区(中文) ---------------- */
/* 右对齐文字: x_right 为右边界(不含) */
static void ui_text_r(u32 fb, int x_right, int y, const char *s, u16 color,
                      int scale)
{
    ui_text(fb, x_right - (int)ui_text_w(s, scale), y, s, color, scale);
}

/* 通道标题条: 通道色的深色版做底 + 通道色文字 + 左侧亮色竖条
   (通道关闭时整条变暗, 提示该通道已关; 点这里可以开关通道) */
static void ui_draw_ch_tab(u32 fb, const ui_osc_t *s, int ch)
{
    const ui_osc_ch_t *c = &s->ch[ch & 1];
    static const char ge[] = "格";
    char v[24];
    int  px = (ch == 0) ? 0 : UI_OSC_CH_PANEL_W;
    int  w  = (ch == 0) ? (UI_OSC_CH_PANEL_W - 1) : UI_OSC_CH_PANEL_W;
    int  n, i;
    u16  tbg = c->on ? ((ch == 0) ? UI_OSC_COLOR_CH1_BG : UI_OSC_COLOR_CH2_BG)
                     : UI_OSC_COLOR_TABBG;
    u16  tfg = c->on ? c->color : UI_OSC_COLOR_BAD;

    lcd_fill_rect(fb, px, UI_OSC_BOTTOM_Y, w, UI_OSC_CH_TAB_H, tbg);
    /* 左侧亮色竖条: 与波形同色, 便于对应通道 */
    lcd_fill_rect(fb, px + 6, UI_OSC_BOTTOM_Y + 3, 4, UI_OSC_CH_TAB_H - 6, tfg);
    ui_text(fb, px + 16, UI_OSC_BOTTOM_Y + 2, (ch == 0) ? "CH1" : "CH2",
            tfg, 1);

    /* 右上角: 垂直档位 x.xxV/格(右对齐, 与波形同色) */
    ui_fmt_mv(v, ui_span_to_mv(s, c->vdiv));
    n = (int)strlen(v);
    v[n] = '/';
    for (i = 0; ge[i] != '\0'; i++) {
        v[n + 1 + i] = ge[i];
    }
    v[n + 1 + i] = '\0';
    ui_text_r(fb, px + UI_OSC_CH_PANEL_W - 8, UI_OSC_BOTTOM_Y + 2, v, tfg, 1);
}

/* 两行测量值: 左组左对齐, 右组右对齐到面板右边界(长数值也不会被裁) */
/* 一行两对"标签 数值": 左对左对齐, 右对右对齐(长数值不会被裁) */
/* 底部数值区的列槽: 名称 3 个 16px 汉字 = 48px, 名称与数值之间留 8px。
   数值槽 = 半栏 200px - 48 - 8 = 144px = 9 字符, 所以数值串必须 <=8~9 字符
   (ui_dec_digits 的档位就是按这个预算定的)。 */
#define UI_KV_NAME_W   48
#define UI_KV_GAP       8

static void ui_kv_pair(u32 fb, int px, int y, const char *k1, const char *v1,
                       const char *k2, const char *v2, u16 color)
{
    /* ★ 固定列槽, 不再"跟着数值宽度排":
       - 名称槽固定 3 个汉字宽(UI_KV_NAME_W), 所以 2 字的"频率/周期"和 3 字的其它项
         数值起始位置一致; 同一行两对的名称 x 固定(左栏 px+8, 右栏 px+半栏),
         三行之间也就对齐了;
       - 数值再长再短, 名称都不会动(原来是按"名称+数值"的整体宽度右对齐,
         数值一变宽, 名称就被挤着往左跑);
       - 名称与数值之间留 UI_KV_GAP, 不再挤在一起。 */
    int x1 = px + 8;
    int x2 = px + UI_OSC_CH_PANEL_W / 2;

    ui_text(fb, x1, y, k1, color, 1);
    ui_text(fb, x1 + UI_KV_NAME_W + UI_KV_GAP, y, v1, color, 1);

    ui_text(fb, x2, y, k2, color, 1);
    ui_text(fb, x2 + UI_KV_NAME_W + UI_KV_GAP, y, v2, color, 1);
}

static void ui_draw_ch_values(u32 fb, const ui_osc_t *s, int ch)
{
    const ui_osc_ch_t *c = &s->ch[ch & 1];
    const ui_osc_meas_t *m = &s->meas[ch & 1];
    char v1[24], v2[24];
    int  px = (ch == 0) ? 0 : UI_OSC_CH_PANEL_W;
    int  y = UI_OSC_BOTTOM_Y + UI_OSC_CH_TAB_H + 4;

    /* 清掉三行数值区(只清这块, 刷新快) */
    lcd_fill_rect(fb, px + 1, y - 2, UI_OSC_CH_PANEL_W - 2, 3 * UI_ROW_H,
                  UI_OSC_COLOR_BG);

    /* 通道关闭: 六个测量值全部显示 "--" */
    if (c->on == 0u) {
        ui_kv_pair(fb, px, y, "峰峰值", "--", "最大值", "--", UI_OSC_COLOR_BAD);
        ui_kv_pair(fb, px, y + UI_ROW_H, "最小值", "--", "有效值", "--",
                   UI_OSC_COLOR_BAD);
        ui_kv_pair(fb, px, y + 2 * UI_ROW_H, "频率", "--", "周期", "--",
                   UI_OSC_COLOR_BAD);
        return;
    }

    /* 行1: 峰峰值 | 最大值 */
    ui_fmt_mv(v1, ui_span_to_mv(s, m->vpp));
    ui_fmt_mv(v2, ui_code_to_mv(s, m->vmax));
    ui_kv_pair(fb, px, y, "峰峰值", v1, "最大值", v2, c->color);

    /* 行2: 最小值 | 有效值 */
    y += UI_ROW_H;
    ui_fmt_mv(v1, ui_code_to_mv(s, m->vmin));
    ui_fmt_mv(v2, ui_code_to_mv(s, m->rms));
    ui_kv_pair(fb, px, y, "最小值", v1, "有效值", v2, c->color);

    /* 行3: 频率 | 周期 */
    y += UI_ROW_H;
    ui_fmt_freq(v1, m->freq_mhz);
    if (m->freq_mhz > 0) {
        ui_fmt_ns(v2, (s32)(1000000000000LL / (s64)m->freq_mhz));
    } else {
        v2[0] = '-'; v2[1] = '-'; v2[2] = '-'; v2[3] = '\0';
    }
    ui_kv_pair(fb, px, y, "频率", v1, "周期", v2, c->color);
}

static void ui_draw_ch_panel(u32 fb, const ui_osc_t *s, int ch)
{
    ui_draw_ch_tab(fb, s, ch);
    ui_draw_ch_values(fb, s, ch);
}

static void ui_draw_bottom(u32 fb, const ui_osc_t *s)
{
    lcd_fill_rect(fb, 0, UI_OSC_BOTTOM_Y, LCD_GFX_WIDTH, UI_OSC_BOTTOM_H,
                  UI_OSC_COLOR_PANEL);
    lcd_hline(fb, 0, LCD_GFX_WIDTH - 1, UI_OSC_BOTTOM_Y, UI_OSC_COLOR_FRAME);
    lcd_vline(fb, UI_OSC_CH_PANEL_W - 1, UI_OSC_BOTTOM_Y, LCD_GFX_HEIGHT - 1,
              UI_OSC_COLOR_FRAME);
    ui_draw_ch_panel(fb, s, 0);
    ui_draw_ch_panel(fb, s, 1);
}

/* ---------------- 对外接口 ---------------- */
void ui_osc_default(ui_osc_t *s)
{
    memset(s, 0, sizeof(*s));
    s->sample_rate = 100000;
    s->adc_bits = 12;
    s->adc_mv_fs = 3300;
    s->adc_zero = 2048;
    s->ch[0].time_per_div_ns = 1000000;  /* 每通道各一份水平档位 */
    s->ch[1].time_per_div_ns = 1000000;
    s->ch[0].on = 1;                     /* 默认两个通道都打开 */
    s->ch[1].on = 1;
    s->ch[0].vdiv = 256;
    s->ch[1].vdiv = 256;
    s->ch[0].center = s->adc_zero;
    s->ch[1].center = s->adc_zero;
    s->ch[0].color = UI_OSC_COLOR_CH1;
    s->ch[1].color = UI_OSC_COLOR_CH2;
    s->trig_src = 0;
    s->trig_off = 0.0;
    s->trig_edge = UI_OSC_EDGE_RISE;
    s->trig_mode = UI_OSC_MODE_AUTO;
    s->trig_level = s->adc_zero;
    s->trig_hyst = 0;                /* 0 = 自动迟滞(Vpp/16) */
    s->run = 1;
    s->sel_ch = 0;                   /* 触摸"档位±/时基±"默认作用于 CH1 */
}

s32 ui_osc_samples_per_div(const ui_osc_t *s, int ch)
{
    s64 v;
    s32 ns = s->ch[ch & 1].time_per_div_ns;

    if (s->sample_rate <= 0) {
        return 1;
    }
    v = ((s64)ns * (s64)s->sample_rate) / 1000000000LL;
    if (v < 1) {
        v = 1;
    }
    if (v > 1000000) {
        v = 1000000;
    }
    return (s32)v;
}


/* ================= ★ 正弦最小二乘测频(高频段) =================
   背景: 原来的"迟滞翻转计数法"只在每周期采样点足够多时才准(代码注释自己写明 f < fs/3),
         64MSPS 下 26MHz 以上每周期只剩 2.2~2.5 点 -> 漏计/多计翻转 -> 严重测不准。
   做法: 对整条记录做单音最小二乘拟合, 不依赖"每次翻转都被采到", 一直有效到 Nyquist。
   模型: x[n] = A*cos(w*n) + B*sin(w*n) + C      (n = 0..N-1)
   给定 w 时 [A,B,C] 有闭式解(3x3 正规方程); 这里先把直流去掉再解 2x2, 更快也更稳。
   残差 R(w) = Σ(x-x̄)² − (A·Sxc + B·Sxs), 对 w 一维最小化即得频率。
   实现要点:
     - cos/sin 用【旋转递推】生成(每次试验只算一组初值), 不逐点调 sin()/cos();
     - 三角量用 float(利用 A9 的 VFPv3 硬件单精度), 累加器用 double(保证残差比较可靠);
     - 每 512 点归一化一次, 防旋转递推的幅度漂移;
     - 两段式: 短前缀(2048点)粗扫找全局谷 -> 全长(<=16384点)逐步收窄细扫。
   调用方按"每周期采样点 < 4"判断, 并做节流(见 ui_osc_measure 末尾)。 */
#include <math.h>
#define UI_FIT_PI     3.14159265358979323846
#define UI_FIT_PREFIX 2048u          /* 粗扫用的前缀长度(取前 N 点, 不是降采样) */

static double ui_fit_resid(const s16 *d, u32 n, double w)
{
    float  dc = (float)cos(w), ds = (float)sin(w);
    float  c = 1.0f, s = 0.0f;
    double mx = 0.0, Sxc = 0.0, Sxs = 0.0, Sxx = 0.0;
    double Scc = 0.0, Sss = 0.0, Scs = 0.0;
    double det, A, B;
    u32    i;

    for (i = 0; i < n; i++) {
        mx += (double)d[i];
    }
    mx /= (double)n;
    for (i = 0; i < n; i++) {
        double x = (double)d[i] - mx;
        double cf = (double)c, sf = (double)s;
        float  cn, sn;

        Sxc += x * cf; Sxs += x * sf; Sxx += x * x;
        Scc += cf * cf; Sss += sf * sf; Scs += cf * sf;
        cn = (c * dc) - (s * ds);
        sn = (s * dc) + (c * ds);
        c = cn; s = sn;
        if ((i & 511u) == 511u) {                 /* 防漂移 */
            double g = sqrt((double)((c * c) + (s * s)));

            if (g > 1e-12) { c = (float)((double)c / g); s = (float)((double)s / g); }
        }
    }
    det = (Scc * Sss) - (Scs * Scs);
    if (det < 1e-9) {
        return 1e30;                              /* 退化: 判为最差 */
    }
    A = ((Sxc * Sss) - (Sxs * Scs)) / det;
    B = ((Sxs * Scc) - (Sxc * Scs)) / det;
    return Sxx - ((A * Sxc) + (B * Sxs));         /* 残差平方和, 越小越像 */
}

/* 两段式搜索: 返回拟合频率(Hz); 失败返回 <= 0 */
static double ui_fit_freq(const s16 *d, u32 n, double fs, double f0)
{
    double w0, lo, hi, step, bw, br;
    u32    n1, k;

    if ((d == 0) || (n < 64u) || (fs <= 0.0) || (f0 <= 0.0)) {
        return 0.0;
    }
    w0 = (2.0 * UI_FIT_PI * f0) / fs;
    if (w0 <= 0.0) {
        return 0.0;
    }
    /* 计数法在 26MHz 附近误差可达百分之几十, 粗扫范围给 ±25% */
    lo = w0 * 0.75;
    hi = w0 * 1.25;
    if (lo <= 1e-9) { lo = 1e-9; }
    n1 = (n > UI_FIT_PREFIX) ? UI_FIT_PREFIX : n;
    k  = 48u;                                     /* 粗扫 48 格(短前缀, 便宜) */
    step = (hi - lo) / (double)k;
    bw = lo; br = ui_fit_resid(d, n1, lo);
    for (k = 1u; k <= 48u; k++) {
        double w = lo + (step * (double)k);
        double r = ui_fit_resid(d, n1, w);

        if (r < br) { br = r; bw = w; }
    }
    /* ★ 密集局部细扫(这一层是"必须"的, 否则会锁到旁瓣上):
       残差峰宽 ≈ fs/n1(前缀 2048 点 ⇒ 64MSPS 时约 31kHz), 而上面 48 格粗扫的步长
       是 0.5*f0/48(30MHz 时约 312kHz) —— 比峰宽粗 10 倍, 直接细扫必然踩偏旁瓣,
       细扫(下面那段)只能在局部收窄, 逃不出来。所以这里以 fs/n1 为步长,
       在"粗扫最优点 ±1 个粗步长"内再扫一遍, 保证落在真峰所在的那个瓣里。 */
    {
        double st2  = fs / (double)n1;              /* 前缀的自然频率栅格 */
        double lo2  = bw - step;
        double hi2  = bw + step;
        u32    m2   = (u32)((hi2 - lo2) / st2) + 1u;

        if (m2 > 96u) { m2 = 96u; }                 /* 上限: 约 20~90 次评估, 便宜 */
        if (m2 > 1u) {
            double s2 = (hi2 - lo2) / (double)m2;
            u32    j;

            for (j = 0u; j <= m2; j++) {
                double w = lo2 + (s2 * (double)j);
                double r;

                if (w <= 0.0) { continue; }
                r = ui_fit_resid(d, n1, w);
                if (r < br) { br = r; bw = w; step = s2; }
            }
        }
    }
    for (k = 0u; k < 4u; k++) {                   /* 细扫: 全长 + 逐级收窄 */
        double h  = step / 4.0;
        double wl = bw - h, wr = bw + h;
        double rl = ui_fit_resid(d, n, wl);
        double rc = ui_fit_resid(d, n, bw);
        double rr = ui_fit_resid(d, n, wr);

        if ((rl < rc) && (rl <= rr))      { bw = wl; }
        else if ((rr < rc) && (rr < rl))  { bw = wr; }
        step = h;
    }
    return (bw * fs) / (2.0 * UI_FIT_PI);
}
void ui_osc_measure(ui_osc_t *s, int ch)
{
    const ui_osc_ch_t *c = &s->ch[ch & 1];
    ui_osc_meas_t *m = &s->meas[ch & 1];
    const s16 *d;
    u32 n = c->len, i;
    s64 sum = 0, sq = 0, f_hz = 0;
    s32 lo, hi, mid, hyst;
    u32 cnt = 0;                           /* 方向翻转次数 */
    double t_first = 0.0, t_last = 0.0;    /* ★ 首/末次翻转的"亚样点"时刻(样点单位) */
    int lvl;

    m->vmin = 0; m->vmax = 0; m->vpp = 0;
    m->mean = 0; m->rms = 0;
    /* ★ 这里绝对不能把 freq_mhz 清零!
       ui_filter_build() 内部的中值门控 ui_med3_skip() 要读它: 清零的那一瞬间它是 0,
       门控就返回 0(= 做中值) -> 高频信号的峰/谷被 3 点中值削掉(削掉深度 = 1-cos(2*pi/spp),
       实测 10MHz@64MSPS 可达 37%), 而且逐帧在"削峰/不削"之间切换 -> 峰顶来回跳。
       冻结(停止)路径不经过这里清零, 所以一按停止波形就正常了。
       故意保留上一帧的值(一帧延迟, 无害): 测量出结果后由下面 m->freq_mhz = ... 覆盖。 */
    ui_filter_build(s, ch);            /* 本帧中值后的样本(门控用上一帧测到的频率) */
    d = ui_filt_ptr[ch & 1];
    if (n > (u32)UI_OSC_SNAP_MAX) {
        n = (u32)UI_OSC_SNAP_MAX;
    }
    if (d == 0 || n < 2) {
        return;
    }

    lo = hi = d[0];
    sum = lo;
    sq = (s64)lo * lo;
    for (i = 1; i < n; i++) {
        s32 v = d[i];
        if (v < lo) {
            lo = v;
        }
        if (v > hi) {
            hi = v;
        }
        sum += v;
        sq += (s64)v * v;
    }
    m->vmin = lo;
    m->vmax = hi;
    m->vpp = hi - lo;
    m->mean = (s32)(sum / (s64)n);
    m->rms = (s32)ui_isqrt64((u64)(sq / (s64)n));

    /* ★ 方波/脉冲判据(供画波形时决定"要不要做 sinc 插值"):
       方波的样点绝大多数贴在 min 或 max 上(平坦段), 正弦则均匀分布。
       这里数一下"落在两端 ±3% 满幅内"的样点占比, 超过 50% 就判为方波。
       复用上面已经算好的 lo/hi, 只多走一遍 d[](便宜, 每帧每通道一次)。
       注: 不改变任何已有的测量量, 只新增一个标志。 */
    {
        s32 thr = (hi - lo) / 32;                 /* 3% 左右 */
        u32 nflat = 0u, k2;

        if (thr < 1) { thr = 1; }
        for (k2 = 0u; k2 < n; k2++) {
            s32 v2 = d[k2];

            if ((v2 <= (lo + thr)) || (v2 >= (hi - thr))) { nflat++; }
        }
        m->sq_wave = ((nflat * 2u) > n) ? 1u : 0u;   /* 占比 > 50% */
    }

    /* 频率: 参考电平取 (max+min)/2, 加 Vpp/8 迟滞抗噪声。
       ★ 上升沿和下降沿都数(半周期分辨率): 记录里只有一两个周期时也能测准。
       只数上升沿的话, 低频信号一条记录里可能只碰到一次, 频率会差一倍
       (实测 800Hz 会报成 400Hz), "自动"按钮据此算出来的采样率就偏了 */
    mid = (hi + lo) / 2;
    hyst = (hi - lo) / 8;
    if (hyst < 1) {
        hyst = 1;
    }
    lvl = 0;                                 /* +1=高, -1=低, 0=还在迟滞带里 */
    for (i = 0; i < n; i++) {
        s32 v = d[i];
        int ns = lvl;
        if (v > mid + hyst) {
            ns = 1;
        } else if (v < mid - hyst) {
            ns = -1;
        }
        if ((lvl != 0) && (ns != 0) && (ns != lvl)) {   /* 一次干净的方向翻转 */
            /* ★ 只记整数下标的话, 首末两端各有 ±1 样点的量化误差, (last-first) 的相对
               误差就有 1/Δ(Δ=翻转跨度, 1.5MHz/64MSPS 时约 8000 样点 -> 180Hz),
               低频段"带小数的 kHz"就是这么偏掉的。这里把翻转时刻插值到小数:
               在"越过迟滞门限"的两个样点之间线性内插到该门限(升/降沿同一公式)。
               升沿和降沿的门限对同一正弦是同相位的偏移, 在首末相减时正好抵消,
               所以插值后量到的是真频率(而不是"门限穿越"的频率)。 */
            {
                double th = (ns > 0) ? (double)(mid + hyst) : (double)(mid - hyst);
                double y0 = (double)d[(i > 0u) ? (i - 1u) : 0u];
                double y1 = (double)v;
                double tt = (double)i;

                if (y1 != y0) {
                    tt = (double)i - 1.0 + (th - y0) / (y1 - y0);
                    if (tt < (double)i - 1.0) { tt = (double)i - 1.0; }
                    if (tt > (double)i)       { tt = (double)i;       }
                }
                if (cnt == 0u) {
                    t_first = tt;
                }
                t_last = tt;
            }
            cnt++;
        }
        if (ns != 0) {
            lvl = ns;
        }
    }
    if (s->sample_rate > 0) {
        if ((cnt >= 2) && (t_last > t_first)) {
            /* cnt 次翻转 = (cnt-1) 个半周期; 用插值后的时刻算, 不受样点量化影响 */
            f_hz = (s64)(((double)s->sample_rate * (double)(cnt - 1u)) /
                         (2.0 * (t_last - t_first)) + 0.5);
        }
        /* cnt < 2: 一条记录里连半个周期都不到, 不给数(界面显示 --) */
    }
    /* 测定频率的上限(用户要求放宽到 44MHz @64MSPS): 取 fs*11/16,
       64MSPS -> 44.0MHz, 48 -> 33.0, 32 -> 22.0, 16 -> 11.0, 8 -> 5.5, 5 -> 3.4MHz,
       即低档位仍随采样率等比缩放, 不会留下一个与实际采样率无关的常数。
       注意(物理前提, 用户已知): 64MSPS 的 Nyquist 是 32MHz, 32MHz 以上的输入会混叠,
         读数可能是一个"看起来正常"的折叠值(如 44MHz -> 20MHz); 是否可测取决于板上
         抗混叠滤波器与实际信号频段, 这里只按用户要求撤掉软件人为限幅。 */
    {
        s64 lim = (s->sample_rate > 0) ? ((s64)s->sample_rate * 11 / 16) : 44000000;
        if (lim < 2000000) {
            lim = 2000000;
        }
        if (f_hz > lim) {
            f_hz = lim;
        }
    }
    /* ★ 高频段(每周期采样点 < 4)计数法已失效 -> 改用正弦最小二乘拟合。
       三个关键点(第一版会"瞬间对、一直跳", 就是缺了这三条):
         ① 每通道各自记状态(不能共用, 否则两个通道互相挤掉刷新机会);
         ② 锁定后用【上次拟合值】当初值(不拿乱跳的计数值当种子, 搜索窗口才稳);
         ③ 只要拟合有效就直接显示拟合值, 不再与计数法混合(消除两估计器打架)。
       带迟滞: 进入 <4 点/周期, 退出要 >6 点/周期, 边界附近不会反复切。
       拟合失败(返回<=0)时退回计数法, 界面不会清空。 */
    if ((s->sample_rate > 0) && (f_hz > 0)) {
        static double ui_fit_last[2] = { 0.0, 0.0 };
        static u8     ui_fit_ok[2]   = { 0u, 0u };
        static u8     ui_fit_tick[2] = { 0u, 0u };
        int    ci   = ch & 1;
        double spp2 = (double)s->sample_rate / (double)f_hz;

        if ((spp2 < 4.0) || ((ui_fit_ok[ci] != 0u) && (spp2 < 6.0))) {
            ui_fit_tick[ci]++;
            if ((ui_fit_tick[ci] & 1u) == 0u) {         /* 每通道每 2 次刷新一次 */
                /* ★ 种子必须用【本次】计数法的值(它每次调用都会重算), 不能拿上次拟合值反馈:
                   一旦锁到旁瓣, 反馈会把错误永久锁死(第一版就是这样)。 */
                double seed = (double)f_hz;
                double ff   = ui_fit_freq(d, n, (double)s->sample_rate, seed);

                /* ★ 残差校验(切断"测频 -> 中值开关 -> 波形尖刺"的反馈链):
                   计数法在方波上会数入边沿振铃/谐波, 频率偏高 -> spp 偏小 ->
                   ui_med3_skip() 把 3 点中值跳掉 -> 边沿过冲尖刺直接上屏。
                   这里把"拟合频率的残差"和"计数频率的残差"比一比(都用 2048 点
                   前缀, 便宜): 拟合必须【明显更好】才采纳; 方波这类非单音信号
                   两者残差相近 -> 拒绝拟合 -> 回退计数法 -> 中值恢复工作 -> 尖刺消失。
                   阈值 0.6 可按实测调(越小越严)。 */
                if (ff > 0.0) {
                    u32    nchk = (n > UI_FIT_PREFIX) ? UI_FIT_PREFIX : n;
                    double wf   = (2.0 * UI_FIT_PI * ff) / (double)s->sample_rate;
                    double wc   = (2.0 * UI_FIT_PI * seed) / (double)s->sample_rate;
                    double rf   = ui_fit_resid(d, nchk, wf);
                    double rc   = ui_fit_resid(d, nchk, wc);

                    if ((rc <= 0.0) || (rf < (rc * 0.6))) {   /* 拟合明显更好才采纳 */
                        ui_fit_last[ci] = ff;
                        ui_fit_ok[ci]   = 1u;
                    } else {
                        ui_fit_ok[ci]   = 0u;   /* 非单音(方波等) -> 回退计数法 */
                    }
                } else {
                    ui_fit_ok[ci]   = 0u;               /* 拟合失败 -> 退回计数法 */
                }
            }
            if (ui_fit_ok[ci] != 0u) {
                f_hz = (s64)(ui_fit_last[ci] + 0.5);    /* ★ 只用拟合值 */
            }
        } else {
            ui_fit_ok[ci] = 0u;                         /* 回到低频: 清掉拟合状态 */
        }
    }
    m->freq_mhz = (s64)(f_hz * 1000.0 + 0.5);   /* ★ 64 位(毫赫兹), 别在这里收窄 */
}

/* ---------------- 运行/停止: 停止时冻结显示 ----------------
 * 按下"运行停止"后, 把当前两通道数据拷进快照, 之后测量与触发都不再更新,
 * 波形和测量值就停住了(应用侧即使继续采集也不影响显示)。
 * 如果单通道样本数超过 UI_OSC_SNAP_MAX, 请把它改大(已定义在文件上半部分)。 */

static s16        ui_snap[2][UI_OSC_SNAP_MAX];
static u32        ui_snap_len[2];
static const s16 *ui_orig_data[2];
static u32        ui_orig_len[2];
static u8         ui_snap_valid;
static u8         ui_run_state = 1u;      /* 1 = 运行中 */

static void ui_snapshot_save(ui_osc_t *s)
{
    int ch;

    for (ch = 0; ch < 2; ch++) {
        ui_orig_data[ch] = s->ch[ch].data;
        ui_orig_len[ch]  = s->ch[ch].len;
        ui_snap_len[ch]  = s->ch[ch].len;
        if (ui_snap_len[ch] > (u32)UI_OSC_SNAP_MAX) {
            ui_snap_len[ch] = (u32)UI_OSC_SNAP_MAX;
        }
        if ((s->ch[ch].data != 0) && (ui_snap_len[ch] > 0u)) {
            (void)memcpy(ui_snap[ch], s->ch[ch].data,
                         (size_t)ui_snap_len[ch] * sizeof(s16));
        }
        s->ch[ch].data = (ui_snap_len[ch] > 0u) ? ui_snap[ch] : 0;
        s->ch[ch].len  = ui_snap_len[ch];
    }
    ui_snap_valid = 1;
}

static void ui_snapshot_restore(ui_osc_t *s)
{
    int ch;

    if (ui_snap_valid == 0u) {
        return;
    }
    for (ch = 0; ch < 2; ch++) {
        s->ch[ch].data = ui_orig_data[ch];
        s->ch[ch].len  = ui_orig_len[ch];
    }
}

/* 最近一次成功触发的位置: 某一帧找不到触发点时用它保持相位(不清零) */
static u32 ui_trig_hold;
static u8  ui_trig_hold_ok;
static double ui_trig_hold_off;    /* ★ 与 ui_trig_hold 配套的亚样点偏移 */

/* ★ 触发搜索的起始余量(样点): 必须躲开记录开头的"镜像重建"区。
   显示窗口从 trig_pos 开始, 而 sinc_at 对负索引(记录开头之前)是用镜像补的数据,
   那段重建出来是失真的: 5MHz(12.8 样点/周期)时 7 个样点就是半个多周期, 屏幕左边
   一大片的峰会被削平、并且每帧随采样相位来回跳; 10MHz 时更是一整段变锯齿。
   余量取 2*SINC_HALF(16) 就够: 核最深只用到 p-7, 触发点 >=16 时最低索引 >0。
   (只影响"选哪一次过零", 不影响其余逻辑; 整周期地换选过零点屏幕上是看不出来的) */
#define UI_TRIG_MARGIN  (2 * 8)

/* 从 i 起连续 k 个样点是否都 >= level(上升沿用) / 都 <= level(下降沿用)。
   原来写死 3 点: 15MHz@64MSPS 只有 4.3 点/周期, 高于电平只持续约 2 点,
   条件永远不成立 -> 高频触发直接失灵。现在点数由调用方按"每周期样点数"决定。 */
static int ui_trig_all_ge(const s16 *d, u32 i, u32 k, s32 level)
{
    u32 j;

    for (j = 1u; j < k; j++) {
        if (d[i + j] < level) {
            return 0;
        }
    }
    return 1;
}

static int ui_trig_all_le(const s16 *d, u32 i, u32 k, s32 level)
{
    u32 j;

    for (j = 1u; j < k; j++) {
        if (d[i + j] > level) {
            return 0;
        }
    }
    return 1;
}

/* 重新测量 + 重新找触发点; 返回 1 表示找到触发点
 * 注意: s->run == 0(停止) 时直接返回, 保持上一帧的波形与测量值不变 */
u8 ui_osc_refresh(ui_osc_t *s)
{
    int ch;

    /* ---- 停止: 冻结(不重测、不重新触发) ---- */
    if (s->run == 0u) {
        if (ui_run_state != 0u) {
            ui_snapshot_save(s);              /* 刚按下"停止": 冻结当前数据 */
            ui_run_state = 0u;
        }
        ui_filter_build(s, 0);                /* 冻结期间画波形同样要有滤波后的样本 */
        ui_filter_build(s, 1);
        return s->trig_found;
    }
    if (ui_run_state == 0u) {
        ui_snapshot_restore(s);               /* 恢复"运行": 换回应用的数据缓冲 */
        ui_run_state = 1u;
    }

    for (ch = 0; ch < 2; ch++) {
        ui_osc_measure(s, ch);
    }

    s->trig_found = 0;
    /* ★ 关键: 找不到触发点时不要清零 —— 沿用它, 保持上一次的相位;
       清零的话这一帧波形会从样本 0 开始画, 屏幕上就是"突然移了个相"地闪一下。
       (数据异常/DMA 超时/幅度太小都会造成某一帧找不到触发点) */
    if (ui_trig_hold_ok == 0u) {
        s->trig_pos = 0;
        s->trig_off = 0.0;
    } else if (ui_trig_hold < s->ch[s->trig_src & 1u].len) {
        s->trig_pos = ui_trig_hold;
        s->trig_off = ui_trig_hold_off;
    } else {
        s->trig_pos = 0;
        s->trig_off = 0.0;
    }
    {
        const ui_osc_ch_t *c = &s->ch[s->trig_src & 1u];
        const ui_osc_meas_t *m = &s->meas[s->trig_src & 1u];
        s32 hyst = (s->trig_hyst > 0) ? s->trig_hyst : (m->vpp / 16);
        int armed;

        if (hyst < 1) {
            hyst = 1;
        }
        {
            const s16 *d = ui_filt_ptr[s->trig_src & 1u];
            u32 n = (c->len > (u32)UI_OSC_SNAP_MAX) ? (u32)UI_OSC_SNAP_MAX : c->len;

        if ((d != 0) && (n > (u32)UI_TRIG_MARGIN)) {
            u32 i;
            /* ★ 确认点数自适应: 一个周期只有几个样点时"连续 3 点"不可能成立。
               spp>=24 -> 3 点(低频抗毛刺); >=12 -> 2 点; 否则 1 点(靠 hyst+armed 抗毛刺)。
               频率未知时取 1 点: 宁可触发略灵敏, 也不要完全找不到触发点。 */
            /* ★ nconf 不再依赖"测得的频率"(那条耦合害过我们: 测频偏高 -> spp 变小 ->
               触发去毛刺从 3 点降到 1 点 -> 阶跃边沿乱选点 -> 竖线上下跳)。
               改成【直接从数据里量】: 找前两个跨越触发电平的位置, 它们的间隔 ≈ 半个
               周期, 于是 spp ≈ 2*间隔。量不到两个跨越点时保守取 3 点(低频方波就落
               在这一支 -> 恢复到"之前修好"的 3 点确认, 边沿振铃毛刺会被跳过)。 */
            int nconf = 3;

            {
                u32 ia = 0u, ib = 0u, k;
                int prev = 0;

                for (k = 0u; k < n; k++) {
                    int sgn = (d[k] > s->trig_level) ? 1 : ((d[k] < s->trig_level) ? -1 : prev);

                    if ((prev != 0) && (sgn != 0) && (sgn != prev)) {
                        if (ia == 0u) { ia = k; }
                        else          { ib = k; break; }
                    }
                    if (sgn != 0) { prev = sgn; }
                }
                if ((ia > 0u) && (ib > ia)) {
                    double spp2 = 2.0 * (double)(ib - ia);      /* 实测每周期采样点 */

                    if (spp2 < 24.0) { nconf = 2; }
                    if (spp2 < 12.0) { nconf = 1; }
                }
            }
            if (s->trig_edge == UI_OSC_EDGE_RISE) {
                armed = (d[UI_TRIG_MARGIN - 1] < s->trig_level) ? 1 : 0;
                for (i = UI_TRIG_MARGIN; i < n; i++) {
                    s32 v = d[i];                  /* 中值后的样本, 不会被毛刺骗到 */
                    if (armed == 0) {
                        if (v < s->trig_level - hyst) {
                            armed = 1;        /* 重新武装: 必须先退到电平以下 */
                        }
                    } else if ((v >= s->trig_level) &&
                               ((i + (u32)nconf) < n) &&
                               (ui_trig_all_ge(d, i, (u32)nconf, s->trig_level) != 0)) {
                        /* 连续 3 点都在电平之上才认: 单点噪声毛刺不会把触发点拉偏 */
                        s->trig_pos = i;
                        s->trig_off = ui_trig_cross_off(d, n, i, s->trig_level);
                        ui_trig_hold_off = s->trig_off;
                        s->trig_found = 1;
                        ui_trig_hold = i;
                        ui_trig_hold_ok = 1;
                        break;
                    }
                }
            } else {
                armed = (d[UI_TRIG_MARGIN - 1] > s->trig_level) ? 1 : 0;
                for (i = UI_TRIG_MARGIN; i < n; i++) {
                    s32 v = d[i];                  /* 中值后的样本, 不会被毛刺骗到 */
                    if (armed == 0) {
                        if (v > s->trig_level + hyst) {
                            armed = 1;
                        }
                    } else if ((v <= s->trig_level) &&
                               ((i + (u32)nconf) < n) &&
                               (ui_trig_all_le(d, i, (u32)nconf, s->trig_level) != 0)) {
                        /* 连续 3 点都在电平之下才认 */
                        s->trig_pos = i;
                        s->trig_off = ui_trig_cross_off(d, n, i, s->trig_level);
                        ui_trig_hold_off = s->trig_off;
                        s->trig_found = 1;
                        ui_trig_hold = i;
                        ui_trig_hold_ok = 1;
                        break;
                    }
                }
            }
        }
        }
    }
    return s->trig_found;
}

/* ---------------- 示波器可用的采样率(整数 MHz) ----------------
   必须与 clk_wiz1.h 里的预设表一一对应: clk_wiz_cfg() 只认表里的频率, 表外值直接忽略
   (硬件不变), 所以 UI 这边先把请求吸附到表内, 免得"软件以为改了、硬件其实没动",
   那样时基和测频会整体偏掉。小数频率(6.144/8.192/...)只给频谱仪用。 */
static const double ui_fs_scope_tbl[ADC_FS_SCOPE_N] = {
    5.0, 6.0, 8.0, 16.0, 32.0, 48.0, 64.0
};

int ui_osc_fs_scope_count(void)
{
    return (int)ADC_FS_SCOPE_N;
}

double ui_osc_fs_scope_at(int idx)
{
    if (idx < 0) { idx = 0; }
    if (idx >= (int)ADC_FS_SCOPE_N) { idx = (int)ADC_FS_SCOPE_N - 1; }
    return ui_fs_scope_tbl[idx];
}

/* 吸附到最近的可用频率(等距取近, 越界取端点) */
double ui_osc_fs_snap(double mhz)
{
    int i, best = 0;
    double bd = 1.0e18;

    for (i = 0; i < (int)ADC_FS_SCOPE_N; i++) {
        double d = mhz - ui_fs_scope_tbl[i];

        if (d < 0.0) { d = -d; }
        if (d < bd) { bd = d; best = i; }
    }
    return ui_fs_scope_tbl[best];
}

void ui_osc_autoset(ui_osc_t *s)
{
    int ch, best = 0;
    s32 full = 4095;

    if (s->adc_bits > 0 && s->adc_bits < 31) {
        full = (s32)((1u << s->adc_bits) - 1u);
    }

    /* 纵向档位: 波形占约 6 格; 近直流/无信号时用全量程档, 免得把小噪声放大满屏 */
    for (ch = 0; ch < 2; ch++) {
        ui_osc_measure(s, ch);
        if (s->ch[ch].data != 0 && s->ch[ch].len > 0) {
            s32 vpp = s->meas[ch].vpp;
            if (vpp < (full / 64)) {
                s->ch[ch].vdiv = ui_snap125_up(full / 5);
                s->ch[ch].center = s->adc_zero;
            } else {
                s32 per6 = vpp / 6;
                s->ch[ch].vdiv = ui_snap125_up((per6 > 0) ? per6 : 1);
                s->ch[ch].center = (s->meas[ch].vmax + s->meas[ch].vmin) / 2;
            }
        }
    }

    /* 触发: 取幅度大的通道做触发源(已关闭的通道不参与), 电平落在其中点 */
    if ((s->ch[1].on != 0u) && (s->ch[0].on == 0u)) {
        best = 1;
    } else if ((s->ch[0].on != 0u) && (s->ch[1].on == 0u)) {
        best = 0;
    } else if (s->meas[1].vpp > s->meas[0].vpp) {
        best = 1;
    }
    s->trig_src = (u8)best;
    s->trig_level = s->ch[best].center;
    s->trig_hyst = 0;
    s->trig_edge = UI_OSC_EDGE_RISE;
    s->trig_mode = UI_OSC_MODE_AUTO;

    /* 时基: 每个通道各自让 2 个周期占 10 格(每格 = 周期/5)。
       记录里装的是 ADC_FS_CYCLES 个周期(默认 8), 所以自动之后还能把时基放大
       约 4 倍去看更多周期(放大到顶时整条记录正好铺满一屏);
       某通道测不到频率时, 该通道的时基保持不变 */
    for (ch = 0; ch < 2; ch++) {
        if (s->meas[ch].freq_mhz > 0) {
            s64 ns = 200000000000LL / (s64)s->meas[ch].freq_mhz;   /* 每格 = 周期/5 -> 2 周期/屏 */
            ns = ui_clamp((s32)ns, 20, 200000000);
            s->ch[ch].time_per_div_ns = ui_snap125_up((s32)ns);
        }
    }

    /* ★ 顺带提一次 ADC 采样率变更请求(真正改时钟由应用在主循环里做):
       记录 n 点、一屏 10 格, 让整条记录正好装 ADC_FS_CYCLES 个周期 -> fs = (n/N)*f。
       自动时基是"2 个周期占 10 格", 所以理想情况下一屏正好等于一整条记录。
       低频时 fs 会撞到 MMCM 输出下限, 那就把时基改成"记录铺满一屏"(1-2-5 档向下靠,
       保证一屏不超过记录长度); 此时一屏不足一个周期 —— 这是 16384 点记录加上 MMCM
       下限的物理限制, 想突破只能改 PL(加抽取或加长记录)。 */
    s->fs_req_mhz1000 = -1;                  /* -1 = 按了"自动"但没测到频率 */
    if (s->meas[best].freq_mhz > 0) {
        u32    n  = (s->ch[best].len > 0u) ? s->ch[best].len : 8192u;
        double f  = (double)s->meas[best].freq_mhz / 1000.0;              /* Hz */
        double fs = ((double)n / (double)ADC_FS_CYCLES) * f / 1000000.0;  /* MHz */

        /* ★ 示波器只能用在表里的整数频率: 先把理想采样率吸附过去(低频会吸到 5MHz,
           高频最多 64MHz), 后面的时基夹取会保证一屏不超过一条记录 */
        fs = ui_osc_fs_snap(fs);

        s->fs_req_mhz1000 = (s32)(fs * 1000.0 + 0.5);

        /* ★ 时基必须让"一屏 10 格"落在一条记录之内: 10*t_div <= n/fs。
           自动时基是按"2 个周期占 10 格"算的, 但记录只有 n 点:
           低频信号(或采样率降不下去)时这个窗口会比记录还长, 波形画到一半
           就没数据了。这里按最终采样率再夹一次, 让整条记录尽量铺满一屏。
           用 s->fs_req_mhz1000 而不是实测值, 是因为夹的时候实得值还不知道;
           偏保守(实得比请求低时窗口只会更短), 不会画到记录之外。 */
        {
            double fs_mhz = (double)s->fs_req_mhz1000 / 1000.0;   /* 请求的采样率 MHz */
            s32 lim;

            if (fs_mhz <= 0.0) {
                fs_mhz = ui_osc_fs_scope_at(0);
            }
            /* 一条记录的时间 = n/fs(秒) -> 一屏 10 格每格最多占它的 1/10 */
            lim = (s32)(((double)n / (fs_mhz * 1000000.0)) * 1e9 / 10.0);
            if (lim < 20) {
                lim = 20;
            }
            for (ch = 0; ch < 2; ch++) {
                if ((s->meas[ch].freq_mhz > 0) &&
                    (s->ch[ch].time_per_div_ns > lim)) {
                    s->ch[ch].time_per_div_ns = ui_snap125_down(lim);
                }
            }
        }
    }
    ui_osc_refresh(s);
}

void ui_osc_trig_level_step(ui_osc_t *s, int dir)
{
    s32 step = s->ch[s->trig_src & 1u].vdiv / 10;
    s32 hi = 4095;
    if (step < 1) {
        step = 1;
    }
    if (s->adc_bits > 0 && s->adc_bits < 31) {
        hi = (s32)((1u << s->adc_bits) - 1u);
    }
    s->trig_level += (dir > 0) ? step : -step;
    s->trig_level = ui_clamp(s->trig_level, 0, hi);
    ui_osc_refresh(s);
}

void ui_osc_trig_edge_toggle(ui_osc_t *s)
{
    s->trig_edge = (s->trig_edge == UI_OSC_EDGE_RISE) ?
                   UI_OSC_EDGE_FALL : UI_OSC_EDGE_RISE;
    ui_osc_refresh(s);
}

void ui_osc_trig_src_toggle(ui_osc_t *s)
{
    s->trig_src = (u8)(s->trig_src ^ 1u);
    ui_osc_refresh(s);
}

void ui_osc_timebase_step(ui_osc_t *s, int dir)
{
    int ch = (int)(s->sel_ch & 1u);
    s32 v = s->ch[ch].time_per_div_ns;

    v = (dir > 0) ? ui_snap125_up(v + 1) : ui_snap125_down(v);
    v = ui_clamp(v, 20, 200000000);

    /* ★ 放大时基时, 一屏 10 格不能超过采集到的一条记录,
       否则波形画到一半就没数据了。就这样夹: t_div <= 记录长度/10 */
    if (s->sample_rate > 0) {
        u32 n = (s->ch[ch].len > 0u) ? s->ch[ch].len : 8192u;
        s64 lim = (s64)n * 1000000000LL / (10LL * (s64)s->sample_rate);

        if (lim < 20) {
            lim = 20;
        }
        if ((s64)v > lim) {
            v = (s32)lim;
        }
    }
    s->ch[ch].time_per_div_ns = v;
}

void ui_osc_vdiv_step(ui_osc_t *s, int ch, int dir)
{
    s32 v = s->ch[ch & 1].vdiv;
    v = (dir > 0) ? ui_snap125_up(v + 1) : ui_snap125_down(v);
    s->ch[ch & 1].vdiv = ui_clamp(v, 1, 1 << 20);
}

/* 切换"档位±/时基±"作用的通道(按钮文字会显示当前是 CH1 还是 CH2) */
void ui_osc_sel_ch_toggle(ui_osc_t *s)
{
    s->sel_ch = (u8)(s->sel_ch ? 0 : 1);
}

/* 打开/关闭某通道(点示波器界面通道条上的 CH1/CH2):
   关闭后不画该通道波形与 0V 标记, 六个测量值显示 "--" */
void ui_osc_ch_toggle(ui_osc_t *s, int ch)
{
    ui_osc_ch_t *c = &s->ch[ch & 1];
    c->on = (u8)(c->on ? 0u : 1u);
    /* 关掉的正好是触发源时, 把触发源让给另一个还开着的通道 */
    if ((c->on == 0u) && (s->trig_src == (u8)(ch & 1))) {
        int other = (ch & 1) ^ 1;
        if (s->ch[other].on != 0u) {
            s->trig_src = (u8)other;
            s->trig_level = s->ch[other].center;
            s->trig_hyst = 0;
        }
    }
}

void ui_osc_run_stop(ui_osc_t *s)
{
    s->run = (u8)(s->run ? 0 : 1);

    if (s->run == 0u) {
        /* 停止: 先用最新数据测一次 + 重新定位触发点, 再冻结(画面与数值同帧一致) */
        s->run = 1u;
        (void)ui_osc_refresh(s);
        s->run = 0u;
        ui_snapshot_save(s);
        ui_run_state = 0u;
    } else {
        (void)ui_osc_refresh(s);      /* 恢复运行: 立即用实时数据刷新一次 */
    }
}

/* 只重画波形区(网格/波形/0V 标记/触发线): 单缓冲原地刷新用, 不闪屏 */
void ui_osc_draw_wave(u32 fb, ui_osc_t *s)
{
    ui_osc_refresh(s);

    lcd_fill_rect(fb, UI_OSC_WAVE_X, UI_OSC_WAVE_Y, UI_OSC_WAVE_W,
                  UI_OSC_WAVE_H, UI_OSC_COLOR_BG);
    ui_draw_grid(fb);
    ui_draw_trigger(fb, s);
    ui_draw_zero_marks(fb, s);
    ui_draw_trace(fb, s, 0);
    lcd_set_protect(s->ch[0].color);   /* CH2 不覆盖 CH1: 交叉处 CH1 保持连续 */
    ui_draw_trace(fb, s, 1);
    lcd_set_protect(0);
}

/* 只刷新两个通道的测量数值(面积小, 比整屏刷新省很多时间) */
void ui_osc_draw_values(u32 fb, ui_osc_t *s)
{
    ui_osc_refresh(s);
    ui_draw_ch_values(fb, s, 0);
    ui_draw_ch_values(fb, s, 1);
}

/* 只叠加波形(不擦屏、不画网格): 单缓冲原地刷新用 */
void ui_osc_draw_trace(u32 fb, ui_osc_t *s)
{
    ui_osc_refresh(s);
    ui_draw_trigger(fb, s);
    ui_draw_zero_marks(fb, s);
    ui_draw_trace(fb, s, 0);
    lcd_set_protect(s->ch[0].color);   /* CH2 不覆盖 CH1: 交叉处 CH1 保持连续 */
    ui_draw_trace(fb, s, 1);
    lcd_set_protect(0);
}

/* 该列"当前的波形像素"里有没有 y 这一行(新旧两帧都算) */
static int ui_span_hits(int x, int y)
{
    int ch;

    if ((y < UI_CLIP_Y0) || (y > UI_CLIP_Y1)) {
        return 0;
    }
    for (ch = 0; ch < 2; ch++) {
        if ((ui_prev[ch][x].on != 0u) && (y >= ui_prev[ch][x].top) &&
            (y <= ui_prev[ch][x].bot)) {
            return 1;
        }
        if ((ui_next[ch][x].on != 0u) && (y >= ui_next[ch][x].top) &&
            (y <= ui_next[ch][x].bot)) {
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------
   单缓冲"原地增量"刷新一帧。

   原来的做法是: 每帧先把上一帧的轨迹整片擦掉、重画整片网格, 再画新轨迹。
   两条通道各 800 列, 列高就是波形包络高度, 一帧要写十几万个像素;
   而液晶从帧头扫到波形区顶部只有约 2.3ms —— 数据一"胖"(噪声大、包络高)
   就写不完, 液晶扫过去时正好看到"旧的擦了、新的还没画"的空白, 就是"闪一下"。

   现在改为增量刷新:
     1) 只重画"内容真的变了"的列(触发锁定后波形基本不动, 通常只有几列);
     2) 每列先把旧迹里新迹盖不到的那截补回底色, 再画新迹 ——
        屏幕上任何时候都留着波形, 不会出现整片空白;
     3) 网格/触发线/0V 标记由 ui_bg_at 逐像素算出来, 擦除时会自动保留,
        不再需要每帧重画整片网格。
   ------------------------------------------------------------------ */

void ui_osc_draw_live(u32 fb, ui_osc_t *s)
{
    static u8 dirty[UI_OSC_WAVE_W];
    int ch, x, y;
    int trig_chg, zero_chg;

    ui_osc_refresh(s);
    ui_bg_calc(s);


    /* ---- 1) 本帧两通道每列的范围(只算不画) ---- */
    (void)ui_trace_scan(s, 0, ui_next[0]);
    (void)ui_trace_scan(s, 1, ui_next[1]);

    /* ---- 2) 背景层(触发线 / 0V 标记)变了吗 ---- */
    trig_chg = (ui_cur_trig_on != ui_prev_trig_on) ? 1 : 0;
    if ((trig_chg == 0) && (ui_cur_trig_color != ui_prev_trig_color)) {
        trig_chg = 1;                       /* 触发源换了通道: 三角标记颜色跟着变 */
    }
    if ((trig_chg == 0) && (ui_cur_trig_on != 0u) && (ui_cur_trig_y != ui_prev_trig_y)) {
        trig_chg = 1;
    }
    zero_chg = 0;
    for (ch = 0; ch < 2; ch++) {
        if (ui_cur_zero_on[ch] != ui_prev_zero_on[ch]) {
            zero_chg = 1;
        } else if ((ui_cur_zero_on[ch] != 0u) && (ui_cur_zero_y[ch] != ui_prev_zero_y[ch])) {
            zero_chg = 1;
        }
    }
    if (ui_prev_valid == 0u) {
        trig_chg = 1;
        zero_chg = 1;
    }
    if (trig_chg != 0) {
        zero_chg = 1;          /* 0V 标记画在触发线之上: 触发线一动就要把标记补回来 */
    }

    /* ---- 3) 标出需要重画波形的列 ---- */
    for (x = 0; x < UI_OSC_WAVE_W; x++) {
        dirty[x] = (ui_prev_valid == 0u) ? 1u : 0u;
        if (dirty[x] != 0u) {
            continue;
        }
        for (ch = 0; ch < 2; ch++) {
            if ((ui_prev[ch][x].on != ui_next[ch][x].on) ||
                (ui_prev[ch][x].top != ui_next[ch][x].top) ||
                (ui_prev[ch][x].bot != ui_next[ch][x].bot)) {
                dirty[x] = 1;
            }
        }
    }
    if (trig_chg != 0) {
        /* 触发线(旧位置+新位置)压到的波形列: 波形也得重画, 否则被擦出小缺口 */
        for (x = UI_CLIP_X0; x <= UI_CLIP_X1; x++) {
            int a, b;
            if (ui_cur_trig_on != 0u) {
                if ((ui_trig_rows(x, ui_cur_trig_y, &a, &b) != 0) &&
                    ((ui_span_hits(x, a) != 0) || (ui_span_hits(x, b) != 0))) {
                    dirty[x] = 1;
                }
                if ((ui_trig_dash_on(x) != 0) && (ui_span_hits(x, ui_cur_trig_y) != 0)) {
                    dirty[x] = 1;
                }
            }
            if ((ui_prev_valid != 0u) && (ui_prev_trig_on != 0u)) {
                if ((ui_trig_rows(x, ui_prev_trig_y, &a, &b) != 0) &&
                    ((ui_span_hits(x, a) != 0) || (ui_span_hits(x, b) != 0))) {
                    dirty[x] = 1;
                }
                if ((ui_trig_dash_on(x) != 0) && (ui_span_hits(x, ui_prev_trig_y) != 0)) {
                    dirty[x] = 1;
                }
            }
        }
    }
    if (zero_chg != 0) {
        /* 0V 菱形只占 11 列: 这几列的波形直接重画 */
        for (ch = 0; ch < 2; ch++) {
            int xa = ui_zero_x_anchor(ch);
            if (((ui_prev_valid != 0u) && (ui_prev_zero_on[ch] != 0u)) ||
                (ui_cur_zero_on[ch] != 0u)) {
                for (x = xa - 5; x <= (xa + 5); x++) {
                    if ((x >= 0) && (x < UI_OSC_WAVE_W)) {
                        dirty[x] = 1;
                    }
                }
            }
        }
    }

    /* ---- 4) 背景层旧位置: 用底色擦掉(网格/另一个标记会保留) ---- */
    if ((trig_chg != 0) && (ui_prev_valid != 0u) && (ui_prev_trig_on != 0u)) {
        for (x = UI_CLIP_X0; x <= UI_CLIP_X1; x++) {
            int a, b;
            if (ui_trig_rows(x, ui_prev_trig_y, &a, &b) != 0) {
                if (ui_trig_tri_dy(x) == -2) {
                    for (y = a; y <= b; y++) {      /* 三角左边竖线: 整段擦掉 */
                        lcd_pixel(fb, x, y, ui_bg_at(x, y));
                    }
                } else {
                    lcd_pixel(fb, x, a, ui_bg_at(x, a));
                    if (b != a) {
                        lcd_pixel(fb, x, b, ui_bg_at(x, b));
                    }
                }
            }
            if (ui_trig_dash_on(x) != 0) {
                lcd_pixel(fb, x, ui_prev_trig_y, ui_bg_at(x, ui_prev_trig_y));
            }
        }
    }
    if ((zero_chg != 0) && (ui_prev_valid != 0u)) {
        for (ch = 0; ch < 2; ch++) {
            int xa = ui_zero_x_anchor(ch);
            if (ui_prev_zero_on[ch] == 0u) {
                continue;
            }
            for (x = xa - 5; x <= (xa + 5); x++) {
                int a, b;
                if ((x < 0) || (x >= UI_OSC_WAVE_W)) {
                    continue;
                }
                if (ui_zero_rows(x, xa, (int)ui_prev_zero_y[ch], &a, &b) == 0) {
                    continue;
                }
                lcd_pixel(fb, x, a, ui_bg_at(x, a));
                if (b != a) {
                    lcd_pixel(fb, x, b, ui_bg_at(x, b));
                }
            }
        }
    }

    /* ---- 5) 背景层新位置(变了才画; 先画它, 波形压在上面) ---- */
    if (trig_chg != 0) {
        ui_draw_trigger(fb, s);
    }
    if (zero_chg != 0) {
        ui_draw_zero_marks(fb, s);
    }

    /* ---- 6) 旧迹里新迹盖不到的那截: 补回底色 ---- */
    if (ui_prev_valid != 0u) {
        for (x = 0; x < UI_OSC_WAVE_W; x++) {
            if (dirty[x] == 0u) {
                continue;
            }
            for (ch = 0; ch < 2; ch++) {
                int nt, nb, ot, ob;
                if (ui_prev[ch][x].on == 0u) {
                    continue;
                }
                ot = ui_prev[ch][x].top;
                ob = ui_prev[ch][x].bot;
                if (ui_next[ch][x].on == 0u) {
                    ui_bg_fill_col(fb, x, ot, ob);
                    continue;
                }
                nt = ui_next[ch][x].top;
                nb = ui_next[ch][x].bot;
                if (ob < nt) {              /* 旧迹整段在新迹上方 */
                    ui_bg_fill_col(fb, x, ot, ob);
                } else if (ot > nb) {       /* 旧迹整段在新迹下方 */
                    ui_bg_fill_col(fb, x, ot, ob);
                } else {
                    if (ot < nt) {
                        ui_bg_fill_col(fb, x, ot, nt - 1);
                    }
                    if (ob > nb) {
                        ui_bg_fill_col(fb, x, nb + 1, ob);
                    }
                }
            }
        }
    }

    /* ---- 7) 画新迹: CH1 全部, 再 CH2(保护色, 交叉处 CH1 保持连续) ---- */
    for (ch = 0; ch < 2; ch++) {
        if (ch == 1) {
            lcd_set_protect(s->ch[0].color);
        }
        for (x = 0; x < UI_OSC_WAVE_W; x++) {
            if ((dirty[x] != 0u) && (ui_next[ch][x].on != 0u)) {
                lcd_vline(fb, UI_OSC_WAVE_X + x, ui_next[ch][x].top,
                          ui_next[ch][x].bot, s->ch[ch].color);
            }
        }
    }
    lcd_set_protect(0);

    /* ---- 8) 记录本帧内容 ---- */
    for (ch = 0; ch < 2; ch++) {
        for (x = 0; x < UI_OSC_WAVE_W; x++) {
            ui_prev[ch][x] = ui_next[ch][x];
        }
    }
    ui_prev_valid = 1;

    /* ---- 9) 测量值 ---- */
    ui_draw_ch_values(fb, s, 0);
    ui_draw_ch_values(fb, s, 1);
}

/* 波形区 / 测量区 的局部 D-Cache 刷新(区域连续且 cache line 对齐) */
void ui_osc_flush_wave(u32 fb)
{
    Xil_DCacheFlushRange((UINTPTR)(fb + (u32)UI_OSC_WAVE_Y * LCD_GFX_WIDTH * 3u),
                         (u32)UI_OSC_WAVE_H * LCD_GFX_WIDTH * 3u);
}

void ui_osc_flush_values(u32 fb)
{
    Xil_DCacheFlushRange((UINTPTR)(fb + (u32)UI_OSC_BOTTOM_Y * LCD_GFX_WIDTH * 3u),
                         (u32)UI_OSC_BOTTOM_H * LCD_GFX_WIDTH * 3u);
}

/* ★ 整片重画后必须调用: 把"当前背景层状态"抄给"上一帧背景层状态"。
   否则下一帧 ui_osc_draw_live() 会拿切换前那台界面的旧触发线/0V标记坐标去擦像素
   (ui_screens.c 里只有 ui_prev_valid != 0 才执行那段擦除), 擦出一片底色, 同时把
   大批列标脏再重画十几万像素 -> 从别的界面回到示波器时花屏一帧。
   注意: 单靠 ui_prev_valid = 0 不行 —— ui_draw_trace() 会在整片重画里把它置回 1。 */
static void ui_osc_sync_marks(void)
{
    ui_prev_trig_on    = ui_cur_trig_on;
    ui_prev_trig_color = ui_cur_trig_color;
    ui_prev_trig_y     = ui_cur_trig_y;
    ui_prev_zero_on[0] = ui_cur_zero_on[0];
    ui_prev_zero_on[1] = ui_cur_zero_on[1];
    ui_prev_zero_y[0]  = ui_cur_zero_y[0];
    ui_prev_zero_y[1]  = ui_cur_zero_y[1];
}
/* 整屏重画: 上电首次显示 / 界面或档位变化时调用 */
void ui_osc_draw(u32 fb, ui_osc_t *s)
{
    ui_osc_refresh(s);

    lcd_fill_rect(fb, UI_OSC_WAVE_X, UI_OSC_WAVE_Y, UI_OSC_WAVE_W,
                  UI_OSC_WAVE_H, UI_OSC_COLOR_BG);
    ui_draw_grid(fb);
    /* 触发虚线/0V 标记先画, 波形后画 —— 否则虚线会横切波形(中间出现小缺口) */
    ui_draw_trigger(fb, s);
    ui_draw_zero_marks(fb, s);
    ui_draw_trace(fb, s, 0);
    lcd_set_protect(s->ch[0].color);   /* CH2 不覆盖 CH1: 交叉处 CH1 保持连续 */
    ui_draw_trace(fb, s, 1);
    lcd_set_protect(0);

    ui_draw_header(fb, s);
    ui_draw_info(fb, s);
    ui_draw_bottom(fb, s);

    /* ★ 同步背景层"上一帧"状态(见 ui_osc_sync_marks 注释): 不同步的话下一帧原地刷新
       会去擦切换前的旧触发线/0V 标记 -> 从别的界面回到示波器花屏一帧 */
    ui_bg_calc(s);
    ui_osc_sync_marks();
}

/* 手动调整触发迟滞: 噪声大时加大(触发更稳), 小信号时减小(更灵敏) */
void ui_osc_trig_hyst_step(ui_osc_t *s, int dir)
{
    const ui_osc_meas_t *m = &s->meas[s->trig_src & 1u];
    s32 step = (m->vpp / 64) + 1;
    s32 hyst = (s->trig_hyst > 0) ? s->trig_hyst : (m->vpp / 16);

    if (hyst < 1) {
        hyst = 1;
    }
    hyst += (dir > 0) ? step : -step;
    if (hyst < 1) {
        hyst = 1;
    }
    if (hyst > 20000) {
        hyst = 20000;
    }
    s->trig_hyst = hyst;
    ui_osc_refresh(s);
}

/* 文字接口(公开): 供触摸按钮栏等模块复用, 与界面共用同一套字库 */
void ui_wave_grid(u32 fb)
{
    ui_draw_grid(fb);
}

/* 底部整条(通道条 + 测量值 + 面板分隔线): 与 ui_osc_draw 用的是同一个函数,
   所以"界面"窗口关掉后整条重画一遍即可完全恢复 */
void ui_osc_draw_bottom(u32 fb, ui_osc_t *s)
{
    ui_draw_bottom(fb, s);
}

int ui_draw_text(u32 fb, int x, int y, const char *s, u16 color, int scale)
{
    return ui_text(fb, x, y, s, color, scale);
}

u32 ui_text_size(const char *s, int scale)
{
    return ui_text_w(s, scale);
}

/* ---- 局部重画静态部分: 按钮动作后只刷这些区域, 避免整屏重画引起闪动 ---- */
void ui_osc_draw_status(u32 fb, ui_osc_t *s)
{
    ui_draw_header(fb, s);
}

void ui_osc_draw_info(u32 fb, ui_osc_t *s)
{
    ui_draw_info(fb, s);
}

void ui_osc_draw_tabs(u32 fb, ui_osc_t *s)
{
    ui_draw_ch_tab(fb, s, 0);
    ui_draw_ch_tab(fb, s, 1);
}

void ui_osc_flush_chrome(u32 fb)
{
    /* 顶栏(0..47) + 信息行(48..71): 连续区域 */
    Xil_DCacheFlushRange((UINTPTR)fb, 72u * (u32)LCD_GFX_WIDTH * 3u);
    /* 两个通道标题条(测量区最上面 20 行) */
    Xil_DCacheFlushRange((UINTPTR)(fb + (u32)UI_OSC_BOTTOM_Y *
                                   (u32)LCD_GFX_WIDTH * 3u),
                         (u32)UI_OSC_CH_TAB_H * (u32)LCD_GFX_WIDTH * 3u);
}

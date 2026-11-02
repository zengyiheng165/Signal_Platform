/******************************************************************************
 * ui_fra.c - 频率特性分析仪界面(第一版: 纯界面 + 演示曲线)
 * 布局: 顶栏 0..47 / 信息行 48..71 / 四格 72..438 / 按钮栏 440..479
 *   左上 幅频(dB, 对数X)   右上 相频(度, 对数X)
 *   左下 冲激响应          右下 阶跃响应
 * ★ 现在画的是"演示数据"(一个二阶低通的解析曲线), 用来先确认版面;
 *   接上采集/拟合后把 fra_demo_* 换成真实数组即可。
 ******************************************************************************/
#include "ui_fra.h"
#include "ui_app.h"
#include "ui_screens.h"
#include "lcd_gfx.h"
#include "xil_cache.h"
#include <stdio.h>   /* sprintf */
#include <math.h>
#include <string.h>

#define FRA_PANE_W   400
#define FRA_PANE_H   183
#define FRA_X0       0
#define FRA_X1       400
#define FRA_Y0       72
#define FRA_Y1       255      /* 上排 72..254 */
#define FRA_Y2       256      /* 下排 256..438 */
#define FRA_Y3       439
#define FRA_PLOT_L   46           /* 画图区左边距: 留给纵轴刻度文字(要在 fra_pane 之前定义) */

static const u16 FRA_DIM = UI_OSC_COLOR_GRID;


/* 频率 -> 本格内的 x(对数轴, 500Hz..1MHz 共 3.3 个十倍频) */
static int fra_x_of_f(double f, int x0, int w)
{
    double lo = log10(500.0), hi = log10(1000000.0);
    double t = (log10(f) - lo) / (hi - lo);

    if (t < 0.0) { t = 0.0; }
    if (t > 1.0) { t = 1.0; }
    /* ★ 铺满整格宽度(x0 .. x0+w-1)并四舍五入: 刻度线/网格/打点/文字用同一个
       映射, 所以打点位置与坐标轴严格对应(以前内缩 4px, 端点会差几像素)。 */
    return x0 + (int)(t * (double)(w - 1) + 0.5);
}

/* 画一格: 边框 + 标题 + 对数网格(十倍频竖线) */
static void fra_pane(u32 fb, int x0, int y0, int w, int h, const char *title)
{
    int px = x0 + FRA_PLOT_L;                 /* 画图区左边界(左边留给纵轴刻度) */
    int pw = w - FRA_PLOT_L;

    lcd_rect(fb, x0, y0, w, h, UI_OSC_COLOR_FRAME);
    (void)ui_draw_text(fb, x0 + 8, y0 + 6, title, UI_OSC_COLOR_TEXT, 1);
    /* 十倍频竖线: 1k / 10k / 100k */
    {
        const double fs[3] = { 1000.0, 10000.0, 100000.0 };
        int i;

        for (i = 0; i < 3; i++) {
            int x = fra_x_of_f(fs[i], px, pw);

            lcd_hline(fb, x, x, y0 + 26, FRA_DIM);          /* 竖线用 hline 逐点 */
            { int yy; for (yy = y0 + 26; yy < (y0 + h - 22); yy++) { lcd_pixel(fb, x, yy, FRA_DIM); } }
        }
    }
    lcd_hline(fb, px, px + pw - 1, y0 + h - 22, FRA_DIM);       /* 底轴(与 x 映射同宽) */
}



/* ★ 频率读数(带一位小数): 参数是"0.1Hz 为单位的整数"(即 hz*10), 避免整数截断;
     <1kHz -> 500.0Hz ; <1MHz -> 20.5kHz ; >=1MHz -> 1.0MHz。全程整数运算, 不用 %f。 */
static void fra_fmt_hz1(char *buf, s32 hz10)
{
    if (hz10 < 0) { hz10 = -hz10; }
    if (hz10 < 10000) {                       /* < 1kHz: 0.1Hz 单位 */
        (void)sprintf(buf, "%d.%dHz", (int)(hz10 / 10), (int)(hz10 % 10));
    } else if (hz10 < 10000000) {             /* < 1MHz: 0.1kHz = 100Hz = 1000 个 0.1Hz */
        (void)sprintf(buf, "%d.%dkHz", (int)(hz10 / 10000), (int)((hz10 / 1000) % 10));
    } else {                                  /* >= 1MHz: 0.1MHz = 100kHz = 100000 个 0.1Hz */
        (void)sprintf(buf, "%d.%dMHz", (int)(hz10 / 1000000), (int)((hz10 / 100000) % 10));
    }
}

static void fra_fmt_hz(char *buf, s32 hz)
{
    /* XXROUND: 原来直接截断(/1000), 1499Hz 会显示成 1kHz、1999Hz 也显示成 1kHz。
       改成【先四舍五入到 kHz】再按档显示, 并自动处理进位(999.6kHz -> 1.000MHz)。 */
    if (hz < 0) { hz = -hz; }
    if (hz < 1000) {
        (void)sprintf(buf, "%dHz", (int)hz);                    /* <1kHz 保持原样(不进位) */
    } else {
        s32 khz = (hz + 500) / 1000;                            /* 四舍五入到 kHz */

        if (khz >= 1000) {
            (void)sprintf(buf, "%d.%03dMHz", (int)(khz / 1000), (int)(khz % 1000));
        } else {
            (void)sprintf(buf, "%dkHz", (int)khz);
        }
    }
}
/* ================= 设置页(整页, 不走窗口底板) ================= */
#define FRA_SET_Y0   72
#define FRA_SET_RH   44
#define FRA_SET_ROWS 8
#define FRA_SET_XL   560      /* "<" 中心 */
#define FRA_SET_XR   740      /* ">" 中心 */

static const char *fra_set_name(int r)
{
    switch (r) {
    case 0: return "起始频率";
    case 1: return "终止频率";
    case 2: return "步长";
    case 3: return "输出幅度";
    case 4: return "平均次数";
    case 5: return "拟合阶数";
    case 6: return "纵轴";
    case 7: return "用时预警";
    default: return "";
    }
}

static void fra_set_text(const ui_fra_t *a, int r, char *buf)
{
    switch (r) {
    case 0: fra_fmt_hz(buf, a->f_start_hz); break;
    case 1: fra_fmt_hz(buf, a->f_stop_hz);  break;
    case 2: (void)sprintf(buf, "%dHz", (int)a->step_hz); break;
    case 3: (void)sprintf(buf, "%d.%03dVpp", (int)(a->vpp_mv / 1000), (int)(a->vpp_mv % 1000)); break;
    case 4: (void)sprintf(buf, "x%d", (int)a->avg); break;
    case 5: (void)sprintf(buf, "%d", (int)a->order); break;
    case 6: (void)strcpy(buf, (a->y_db != 0u) ? "dB" : "线性"); break;
    case 7: (void)strcpy(buf, (a->warn_s > 0) ? "" : "关"); if (a->warn_s > 0) { (void)sprintf(buf, "%ds", (int)a->warn_s); } break;
    default: buf[0] = '\0'; break;
    }
}

/* 频率吸附到 500Hz 整数倍(分辨率 = fs/N = 500Hz) */
static s32 fra_snap500(s32 hz)
{
    if (hz < 500) { hz = 500; }
    hz = ((hz + 250) / 500) * 500;
    if (hz < 500) { hz = 500; }
    if (hz > 1000000) { hz = 1000000; }
    return hz;
}

static void fra_set_step(ui_fra_t *a, int r, int dir)
{
    switch (r) {
    case 0: a->f_start_hz = fra_snap500(a->f_start_hz + dir * 10000);
            if (a->f_start_hz < 1000) { a->f_start_hz = 1000; }   /* ★ 起始频率下限 1kHz */
            break;                                                 /* (原来这里没有 break, 会顺带改到"终止"档) */
    case 1: a->f_stop_hz  = fra_snap500(a->f_stop_hz  + dir * 10000); break;
    case 2: a->step_hz = (a->step_hz == 500) ? 1000 : 500; break;      /* 只有 500/1000 有效 */
    case 3: a->vpp_mv += dir * 100;
            if (a->vpp_mv < 100) { a->vpp_mv = 100; }
            if (a->vpp_mv > UI_AWG_VPP_MAX_MV) { a->vpp_mv = UI_AWG_VPP_MAX_MV; }   /* ★ 改用标定宏, 不再硬编码(满幅 11.5Vpp) */
            break;
    case 4: a->avg = (u8)((dir > 0) ? ((a->avg >= 8u) ? 1u : (a->avg * 2u))
                                   : ((a->avg <= 1u) ? 8u : (a->avg / 2u))); break;
    case 5: a->order = 2u; break;                    /* 阶数固定二阶(PL 只出二阶), 按钮不再改数 */

    case 6: a->y_db = (u8)((a->y_db != 0u) ? 0u : 1u); break;
    case 7: a->warn_s += dir * 10;
            if (a->warn_s < 0) { a->warn_s = 0; }
            if (a->warn_s > 600) { a->warn_s = 600; }
            break;
    default: break;
    }
    /* 起始 < 终止 保证 */
    if (a->f_start_hz > (a->f_stop_hz - 1000)) { a->f_stop_hz = fra_snap500(a->f_start_hz + 1000); }
}

/* 重画一行 + 只刷这一行(44*800*3 是 32 字节整数倍) */
static void fra_set_row(u32 fb, const ui_fra_t *a, int r)
{
    int y = FRA_SET_Y0 + r * FRA_SET_RH;
    char val[32];

    fra_set_text(a, r, val);
    lcd_fill_rect(fb, 0, y, LCD_GFX_WIDTH, FRA_SET_RH, (r & 1) ? UI_OSC_COLOR_BG : UI_OSC_COLOR_INFO);
    (void)ui_draw_text(fb, 40, y + 14, fra_set_name(r), UI_OSC_COLOR_LABEL, 1);
    (void)ui_draw_text(fb, FRA_SET_XL, y + 14, "<", UI_OSC_COLOR_TEXT, 1);
    (void)ui_draw_text(fb, 640, y + 14, val, UI_OSC_COLOR_TEXT, 1);
    (void)ui_draw_text(fb, FRA_SET_XR, y + 14, ">", UI_OSC_COLOR_TEXT, 1);
    lcd_hline(fb, 0, LCD_GFX_WIDTH - 1, y + FRA_SET_RH - 1, UI_OSC_COLOR_FRAME);
    Xil_DCacheFlushRange((UINTPTR)(fb + (u32)y * LCD_GFX_WIDTH * 3u),
                         (u32)FRA_SET_RH * LCD_GFX_WIDTH * 3u);
}

static void fra_draw_settings(u32 fb, const ui_fra_t *a)
{
    int r;

    for (r = 0; r < FRA_SET_ROWS; r++) {
        fra_set_row(fb, a, r);
    }

    Xil_DCacheFlushRange((UINTPTR)fb,
                         ((u32)(FRA_SET_Y0 + FRA_SET_ROWS * FRA_SET_RH + 30) * LCD_GFX_WIDTH * 3u));
}

int ui_fra_set_touch(u32 fb, ui_app_t *a, int ev, int x, int y)
{
    /* ★ 数字键盘开着时: 触摸只给键盘; 回车确认后把输入归一化写入起始/终止频率。
       归一化规则(用户要求): 任意输入 -> 夹到 1kHz..1MHz, 再四舍五入到最近的 500Hz 整数倍。 */
    if (a->kbd.open != 0u) {
        if (ui_numkbd_touch(fb, &a->kbd, x, y, ev) == 0) {        /* 键盘收起 */
            if (a->kbd.ok != 0u) {
                s32 hz = a->kbd.value / 1000;                     /* 键盘值 = 数字 x 1000 */

                if (hz < 1000)    { hz = 1000; }
                if (hz > 1000000) { hz = 1000000; }
                hz = ((hz + 250) / 500) * 500;                    /* ★ 最近的 500Hz 整数倍 */
                if (a->kbd.target == UI_APP_KBD_TGT_FRA_STOP) {
                    a->fra.f_stop_hz = hz;
                    if (a->fra.f_stop_hz < (a->fra.f_start_hz + 500)) {
                        a->fra.f_stop_hz = a->fra.f_start_hz + 500;
                    }
                } else {
                    a->fra.f_start_hz = hz;
                    if (a->fra.f_start_hz > (a->fra.f_stop_hz - 500)) {
                        a->fra.f_stop_hz = a->fra.f_start_hz + 500;
                    }
                }
                a->fra.draw_set = 1u;          /* 值变了: 帧边界重画设置页 */
            }
            a->kbd.open = 0u;
            ui_app_kbd_close(a);               /* 让窗口层贴回底板 */
        }
        return 1;                              /* 键盘模态: 其余触摸全部吃掉 */
    }
    int r;

    if (a->screen != UI_SCREEN_FRA) { return 0; }
    /* ★ 底部按钮栏要让给按钮栏处理: 否则"设置"按钮再也点不到 -> 进了设置页就退不出来 */
    if (y >= (int)UI_OSC_BTN_Y) { return 0; }
    if (ev != TOUCH_EV_UP) { return 1; }                 /* 按下/移动: 只吃掉事件 */
    /* ★ 先判 y 是否落在设置页行区之内, 再算行号 ——
       C 的整数除法向零截断: y = 29..71(标题下半 + 信息行 48..71) 时
       (y - 72) / 44 会得到 0, 被误判成第 0 行"起始频率" -> 点顶栏却弹出数字键盘。
       下界必须在这里挡, 光靠下面的 (r < 0) 挡不住(算出来就是 0, 不是负数)。 */
    if ((y < FRA_SET_Y0) || (y >= (FRA_SET_Y0 + (FRA_SET_ROWS * FRA_SET_RH)))) { return 1; }
    r = (y - FRA_SET_Y0) / FRA_SET_RH;
    if ((r < 0) || (r >= FRA_SET_ROWS)) { return 1; }
    /* ★ A 方案: 起始/终止频率这两行整行点击 -> 弹数字键盘(不再用 < > 加减) */
    if ((r == 0) || (r == 1)) {
        char init[16];

        /* ★ 键盘要的是"纯数字串"(单位由 unit 档单独显示): 传 "1.0kHz" 会打不开。
           键盘值 = 数字 x 1000, 所以给 kHz 的定点数: "20.500" 表示 20.5kHz。 */
        /* ★ 不再预填当前值: 预填 "%d.%03d" 会占满 3 位小数, 而 kbd_push_char 的规则是
           "小数位已满 dec_max 就丢弃新输入的数字" -> 用户按数字没反应(滤波器页实测就是这个 bug)。
           改成留空: 键盘上直接输新值(单位 kHz), 回车生效。 */
        init[0] = '\0';
        ui_app_kbd_open_fra(a, r, init);                   /* ★ 内部会置 ui_win_redraw_req */
        return 1;
    }
    if ((x >= (FRA_SET_XL - 30)) && (x <= (FRA_SET_XL + 30))) {
        fra_set_step(&a->fra, r, -1);
    } else if ((x >= (FRA_SET_XR - 30)) && (x <= (FRA_SET_XR + 30))) {
        fra_set_step(&a->fra, r, +1);
    } else {
        return 1;                                        /* 点空白: 无反应 */
    }
    fra_set_row(fb, &a->fra, r);                         /* 只重画这一行 */
    a->fra.redraw_bar = 1u;   /* ★ 参数变了: 按钮栏上的"纵轴/阶数/平均"标签同样要跟着刷 */
    return 1;
}
/* 预计用时(秒): 每帧 ≈ 2.5ms(= 16384/8.192MHz 记录 2ms + 开销); 每个频点需要
   (稳定 3 帧 + 平均 avg 帧) 帧 ⇒ 预计 = 点数 x (3+avg) x 2.5ms
   (2000 点、平均 4 ⇒ 约 35s; 旧公式按 8ms/点x平均 会高估到 64s) */
int ui_fra_est_sec(const ui_fra_t *a)
{
    s32 npt = (a->step_hz > 0) ? ((a->f_stop_hz - a->f_start_hz) / a->step_hz + 1) : 0;
    s32 ms  = npt * (3 + (s32)a->avg) * 25 / 10;      /* 2.5ms -> 25/10 */

    return (int)(ms / 1000);
}
/* ---- 实测曲线: 幅频/相频(点) ---- */
/* ---------------- 单曲线视图(整屏一格) ---------------- */
#define FRA_FULL_X   0
#define FRA_FULL_Y   72
#define FRA_FULL_W   800
#define FRA_FULL_H   367          /* 72..438 */

const char *ui_fra_view_short(u8 v)
{
    switch (v) {
    case 0:  return "幅频";
    case 1:  return "相频";
    case 2:  return "冲激";
    default: return "阶跃";
    }
}

static const char *fra_view_name(u8 v)
{
    switch (v) {
    case 0:  return "幅频响应";
    case 1:  return "相频响应";
    case 2:  return "冲激响应";
    default: return "阶跃响应";
    }
}

/* 幅频(整屏): 对数频率轴; dB 轴取 +20..-60, 线性轴取 0..2 */
/* ---------------- 坐标轴刻度 ---------------- */
/* 纵轴刻度文字(画在左侧留白里, 竖直居中对齐刻度线) */
static void fra_y_tick(u32 fb, const char *s, int y)
{
    int tw = (int)ui_text_size(s, 1);
    int ty = y - 8;

    if (ty < FRA_FULL_Y + 20) { ty = FRA_FULL_Y + 20; }
    if (ty > (FRA_FULL_Y + FRA_FULL_H - 20)) { ty = FRA_FULL_Y + FRA_FULL_H - 20; }
    lcd_hline(fb, FRA_FULL_X + FRA_PLOT_L - 4, FRA_FULL_X + FRA_PLOT_L, y, FRA_DIM);  /* 刻度短线 */
    (void)ui_draw_text(fb, FRA_FULL_X + FRA_PLOT_L - 8 - tw, ty, s, UI_OSC_COLOR_LABEL, 1);
}

/* 横轴: 频率刻度 500/1k/10k/100k/1M (对数轴, 位置与网格一致) */
static void fra_axes_freq(u32 fb, int ybot)
{
    static const struct { double f; const char *s; } tk[5] = {
        { 500.0, "500" }, { 1000.0, "1k" }, { 10000.0, "10k" },
        { 100000.0, "100k" }, { 1000000.0, "1M" }
    };
    int i;

    for (i = 0; i < 5; i++) {
        int x  = fra_x_of_f(tk[i].f, FRA_FULL_X + FRA_PLOT_L, FRA_FULL_W - FRA_PLOT_L);
        int tw = (int)ui_text_size(tk[i].s, 1);
        int tx = x - tw / 2;

        if (tx < 2) { tx = 2; }
        if ((tx + tw) > (LCD_GFX_WIDTH - 2)) { tx = LCD_GFX_WIDTH - 2 - tw; }
        lcd_vline(fb, x, ybot, ybot + 4, FRA_DIM);                    /* 刻度短线(竖) */
        (void)ui_draw_text(fb, tx, ybot + 6, tk[i].s, UI_OSC_COLOR_LABEL, 1);
    }
}

/* 纵轴: 幅频 dB, +20(顶) .. -60(底) */
static void fra_axes_db(u32 fb, int ytop, int h)
{
    static const int db[5] = { 20, 0, -20, -40, -60 };
    int i;

    for (i = 0; i < 5; i++) {
        int y = ytop + (int)(((20.0 - (double)db[i]) / 80.0) * (double)h);
        char s[8];

        if (db[i] == 0) { (void)strcpy(s, "0"); }
        else            { (void)sprintf(s, "%+d", db[i]); }
        fra_y_tick(fb, s, y);
    }
}

/* 纵轴: 幅频线性 0..2 */
static void fra_axes_lin(u32 fb, int ytop, int ybot, int h)
{
    static const char *lb[3] = { "2", "1", "0" };
    int i;

    for (i = 0; i < 3; i++) {
        (void)h;
        fra_y_tick(fb, lb[i], ybot - (ybot - ytop) * (2 - i) / 2);
    }
}

/* 纵轴: 相频 度, +360(顶) .. -360(底) */
static void fra_axes_deg(u32 fb, int ytop, int h)
{
    static const int dg[5] = { 180, 90, 0, -90, -180 };   /* ★ 相位范围 -180..180 */
    int i;

    for (i = 0; i < 5; i++) {
        int y = ytop + (int)(((180.0 - (double)dg[i]) / 360.0) * (double)h);
        char s[8];

        if (dg[i] == 0) { (void)strcpy(s, "0"); }
        else            { (void)sprintf(s, "%+d", dg[i]); }
        fra_y_tick(fb, s, y);
    }
}

/* 两点连线(逐像素, 用于叠加拟合曲线) */
static void fra_line(u32 fb, int x0, int y0, int x1, int y1, u16 col)
{
    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
    int sx = (x1 >= x0) ? 1 : -1;
    int sy = (y1 >= y0) ? 1 : -1;
    int er = dx - dy;

    for (;;) {
        lcd_pixel(fb, x0, y0, col);
        if ((x0 == x1) && (y0 == y1)) { break; }
        {
            int e2 = er * 2;

            if (e2 > -dy) { er -= dy; x0 += sx; }
            if (e2 <  dx) { er += dx; y0 += sy; }
        }
    }
}

/* 幅频: 值 -> y(实测点与拟合线共用, 保证两者严格重合) */
static int fra_mag_y(const ui_fra_t *a, double v, int ytop, int ybot, int h)
{
    if (v <= 1e-9) { v = 1e-9; }
    if (a->y_db != 0u) {
        double k = (20.0 - 20.0 * log10(v)) / 80.0;

        if (k < 0.0) { k = 0.0; }
        if (k > 1.0) { k = 1.0; }
        return ytop + (int)(k * (double)h);
    }
    {
        double k = v / 2.0;

        if (k > 1.0) { k = 1.0; }
        return ybot - (int)(k * (double)h);
    }
}

/* 相频: 度 -> y */
static int fra_pha_y(double p, int ytop, int h)
{
    double k = (180.0 - p) / 360.0;    /* ★ 相位范围 -180..180 */

    if (k < 0.0) { k = 0.0; }
    if (k > 1.0) { k = 1.0; }
    return ytop + (int)(k * (double)h);
}

/* ★ 光标绘制(定义在文件末尾, 这里前置声明) */
/* ★ 光标"无闪"方案: 每次画光标前, 先把上一次光标占据的那一小块贴回屏幕,
   这样就不用重画整个曲线区(fra_redraw_pane 要铺 800x367x3≈0.88MB, 会闪)。
   备份区宽度取够放下读数框, 高度覆盖曲线区。 */
#define CUR_BAK_W  192
#define CUR_BAK_H  352
static u8  cur_bak[CUR_BAK_W * CUR_BAK_H * 3];
static int cur_bak_x = -1, cur_bak_y = 0, cur_bak_w = 0, cur_bak_h = 0;

static void cur_bak_save(u32 fb, int x, int y, int w, int h)
{
    int r;

    if ((w <= 0) || (h <= 0) || (w > CUR_BAK_W) || (h > CUR_BAK_H)) { cur_bak_x = -1; return; }
    if (x < 0) { x = 0; }
    if ((x + w) > LCD_GFX_WIDTH) { w = LCD_GFX_WIDTH - x; }
    if ((y + h) > LCD_GFX_HEIGHT) { h = LCD_GFX_HEIGHT - y; }
    if ((w <= 0) || (h <= 0)) { cur_bak_x = -1; return; }
    for (r = 0; r < h; r++) {
        const u8 *src = (const u8 *)(fb + ((u32)(y + r) * LCD_GFX_WIDTH + (u32)x) * 3u);
        u8 *dst = &cur_bak[(u32)r * (u32)CUR_BAK_W * 3u];

        (void)memcpy(dst, src, (size_t)w * 3u);
    }
    cur_bak_x = x; cur_bak_y = y; cur_bak_w = w; cur_bak_h = h;
}

static void cur_bak_restore(u32 fb)
{
    int r;

    if (cur_bak_x < 0) { return; }
    for (r = 0; r < cur_bak_h; r++) {
        u8 *dst = (u8 *)(fb + ((u32)(cur_bak_y + r) * LCD_GFX_WIDTH + (u32)cur_bak_x) * 3u);
        const u8 *src = &cur_bak[(u32)r * (u32)CUR_BAK_W * 3u];

        (void)memcpy(dst, src, (size_t)cur_bak_w * 3u);
    }
    /* ★ 逐行只刷窄带那点字节(按 32 字节对齐): 原来按整行刷 845KB, 会把标题行也推下去 -> 标题闪 */
    {
        int xb = cur_bak_x;
        int nb = cur_bak_w * 3;
        u32 addr;
        u32 base;

        addr = (u32)((UINTPTR)(fb + ((u32)cur_bak_y * LCD_GFX_WIDTH + (u32)xb) * 3u)) & 0x1Fu;
        if (addr != 0u) { xb -= (int)(addr / 3u); nb += (int)addr; }
        if (xb < 0) { xb = 0; }
        base = (u32)((UINTPTR)(fb + ((u32)cur_bak_y * LCD_GFX_WIDTH) * 3u) + (u32)xb * 3u);
        nb = (nb + 31) & ~31;                     /* 长度补到 32 字节整数倍 */
        if ((xb * 3 + nb) > (LCD_GFX_WIDTH * 3)) { nb = LCD_GFX_WIDTH * 3 - xb * 3; }
        for (r = 0; r < cur_bak_h; r++) {
            Xil_DCacheFlushRange((UINTPTR)(base + (u32)r * LCD_GFX_WIDTH * 3u), nb);
        }
    }
    cur_bak_x = -1;
}

static void fra_draw_cursor(u32 fb, const ui_fra_t *a, int ytop, int ybot, int px, int pw, int is_phase);
static void fra_draw_tf(u32 fb, const ui_fra_t *a, int is_phase, int px, int pw, int ytop);

static void fra_draw_mag(u32 fb, const ui_fra_t *a)
{
    cur_bak_x = -1;   /* 曲线区被整块重画: 备份作废(否则会把旧光标/旧读数贴回来) */
    int ytop = FRA_FULL_Y + 26, ybot = FRA_FULL_Y + FRA_FULL_H - 22;
    int h = ybot - ytop;
    int px = FRA_FULL_X + FRA_PLOT_L, pw = FRA_FULL_W - FRA_PLOT_L;
    u16 i;

    fra_pane(fb, FRA_FULL_X, FRA_FULL_Y, FRA_FULL_W, FRA_FULL_H,
             (a->y_db != 0u) ? "幅频响应 / dB" : "幅频响应 / 线性");
    if (a->y_db != 0u) {                       /* 0dB 参考线 */
        int y0 = ytop + (int)((20.0 / 80.0) * (double)h);

        lcd_hline(fb, px, px + pw - 1, y0, UI_OSC_COLOR_DIM);
    }
    /* 拟合曲线(白线)先画, 实测点压在上面 */
    if (a->fit_ok != 0u) {
        int lx = 0, ly = 0, have = 0;

        for (i = 0; (i < a->npt) && (i < UI_FRA_MAX_PTS); i++) {
            double f = (double)a->f_start_hz + (double)i * (double)a->step_hz;
            int x = fra_x_of_f(f, px, pw);
            int y = fra_mag_y(a, (double)a->fit_mag[i], ytop, ybot, h);

            if (have != 0) { fra_line(fb, lx, ly, x, y, UI_OSC_COLOR_TEXT); }
            lx = x; ly = y; have = 1;
        }
    }
    for (i = 0; (i < a->npt) && (i < UI_FRA_MAX_PTS); i++) {
        double f = (double)a->f_start_hz + (double)i * (double)a->step_hz;
        int x = fra_x_of_f(f, px, pw);
        int y = fra_mag_y(a, (double)a->habs[i], ytop, ybot, h);

        lcd_fill_rect(fb, x - 1, y - 1, 3, 3, UI_OSC_COLOR_CH1);   /* ★ 圆点(3x3 实心) */
    }
    fra_axes_freq(fb, ybot);
    if (a->y_db != 0u) {
        fra_axes_db(fb, ytop, h);
    } else {
        fra_axes_lin(fb, ytop, ybot, h);
    }
    /* ★ 截止频率标记: 一条 -3dB 横线 + 曲线上的交点 + 频率数值(用户要求) */
    if (a->fc_ok != 0u) {
        int yl = fra_mag_y(a, (double)a->fc_lvl, ytop, ybot, h);
        int xf = fra_x_of_f((double)a->fc_hz, px, pw);
        char fs[24];
        int tw;

        if (yl < ytop) { yl = ytop; }
        if (yl > ybot) { yl = ybot; }
        lcd_hline(fb, px, px + pw - 1, yl, UI_OSC_COLOR_LABEL);      /* -3dB 横线 */
        if ((xf >= px) && (xf < (px + pw))) {                        /* 交点标记(空心圈) */
            lcd_rect(fb, xf - 3, yl - 3, 7, 7, UI_OSC_COLOR_CH2);
        }
        (void)fra_fmt_hz1(fs, (s32)(a->fc_hz * 10));   /* XXFC1D: 改用 0.1kHz 口径, 与【光标读数】一致(原来用 fra_fmt_hz 只到 1kHz -> 标注"20kHz"而光标"20.5kHz", 看起来仍对不上) */
        {   /* 数值贴在横线右端上方(不压曲线) */
            char lab[32];

            (void)sprintf(lab, "fc %s", fs);
            tw = (int)ui_text_size(lab, 1);
            (void)ui_draw_text(fb, px + pw - tw - 6, yl - 18, lab, UI_OSC_COLOR_LABEL, 1);
        }
    }
    fra_draw_tf(fb, a, 0, px, pw, ytop);      /* ★ 传递函数框(边框 = 幅频波形色 CH1) */
    fra_draw_cursor(fb, a, ytop, ybot, px, pw, 0);
}

static void fra_draw_phase(u32 fb, const ui_fra_t *a)
{
    cur_bak_x = -1;   /* 曲线区被整块重画: 备份作废(否则会把旧光标/旧读数贴回来) */
    int ytop = FRA_FULL_Y + 26, ybot = FRA_FULL_Y + FRA_FULL_H - 22;
    int h = ybot - ytop;
    int px = FRA_FULL_X + FRA_PLOT_L, pw = FRA_FULL_W - FRA_PLOT_L;
    u16 i;

    fra_pane(fb, FRA_FULL_X, FRA_FULL_Y, FRA_FULL_W, FRA_FULL_H, "相频响应 / 度");
    lcd_hline(fb, px, px + pw - 1, ytop + h / 2, UI_OSC_COLOR_DIM);
    if (a->fit_ok != 0u) {
        int lx = 0, ly = 0, have = 0;

        for (i = 0; (i < a->npt) && (i < UI_FRA_MAX_PTS); i++) {
            double f = (double)a->f_start_hz + (double)i * (double)a->step_hz;
            int x = fra_x_of_f(f, px, pw);
            int y = fra_pha_y((double)a->fit_pha[i], ytop, h);

            if (have != 0) { fra_line(fb, lx, ly, x, y, UI_OSC_COLOR_TEXT); }
            lx = x; ly = y; have = 1;
        }
    }
    for (i = 0; (i < a->npt) && (i < UI_FRA_MAX_PTS); i++) {
        double f = (double)a->f_start_hz + (double)i * (double)a->step_hz;
        int x = fra_x_of_f(f, px, pw);
        int y = fra_pha_y((double)a->hpha[i], ytop, h);

        lcd_fill_rect(fb, x - 1, y - 1, 3, 3, UI_OSC_COLOR_CH2);   /* ★ 圆点(3x3 实心) */
    }
    fra_axes_freq(fb, ybot);
    fra_axes_deg(fb, ytop, h);
    fra_draw_tf(fb, a, 1, px, pw, ytop);      /* ★ 传递函数框(边框 = 相频波形色 CH2) */
    fra_draw_cursor(fb, a, ytop, ybot, px, pw, 1);
}

/* 横轴: 时间刻度 0 / T/4 / T/2 / 3T/4 / T,  T = N/(2*f_max) */
static void fra_axes_time(u32 fb, int ybot, s32 fmax_hz)
{
    int k;

    if (fmax_hz <= 0) { return; }
    for (k = 0; k <= 4; k++) {
        double t_us = (double)k * (double)UI_FRA_IFFT_N * 1.0e6 / (8.0 * (double)fmax_hz);
        int x = FRA_FULL_X + FRA_PLOT_L +
                (int)((double)k * (double)(FRA_FULL_W - FRA_PLOT_L) / 4.0);
        char s[16];
        int tw;

        if (t_us < 1000.0) { (void)sprintf(s, "%.0fus", t_us); }
        else               { (void)sprintf(s, "%.2fms", t_us / 1000.0); }
        tw = (int)ui_text_size(s, 1);
        if ((x - tw / 2) < 2) { x = 2 + tw / 2; }
        if ((x + tw / 2) > (LCD_GFX_WIDTH - 2)) { x = LCD_GFX_WIDTH - 2 - tw / 2; }
        lcd_vline(fb, x, ybot, ybot + 4, FRA_DIM);
        (void)ui_draw_text(fb, x - tw / 2, ybot + 6, s, UI_OSC_COLOR_LABEL, 1);
    }
}

/* 冲激/阶跃(整屏): 曲线来自 PL 逆 FFT(ui_fra_t.imp[]/stp[]), 每列取 min/max 画竖线 */
/* ================= ★ 拟合传递函数显示框(幅频/相频页右上角) =================
   内容(方案 A+C): 传递函数形式 + 4 个系数(科学计数法) + 一对共轭极点。
   边框颜色 = 该页波形颜色(幅频 = CH1, 相频 = CH2), 与打点同色。
   必须在 fra_draw_cursor() 之前调用: 游标是 save-under/restore, 后画才不会把框抹掉。 */
#define FRA_TF_W   264
#define FRA_TF_H   74
#define FRA_TF_LH  16

static void fra_draw_tf(u32 fb, const ui_fra_t *a, int is_phase, int px, int pw, int ytop)
{
    char l1[40], l2[40], l3[40], l4[40];
    u16  col = (is_phase != 0) ? UI_OSC_COLOR_CH2 : UI_OSC_COLOR_CH1;
    int  x, y;

    if (a->tf_ok == 0u) { return; }

    x = px + pw - FRA_TF_W - 24;      /* ★ 再往左一点(用户要求), 右边缘留 24px */
    y = ytop + 4;
    if (x < px) { x = px; }

    lcd_fill_rect(fb, x, y, FRA_TF_W, FRA_TF_H, UI_OSC_COLOR_INFO);   /* 底色(不透明, 压住曲线) */
    lcd_rect(fb, x, y, FRA_TF_W, FRA_TF_H, col);                      /* ★ 边框 = 该页波形色 */

    (void)sprintf(l1, "H(s)=(1+b1s+b0s^2)/(1+a1s+a0s^2)");
    (void)sprintf(l2, "b0=%.2e b1=%.2e", (double)a->tf_b0, (double)a->tf_b1);
    (void)sprintf(l3, "a0=%.2e a1=%.2e", (double)a->tf_a0, (double)a->tf_a1);
    if (a->tf_pi >= 0.0f) {
        (void)sprintf(l4, "p=%.2e+j%.2e", (double)a->tf_pr, (double)a->tf_pi);
    } else {
        (void)sprintf(l4, "p=%.2e-j%.2e", (double)a->tf_pr, (double)-a->tf_pi);
    }

    (void)ui_draw_text(fb, x + 5, y + 3,               l1, UI_OSC_COLOR_TEXT, 1);
    (void)ui_draw_text(fb, x + 5, y + 3 + FRA_TF_LH,   l2, UI_OSC_COLOR_TEXT, 1);
    (void)ui_draw_text(fb, x + 5, y + 3 + FRA_TF_LH*2, l3, UI_OSC_COLOR_TEXT, 1);
    (void)ui_draw_text(fb, x + 5, y + 3 + FRA_TF_LH*3, l4, UI_OSC_COLOR_LABEL, 1);
}

static void fra_draw_time(u32 fb, const ui_fra_t *a, int is_step)
{
    /* ★ 只显示记录的前一段: 冲激/阶跃的有用部分都在开头, 整段铺满会把它们压扁。
   改这一个数即可(单位=采样点, 122ns/点): 4096 点 ≈ 0.5ms。 */
#define FRA_TIME_SHOW_N 512
    int nshow = FRA_TIME_SHOW_N;

    if (nshow > (int)UI_FRA_IFFT_N) { nshow = (int)UI_FRA_IFFT_N; }
    if (nshow < 2) { nshow = 2; }
    const float *v = (is_step != 0) ? a->stp : a->imp;
    int ytop = FRA_FULL_Y + 26, ybot = FRA_FULL_Y + FRA_FULL_H - 22;
    int h = ybot - ytop;
    int px = FRA_FULL_X + FRA_PLOT_L, pw = FRA_FULL_W - FRA_PLOT_L;
    float mn, mx;
    int i, x;
    char s[24];

    {   /* 标题带带宽与记录长度(整数打印, 不用 %f) */
        char ttl[96];
        int fk = (a->imp_fmax_hz > 0) ? (int)(a->imp_fmax_hz / 1000) : 0;
        int tk = (a->imp_fmax_hz > 0)
               ? (int)((double)nshow * 1000000.0 / (2.0 * (double)a->imp_fmax_hz) + 0.5)
               : 0;

        if (tk >= 1000) {
            (void)sprintf(ttl, "%s 带宽%d.%03dMHz 记录%d.%02dms %%h%d H0%d pk%d",
                          is_step ? "阶跃响应 / V" : "冲激响应 / V",
                          fk / 1000, fk % 1000, tk / 1000, (tk % 1000) / 10,
                          (int)(a->dbg_sumh * 1000.0f), (int)(a->dbg_h0dc * 1000.0f),
                          (int)a->dbg_peak_n);
        } else {
            (void)sprintf(ttl, "%s 带宽%d.%03dMHz 记录%dus %%h%d H0%d pk%d",
                          is_step ? "阶跃响应 / V" : "冲激响应 / V",
                          fk / 1000, fk % 1000, tk,
                          (int)(a->dbg_sumh * 1000.0f), (int)(a->dbg_h0dc * 1000.0f),
                          (int)a->dbg_peak_n);
        }
        fra_pane(fb, FRA_FULL_X, FRA_FULL_Y, FRA_FULL_W, FRA_FULL_H, ttl);
    }
    /* 时间轴按"只显示前 nshow 点"换算: 传给坐标轴一个等价的 fmax -> 它内部算出的 T 就是 nshow 那一段 */
    /* ★ 先除后乘: 原来 (double)fmax * (double)16384 再转 s32 会溢出(16.7e9 > 2^31) -> 传进负数 -> fra_axes_time 直接 return -> 时间刻度一个都不画。 */
    fra_axes_time(fb, ybot, (s32)((double)a->imp_fmax_hz * ((double)UI_FRA_IFFT_N / (double)nshow)));
    if ((a->ifft_ok == 0u) || (a->npt == 0u)) {
        return;                                /* 还没算出来: 只留格线 */
    }
    /* ★ 纵轴量程: 计算完成时"自动设置"过一次(见 helloworld.c 的 fra_solve),
       这里直接用存好的范围, 显示期间不再变动 —— 刻度单位仍然是 V。 */
    if (is_step != 0) {
        mn = a->stp_lo;
        mx = a->stp_hi;
    } else {
        mn = a->imp_lo;
        mx = a->imp_hi;
    }
    /* ★ 纵轴量程取【实测 + 模型】的并集(用户选择 a): 两条曲线都要完整可见, 不会被截断 */
    if (a->fit_time_ok != 0u) {
        float l2 = (is_step != 0) ? a->stp2_lo : a->imp2_lo;
        float h2 = (is_step != 0) ? a->stp2_hi : a->imp2_hi;

        if (l2 < mn) { mn = l2; }
        if (h2 > mx) { mx = h2; }
    }
    if ((mx - mn) < 1e-9f) { mx = mn + 1.0f; }
    if ((mx - mn) < 1e-12f) { mx = mn + 1.0f; }
    {
        float d = (mx - mn) * 0.05f;           /* 上下留 5% 余量 */

        mn -= d;
        mx += d;
    }
    for (x = 0; x < pw; x++) {
        int i0 = (int)((double)x * (double)nshow / (double)pw);
        int i1 = (int)((double)(x + 1) * (double)nshow / (double)pw);
        float lo, hi;
        int ya, yb, t2;

        if (i1 <= i0) { i1 = i0 + 1; }
            if (i1 > nshow) { i1 = nshow; }
        lo = hi = v[i0];
        for (i = i0 + 1; i < i1; i++) {
            if (v[i] < lo) { lo = v[i]; }
            if (v[i] > hi) { hi = v[i]; }
        }
        ya = ybot - (int)(((double)hi - (double)mn) / ((double)mx - (double)mn) * (double)h);
        yb = ybot - (int)(((double)lo - (double)mn) / ((double)mx - (double)mn) * (double)h);
        if (ya < ytop) { ya = ytop; }
        if (yb > ybot) { yb = ybot; }
        if (yb < ya) { t2 = ya; ya = yb; yb = t2; }
        lcd_vline(fb, px + x, ya, yb, UI_OSC_COLOR_CH1);
    }
    /* ★ 叠加 PL 二阶模型的拟合曲线(白色): 逐列取 min/max 的算法与上面完全一致,
       只把数据源换成 imp2[]/stp2[], 颜色换成 TEXT -> 两条曲线同一坐标映射, 可以直接对比。 */
    if (a->fit_time_ok != 0u) {
        const float *v2 = (is_step != 0) ? a->stp2 : a->imp2;
        int pya = 0, pyb = 0;          /* ★ 上一列的上下沿, 用来把相邻列连起来 */

        for (x = 0; x < pw; x++) {
            int i0 = (int)((double)x * (double)nshow / (double)pw);
            int i1 = (int)((double)(x + 1) * (double)nshow / (double)pw);
            float lo, hi;
            int ya, yb, t2;

            if (i1 <= i0) { i1 = i0 + 1; }
            if (i1 > nshow) { i1 = nshow; }
            lo = hi = v2[i0];
            for (i = i0 + 1; i < i1; i++) {
                if (v2[i] < lo) { lo = v2[i]; }
                if (v2[i] > hi) { hi = v2[i]; }
            }
            ya = ybot - (int)(((double)hi - (double)mn) / ((double)mx - (double)mn) * (double)h);
            yb = ybot - (int)(((double)lo - (double)mn) / ((double)mx - (double)mn) * (double)h);
            if (ya < ytop) { ya = ytop; }
            if (yb > ybot) { yb = ybot; }
            if (yb < ya) { t2 = ya; ya = yb; yb = t2; }
            /* ★ 与上一列连线: 只画单列竖线时, 斜率大的地方相邻列只有一个像素、对角不相接,
               看上去就是"一点一点断开的"(实测现象)。连上沿与下沿两个端点即可连续。 */
            if (x > 0) {
                fra_line(fb, px + x - 1, pya, px + x, ya, UI_OSC_COLOR_TEXT);
                fra_line(fb, px + x - 1, pyb, px + x, yb, UI_OSC_COLOR_TEXT);
            }
            lcd_vline(fb, px + x, ya, yb, UI_OSC_COLOR_TEXT);
            pya = ya;
            pyb = yb;
        }
    }
    {   /* ★ 刻度按 V 打印(整数技巧, 不用 %f) */
        int mv = (int)((double)mx * 1000.0 + 0.5);
        int sg = (mv < 0) ? -1 : 1;

        if (sg < 0) { mv = -mv; }
        (void)sprintf(s, "%s%d.%02dV", (sg < 0) ? "-" : "", mv / 1000, (mv % 1000) / 10);
        fra_y_tick(fb, s, ytop);
        mv = (int)((double)mn * 1000.0 + (((double)mn < 0.0) ? -0.5 : 0.5));
        sg = (mv < 0) ? -1 : 1;
        if (sg < 0) { mv = -mv; }
        (void)sprintf(s, "%s%d.%02dV", (sg < 0) ? "-" : "", mv / 1000, (mv % 1000) / 10);
        fra_y_tick(fb, s, ybot);
    }
    if ((mn < 0.0f) && (mx > 0.0f)) {          /* 0 参考线 */
        int y0 = ybot - (int)((0.0 - (double)mn) / ((double)mx - (double)mn) * (double)h);

        lcd_hline(fb, px, px + pw - 1, y0, UI_OSC_COLOR_DIM);
    }
    if (is_step != 0) {                        /* 阶跃终值 = 直流增益 */
        int yf = ybot - (int)(((double)v[nshow - 1] - (double)mn) /
                              ((double)mx - (double)mn) * (double)h);

        lcd_hline(fb, px, px + pw - 1, yf, UI_OSC_COLOR_DIM);
    }
}

/* ---------------- 曲线选择弹窗(模态) ---------------- */
#define FRA_PK_X    200
#define FRA_PK_Y    120
#define FRA_PK_W    400
#define FRA_PK_RH   44
#define FRA_PK_N    4

static void fra_draw_pick(u32 fb, const ui_fra_t *a)
{
    int i;
    int h = FRA_PK_N * FRA_PK_RH;

    lcd_fill_rect(fb, FRA_PK_X, FRA_PK_Y, FRA_PK_W, h, UI_OSC_COLOR_INFO);
    lcd_rect(fb, FRA_PK_X, FRA_PK_Y, FRA_PK_W, h, UI_OSC_COLOR_FRAME);
    for (i = 0; i < FRA_PK_N; i++) {
        int ry = FRA_PK_Y + i * FRA_PK_RH;

        if ((u8)i == a->view) {                /* 当前曲线高亮 */
            lcd_fill_rect(fb, FRA_PK_X + 2, ry + 2, FRA_PK_W - 4, FRA_PK_RH - 4,
                          UI_OSC_COLOR_TABBG);
        }
        (void)ui_draw_text(fb, FRA_PK_X + 24, ry + 14, fra_view_name((u8)i),
                           ((u8)i == a->view) ? UI_OSC_COLOR_CH1 : UI_OSC_COLOR_TEXT, 1);
        if (i > 0) {
            lcd_hline(fb, FRA_PK_X + 4, FRA_PK_X + FRA_PK_W - 5, ry, UI_OSC_COLOR_GRID);
        }
    }
    /* 整行 flush(起止都 32 字节对齐): 每行 800*3=2400 字节 = 75*32 */
    Xil_DCacheFlushRange((UINTPTR)(fb + (u32)FRA_PK_Y * LCD_GFX_WIDTH * 3u),
                         (u32)h * LCD_GFX_WIDTH * 3u);
}

int ui_fra_pick_touch(u32 fb, ui_app_t *a, int ev, int x, int y)
{
    int i;

    (void)fb;                 /* 弹窗不在触摸里写屏: 关闭后由帧边界整屏重画 */
    if (a->screen != UI_SCREEN_FRA) { return 0; }
    if (ev != TOUCH_EV_UP) { return 1; }                 /* 模态: 按下/移动一律吃掉 */
    i = (y - FRA_PK_Y) / FRA_PK_RH;
    if ((x >= FRA_PK_X) && (x < (FRA_PK_X + FRA_PK_W)) &&
        (y >= FRA_PK_Y) && (i >= 0) && (i < FRA_PK_N)) {
        a->fra.view = (u8)i;                             /* 选中要看的曲线 */
    }
    a->fra.pick_open = 0u;
    a->fra.redraw_pane = 1u;     /* 只重画曲线区(整屏重画会闪一下) */
    a->fra.redraw_bar  = 1u;     /* ★ 曲线名变了: 按钮栏标签必须在帧边界跟着刷, 否则永远慢一拍 */
    return 1;
}

void ui_fra_default(ui_fra_t *a)
{
    /* ★ 必须先 memset 再赋初值！
       原来这里把 a->view / a->pick_open 写在 memset 之前 -> 会被清零覆盖
       (view=0 恰好等于要的值, 所以一直没暴露; 但下一个改初值的人一定会踩)。
       ui_filter.c 里已把这一点写成反面教材。 */
    (void)memset(a, 0, sizeof(*a));
    a->view      = 0u;                    /* 默认显示幅频响应 */
    a->pick_open = 0u;
    a->f_start_hz = 1000;                   /* ★ 起始频率下限 1kHz */
    a->f_stop_hz  = 1000000;
    a->step_hz    = 500;
    a->vpp_mv     = 2000;
    a->avg        = 4;
    a->order      = 2;
    a->y_db       = 1;
    a->warn_s     = 60;
}

/* ★ 只画顶栏 + 信息行(按钮动作后走这里, 不整屏重画 -> 不闪) */
void ui_fra_draw_chrome(u32 fb, ui_fra_t *a)
{

    /* 顶栏 */
    lcd_fill_rect(fb, 0, 0, LCD_GFX_WIDTH, UI_OSC_HEADER_H, UI_OSC_COLOR_BAR);
    (void)ui_draw_text(fb, 12, 8, "频率特性分析仪", UI_OSC_COLOR_TEXT, 2);
    {   /* 顶栏右侧: 扫频进度 + 当前频率(用整数打印, 不用串口) */
        char st[64];

        if (a->running != 0u) {
            s32 np = (a->step_hz > 0) ? ((a->f_stop_hz - a->f_start_hz) / a->step_hz + 1) : 0;

            (void)sprintf(st, "扫频中 %4d/%4d  %6d kHz", (int)a->npt, (int)np,
                          (int)(a->f_meas_hz / 1000));
        } else if ((a->npt > 0u) && (a->fit_ok != 0u)) {
            /* ★ 不用 %f: newlib 的浮点 printf 往往没开, 会打出空串 */
            int p1 = (int)(a->fit_res * 100.0f);
            int p2 = (int)(a->fit_res * 10000.0f) - p1 * 100;

            if (p2 < 0) { p2 = -p2; }
            /* ★ 后面两个数是"同帧自检": PL = PL FFT 算出的相位, PS = PS 端单点 DFT 算出的相位
               (同一帧同样的数据)。两个数应该基本相等; 若符号相反, 就说明 PL FFT 的实/虚取反了。 */
            (void)sprintf(st, "完成 拟合%d阶 残差%d.%02d%%", (int)a->fit_order, p1, p2);
        } else {
            (void)strcpy(st, (a->npt > 0u) ? "扫描完成" : "待机");
        }
        (void)ui_draw_text(fb, 520, 16, st, UI_OSC_COLOR_LABEL, 1);
    }
    lcd_hline(fb, 0, LCD_GFX_WIDTH - 1, UI_OSC_HEADER_H - 1, UI_OSC_COLOR_FRAME);

    /* ---- 信息行: 8 段"标签+数值", 段间细竖线分隔, 按实际字宽整行居中 ----
       标签用浅灰(弱化), 数值用白(突出); "预计"超预警阈值时用黄色提醒。 */
    lcd_fill_rect(fb, 0, UI_OSC_INFO_Y, LCD_GFX_WIDTH, UI_OSC_INFO_H, UI_OSC_COLOR_INFO);
    {
        char h1[16], h2[16], rng[40];
        char v[8][24];
        const char *lab[8];
        u16 col[8];
        s32 npt = (a->step_hz > 0) ? ((a->f_stop_hz - a->f_start_hz) / a->step_hz + 1) : 0;
        int est = ui_fra_est_sec(a);
        int i, tw = 0, x;

        fra_fmt_hz(h1, a->f_start_hz);
        fra_fmt_hz(h2, a->f_stop_hz);
        (void)sprintf(rng, "%s~%s", h1, h2);

        lab[0] = "扫频"; (void)strcpy(v[0], rng);
        lab[1] = "步长"; (void)sprintf(v[1], "%dHz", (int)a->step_hz);
        lab[2] = "点数"; (void)sprintf(v[2], "%d", (int)npt);
        lab[3] = "平均"; (void)sprintf(v[3], "x%d", (int)a->avg);
        lab[4] = "幅度"; (void)sprintf(v[4], "%d.%03dVpp", (int)(a->vpp_mv / 1000), (int)(a->vpp_mv % 1000));
        lab[5] = "阶数"; (void)sprintf(v[5], "%d", (int)a->order);
        lab[6] = "纵轴"; (void)strcpy(v[6], (a->y_db != 0u) ? "dB" : "线性");
        lab[7] = "预计"; (void)sprintf(v[7], "%ds", est);
        for (i = 0; i < 8; i++) {
            col[i] = UI_OSC_COLOR_TEXT;
            if (i == 7) {
                if (a->warn_s == 0) { col[i] = UI_OSC_COLOR_LABEL; }
                else if (est > (int)a->warn_s) { col[i] = UI_OSC_COLOR_CH1; }
            }
        }
        for (i = 0; i < 8; i++) {                       /* 先量总宽, 再居中 */
            tw += (int)ui_text_size(lab[i], 1) + 4 + (int)ui_text_size(v[i], 1);
            if (i != 7) { tw += 12; }
        }
        x = (LCD_GFX_WIDTH - tw) / 2;
        if (x < 8) { x = 8; }
        for (i = 0; i < 8; i++) {
            (void)ui_draw_text(fb, x, UI_OSC_INFO_Y + 4, lab[i], UI_OSC_COLOR_LABEL, 1);
            x += (int)ui_text_size(lab[i], 1) + 4;
            (void)ui_draw_text(fb, x, UI_OSC_INFO_Y + 4, v[i], col[i], 1);
            x += (int)ui_text_size(v[i], 1);
            if (i != 7) {
                x += 6;
                lcd_vline(fb, x, UI_OSC_INFO_Y + 6, UI_OSC_INFO_Y + UI_OSC_INFO_H - 7,
                          UI_OSC_COLOR_TABBG);
                x += 6;
            }
        }
    }
    lcd_hline(fb, 0, LCD_GFX_WIDTH - 1, UI_OSC_INFO_Y + UI_OSC_INFO_H - 1, UI_OSC_COLOR_FRAME);
}

/* ================= 用时预警确认弹窗(模态) ================= */
#define FRA_CF_X   200
#define FRA_CF_Y   160
#define FRA_CF_W   400
#define FRA_CF_H   150
#define FRA_CF_BY  (FRA_CF_Y + 96)      /* 按钮行 */

static void fra_draw_confirm(u32 fb, const ui_fra_t *a)
{
    int est = ui_fra_est_sec(a);
    s32 npt = (a->step_hz > 0) ? ((a->f_stop_hz - a->f_start_hz) / a->step_hz + 1) : 0;
    char b1[48], b2[56];
    int band_y = FRA_CF_Y - 4;
    int band_h = FRA_CF_H + 8;

    (void)sprintf(b1, "本次测量预计用时 %d 秒", est);
    (void)sprintf(b2, "步长%dHz 平均x%d 共%d点", (int)a->step_hz, (int)a->avg, (int)npt);

    lcd_fill_rect(fb, FRA_CF_X, FRA_CF_Y, FRA_CF_W, FRA_CF_H, UI_OSC_COLOR_INFO);
    lcd_rect(fb, FRA_CF_X, FRA_CF_Y, FRA_CF_W, FRA_CF_H, UI_OSC_COLOR_FRAME);
    lcd_rect(fb, FRA_CF_X + 1, FRA_CF_Y + 1, FRA_CF_W - 2, FRA_CF_H - 2, UI_OSC_COLOR_FRAME);
    (void)ui_draw_text(fb, FRA_CF_X + 28, FRA_CF_Y + 22, b1, UI_OSC_COLOR_TEXT, 1);
    (void)ui_draw_text(fb, FRA_CF_X + 28, FRA_CF_Y + 48, b2, UI_OSC_COLOR_LABEL, 1);
    /* 两个按钮: 确定 / 取消 */
    lcd_fill_rect(fb, FRA_CF_X + 60, FRA_CF_BY, 110, 32, UI_OSC_COLOR_BAR);
    lcd_fill_rect(fb, FRA_CF_X + 230, FRA_CF_BY, 110, 32, UI_OSC_COLOR_BAR);
    (void)ui_draw_text(fb, FRA_CF_X + 96, FRA_CF_BY + 8, "确定", UI_OSC_COLOR_TEXT, 1);
    (void)ui_draw_text(fb, FRA_CF_X + 266, FRA_CF_BY + 8, "取消", UI_OSC_COLOR_TEXT, 1);
    /* ★ 按整行 flush(起止都 32 字节对齐), 不做非对齐 flush */
    Xil_DCacheFlushRange((UINTPTR)(fb + (u32)band_y * LCD_GFX_WIDTH * 3u),
                         (u32)band_h * LCD_GFX_WIDTH * 3u);
}

int ui_fra_confirm_touch(u32 fb, ui_app_t *a, int ev, int x, int y)
{
    (void)fb;                     /* 弹窗不在触摸里写屏(关闭后由帧边界整屏重画) */
    if (a->screen != UI_SCREEN_FRA) { return 0; }
    if (ev != TOUCH_EV_UP) { return 1; }                 /* 模态: 按下/移动一律吃掉 */
    if ((y >= FRA_CF_BY) && (y <= (FRA_CF_BY + 32))) {
        if ((x >= (FRA_CF_X + 60)) && (x <= (FRA_CF_X + 170))) {          /* 确定 */
            a->fra.running = 1u;
            a->fra.confirm_open = 0u;
            a->fra.redraw_pane = 1u;                          /* 帧边界整屏重画(不在触摸里写整屏) */
            return 1;
        }
        if ((x >= (FRA_CF_X + 230)) && (x <= (FRA_CF_X + 340))) {         /* 取消 */
            a->fra.confirm_open = 0u;
            ui_app_redraw_req();
            return 1;
        }
    }
    return 1;                                             /* 点弹窗其它地方: 无反应(模态) */
}
/* ★ 只重画"曲线区"(曲线 + 格线 + 边框), 不动顶栏/信息行/按钮栏。
   弹窗关闭时用它代替整屏重画 —— 整屏要 lcd_clear 1.2MB 帧存再重画,
   超过一帧时间屏幕上就会看到"闪一下"。 */
static void fra_redraw_pane(u32 fb, const ui_fra_t *a)
{
    lcd_fill_rect(fb, FRA_FULL_X, FRA_FULL_Y, FRA_FULL_W, FRA_FULL_H, UI_OSC_COLOR_BG);
    switch (a->view) {
    case 1:  fra_draw_phase(fb, a);      break;
    case 2:  fra_draw_time(fb, a, 0);    break;
    case 3:  fra_draw_time(fb, a, 1);    break;
    default: fra_draw_mag(fb, a);        break;
    }
    ui_fra_flush(fb);                    /* 只 flush 曲线区那一段(72..439) */
}

void ui_fra_draw(u32 fb, ui_fra_t *a)
{
    lcd_clear(fb, UI_OSC_COLOR_BG);
    ui_fra_draw_chrome(fb, a);


    if (a->set_open != 0u) {
        fra_draw_settings(fb, (const ui_fra_t *)a);
        return;
    }
    if (a->confirm_open != 0u) {
        fra_draw_confirm(fb, (const ui_fra_t *)a);   /* 画在曲线之上 */
        return;
    }
    /* ★ 一屏只画一条曲线, 由"曲线"按钮切换(默认幅频) */
    switch (a->view) {
    case 1:  fra_draw_phase(fb, (const ui_fra_t *)a);          break;
    case 2:  fra_draw_time(fb, (const ui_fra_t *)a, 0);        break;
    case 3:  fra_draw_time(fb, (const ui_fra_t *)a, 1);        break;
    default: fra_draw_mag(fb, (const ui_fra_t *)a);            break;
    }

    if (a->pick_open != 0u) {
        fra_draw_pick(fb, (const ui_fra_t *)a);                /* 选择弹窗画在曲线之上 */
    }
}

/* ★ 供"数字键盘窗口层"在收起键盘后调用:
   键盘收起时窗口层会把"弹键盘那一刻"的底板原样贴回 —— 那张图里是【旧数值】,
   如果没有人重画设置页, 新输入的值就被盖住了(用户看到"输入完什么反应都没有")。
   频谱仪那条路径是在 ui_app_kbd_refresh 里重画它的第 3/4 行, FRA 没有对应代码, 所以补这个。 */
void ui_fra_redraw_settings(u32 fb, ui_fra_t *a)
{
    if (a->set_open == 0u) {
        return;
    }
    lcd_fill_rect(fb, FRA_FULL_X, FRA_FULL_Y, FRA_FULL_W, FRA_FULL_H, UI_OSC_COLOR_BG);
    fra_draw_settings(fb, (const ui_fra_t *)a);      /* 内部自带整页 flush */
    /* ★ 顶栏(信息行)也要刷: 起始/终止频率变了, "1kHz ~ 1.000MHz" 那段必须跟着变,
       否则用户看到的就是"输完了什么都没变"。这一段在窗口层收键盘时被调用,
       不受主循环 else-if 链(ui_app_win_open()==1 时进不去 ui_app_draw_live)的限制。 */
    ui_fra_draw_chrome(fb, a);              /* 该函数签名是非 const, 直接传 */
    ui_fra_flush_chrome(fb);
}

void ui_fra_draw_live(u32 fb, ui_fra_t *a)
{
    /* ★ 扫频新点: 只重画"曲线区 + 顶部进度条", 绝不能走 ui_fra_draw() 的整屏 lcd_clear ——
       按钮栏是应用层画的, 整屏清屏会把它擦掉且没人补画(就是"点运行后按钮全没了"),
       而且每来一个点清一次屏, 屏幕上就是持续闪烁。 */
    /* ★ redraw_pane 必须【最先】处理: 它是把整个曲线区铺底重画, 如果排在弹窗/设置页
       之后, 就会把刚画好的子窗口擦掉(实测: "曲线选择"窗弹出后立刻消失就是这个原因)。 */
    if (a->redraw_pane != 0u) {
        a->redraw_pane = 0u;
        fra_redraw_pane(fb, (const ui_fra_t *)a);
    }
    if (a->redraw_curve != 0u) {
        a->redraw_curve = 0u;
        fra_redraw_pane(fb, (const ui_fra_t *)a);      /* 曲线区(含 ui_fra_flush) */
        ui_fra_draw_chrome(fb, a);                     /* 顶栏: 进度 + 当前频率 */
        ui_fra_flush_chrome(fb);
        if (a->pick_open != 0u) {
            fra_draw_pick(fb, (const ui_fra_t *)a);    /* 弹窗开着就画回去 */
        }
    }
    /* ★ 打开弹窗: 只把弹窗画在曲线上 + 只 flush 它那一段(整行对齐), 不走整屏重画 */
    if (a->draw_popup != 0u) {
        a->draw_popup = 0u;
        if (a->pick_open != 0u) {
            fra_draw_pick(fb, (const ui_fra_t *)a);      /* 内部自带整行 flush */
        }
    }
    /* ★ 设置页: 只覆盖"曲线区"(顶栏/信息行/按钮栏一律不动, 所以不会闪)。
       设置页自己每行铺底(72..423), 剩下的 424..438 这 15 行由这里的整块铺底补上,
       否则曲线会残留在下面; 它内部也自带 flush(0..453 行)。 */
    if (a->draw_set != 0u) {
        a->draw_set = 0u;
        if (a->set_open != 0u) {
            lcd_fill_rect(fb, FRA_FULL_X, FRA_FULL_Y, FRA_FULL_W, FRA_FULL_H, UI_OSC_COLOR_BG);
            fra_draw_settings(fb, (const ui_fra_t *)a);
        }
    }
    /* ★ 用时确认窗: 只把它画在曲线上(它内部自带整行 flush), 不整屏重画 */
    if (a->draw_confirm != 0u) {
        a->draw_confirm = 0u;
        if (a->confirm_open != 0u) {
            fra_draw_confirm(fb, (const ui_fra_t *)a);
        }
    }
    /* ★ 只移动光标: 擦旧画新(不重画曲线区 -> 不闪, 读数跟着更新) */
    if (a->cur_draw != 0u) {
        a->cur_draw = 0u;
        ui_fra_cursor_refresh(fb, a);
    }

    /* ★ 关闭弹窗: 只重画曲线区 */
}

void ui_fra_flush(u32 fb)
{
    Xil_DCacheFlushRange((UINTPTR)(fb + ((u32)FRA_Y0 * LCD_GFX_WIDTH * 3u)),
                         ((u32)(FRA_Y3 - FRA_Y0) * LCD_GFX_WIDTH * 3u));
}

void ui_fra_flush_chrome(u32 fb)
{
    Xil_DCacheFlushRange((UINTPTR)fb,
                         ((u32)(UI_OSC_INFO_Y + UI_OSC_INFO_H) * LCD_GFX_WIDTH * 3u));
}
/* ★ 扫频子窗口: 运行中显示"正在运行......", 结束后显示"运行结束"(1 秒后由主循环关掉)。
   位置固定在曲线区正中; 自己整行 flush(起止都按整行对齐, 单缓冲屏上要等帧起点才调用)。 */
void ui_fra_run_win_draw(u32 fb, const ui_fra_t *a, int finished)
{
    int w = 360, h = 96;
    int x = (LCD_GFX_WIDTH - w) / 2;
    int y = FRA_FULL_Y + (FRA_FULL_H - h) / 2;
    const char *s = (finished != 0) ? "运行结束" : "正在运行......";

    (void)a;
    lcd_fill_rect(fb, x, y, w, h, UI_OSC_COLOR_INFO);
    lcd_rect(fb, x, y, w, h, UI_OSC_COLOR_FRAME);
    (void)ui_draw_text(fb, x + (w - (int)ui_text_size(s, 2)) / 2, y + (h - 32) / 2,
                       s, (finished != 0) ? UI_OSC_COLOR_TEXT : UI_OSC_COLOR_CH1, 2);
    Xil_DCacheFlushRange((UINTPTR)(fb + (u32)y * LCD_GFX_WIDTH * 3u),
                         (u32)h * LCD_GFX_WIDTH * 3u);
}
/* ★ 光标: 竖直虚线 + 曲线上的标记 + 读数(频率 + 数值)。由界面 <-/-> 按钮移动。 */
static void fra_draw_cursor(u32 fb, const ui_fra_t *a, int ytop, int ybot, int px, int pw, int is_phase)
{
    char lab[40];
    u16  col;                      /* ★ 光标颜色: 相频用亮黄(与幅频打点同色), 幅频保持原色 */
    int  bx;                       /* ★ 读数框横坐标 */
    int  x, y, i;

    if ((a->cur_on == 0u) || (a->npt == 0u)) { cur_bak_restore(fb); return; }   /* 关掉光标: 也把旧的贴回去 */
    col = (is_phase != 0) ? UI_OSC_COLOR_CH1 : UI_OSC_COLOR_CH2;
    cur_bak_restore(fb);      /* ★ 顺序很重要: 先擦掉上一次的光标, 再画新的, 最后才保存 */
    i = (int)a->cur_idx;
    if (i >= (int)a->npt) { i = (int)a->npt - 1; }
    {
        double f = (double)a->f_start_hz + (double)i * (double)a->step_hz;

        x = fra_x_of_f(f, px, pw);
        if ((x < px) || (x >= (px + pw))) { return; }
        y = (is_phase != 0) ? fra_pha_y((double)a->hpha[i], ytop, (ybot - ytop))
                            : fra_mag_y(a, (double)a->habs[i], ytop, ybot, (ybot - ytop));
        if (y < ytop) { y = ytop; }
        if (y > ybot) { y = ybot; }
        /* ★ 读数框横坐标: 默认贴光标右侧; 若右边缘会超出绘图区, 就改贴光标左侧。
           纵坐标不变(仍在绘图区顶部 ytop+2)。宽度固定 150, 避免文字变短留残影。 */
        {
            int bw = 160;   /* XXFIT: 150->160: 光标读数改两位小数后框宽上限是 152px(tw=144+8), 用 150 会让右边缘框线被屏幕裁掉 1~2px */

            bx = x + 8;
            if ((bx + bw) > (px + pw)) { bx = x - 8 - bw; }
            if (bx < px) { bx = px; }
            if ((bx + bw) > (px + pw)) { bw = px + pw - bx; }
        }
        {
            int yy;

        /* ★★ 顺序关键: 这里存的是"还没有光标"的干净像素!
           原来写在画完之后, 存下来的图里带着光标 -> 每次贴回都把光标又画上 -> 擦不掉。 */
        /* ★ 备份区要同时包住光标线和读数框(读数框会跟着光标移动) */
        {
            int xa = (x - 6 < bx - 2) ? (x - 6) : (bx - 2);
            int xb2 = (x + 6 > bx + 190) ? (x + 6) : (bx + 190);   /* XXFIT: 保存带右界 152->190, 覆盖最大框宽 152px + 余量(原值恰好卡边界 -> 移走光标会留竖线残影) */

            if ((xb2 - xa) > CUR_BAK_W) { xb2 = xa + CUR_BAK_W; }
            cur_bak_save(fb, xa, ytop, xb2 - xa, (ybot - ytop) + 22);
        }
            for (yy = ytop; yy <= ybot; yy += 6) {
                int y2 = yy + 3;

                if (y2 > ybot) { y2 = ybot; }
                lcd_vline(fb, x, yy, y2, col);
            }
        }
        lcd_rect(fb, x - 3, y - 3, 7, 7, col);
        {
            char fh[16];

            (void)fra_fmt_hz1(fh, (s32)(f * 10.0 + 0.5));   /* ★ 传 0.1Hz 单位 */
            if (is_phase != 0) {
                int d  = (int)((double)a->hpha[i] * 10.0 + ((a->hpha[i] >= 0.0f) ? 0.5 : -0.5));   /* ★ 0.1 度 */
                int ad = (d < 0) ? -d : d;

                (void)sprintf(lab, "%s %s%d.%d°", fh, (d < 0) ? "-" : "", ad / 10, ad % 10);   /* ★ 单位用 °(右上角小圈) */
            } else {
                double mg = (double)a->habs[i];
                int db100, ad;

                if (mg < 1e-9) { mg = 1e-9; }
                {   /* XXROUND: 由【向零截断】改成【四舍五入】。原来 -2.95dB 会显示成 -2.9(截断),
                       与截止频率标注用的 -3.01dB 看起来对不上; 四舍五入后 -2.95 显示 -3.0, 与标注一致。 */
                    double ddb = 20.0 * log10(mg) * 100.0;   /* XXDB2: 由 0.1dB 单位改成 0.01dB 单位(多一位小数) */

                    db100 = (int)(ddb + ((ddb >= 0.0) ? 0.5 : -0.5));   /* 四舍五入到 0.01dB */
                }
                ad = (db100 < 0) ? -db100 : db100;
                /* XXDB2: 用 %02d 显示小数两位, 保证前导零(例如 -3.05 -> "-3.05", -3.00 -> "-3.00")。 */
                (void)sprintf(lab, "%s %s%d.%02d dB", fh, (db100 < 0) ? "-" : "", ad / 100, ad % 100);
            }
            /* 宽度固定 150(见上面的 bx 计算), 这里不再按文字宽度变 */

            /* ★ 只画矩形边框(不再填充黑底), 大小跟着读数文字走;
               边框与文字都用当前页面的光标颜色 col(幅频=CH2, 相频=CH1 亮黄)。 */
            {
                int tw = (int)ui_text_size(lab, 1);
                int bwd = tw + 8;

                if (bwd < 24) { bwd = 24; }
                if ((bx + bwd) > (px + pw)) { bwd = px + pw - bx; }
                lcd_fill_rect(fb, bx, ytop + 1, bwd, 18, UI_OSC_COLOR_INFO);   /* ★ 黑底 */
                lcd_rect(fb, bx, ytop + 1, bwd, 18, col);                      /* ★ 同色边框 */
            }
            (void)ui_draw_text(fb, bx + 4, ytop + 2, lab, col, 1);   /* ★ 文字用光标颜色 */
        }
        /* ★ 备份区必须"跟着光标走": 只存光标线周围那条窄带(含虚线+方框)。
           原来固定存 px+2 那块(左边), 光标跑到右边时存的图里根本没有光标 -> 擦不掉。 */
        /* (原来的保存位置已前移: 必须先存"干净"像素再画光标) */
    }
}
/* ★ 只刷新光标: 先把上次那块贴回(擦掉旧光标), 再按当前 view 画新光标。
   由界面 <-/-> 按钮触发, 不走 fra_redraw_pane -> 不闪、读数也能跟着变。 */
void ui_fra_cursor_refresh(u32 fb, ui_fra_t *a)
{
    int ytop = FRA_FULL_Y + 26, ybot = FRA_FULL_Y + FRA_FULL_H - 22;
    int px = FRA_FULL_X + FRA_PLOT_L, pw = FRA_FULL_W - FRA_PLOT_L;

    fra_draw_cursor(fb, (const ui_fra_t *)a, ytop, ybot, px, pw, (a->view == 1u) ? 1 : 0);
}
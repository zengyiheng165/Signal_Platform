/******************************************************************************
 * ui_filter.c - 数字滤波器界面实现(第五个独立页面)
 *
 * 防闪屏三条铁律(与工程约定一致, 逐条落实):
 *   1) 整屏重画只发生在 ui_filter_draw(), 它只被 ui_app_draw() 在【切页那一帧】调用;
 *      运行期间任何重画都走 ui_filter_draw_live(), 且只在脏标志置位时做一次, 做完清标志。
 *   2) 液晶刷新一律按【整行】粒度: 只用 ui_filter_flush()/ui_filter_flush_chrome()
 *      (刷 72..439 整条横带 / 顶栏+信息行), 绝不按 x 裁剪 -> 不会撕裂。
 *   3) 触摸回调(ui_filter_hit/row_step)只改状态 + 置脏标志, 一个像素都不画。
 ******************************************************************************/
#include "ui_filter.h"
#include "ui_screens.h"
#include "lcd_gfx.h"
#include "xil_cache.h"
#include <stdio.h>
#include <string.h>

/* ---------------- 内部工具 ---------------- */
static const char *flt_type_name(u8 t)
{
    switch (t) {
    case UI_FLT_TYPE_LP: return "低通";
    case UI_FLT_TYPE_HP: return "高通";
    case UI_FLT_TYPE_BP: return "带通";
    default:             return "带阻";
    }
}

static const char *flt_kind_name(u8 k)
{
    return (k == UI_FLT_KIND_FIR) ? "FIR" : "IIR";
}

static const char *flt_win_name(u8 w)
{
    switch (w) {
    case 0u: return "矩形";
    case 1u: return "汉宁";
    case 2u: return "汉明";
    default: return "布莱克曼";
    }
}


static const char *flt_status_name(u8 st)
{
    switch (st) {
    case UI_FLT_ST_OK:   return "已下发";
    case UI_FLT_ST_SAT:  return "饱和:请降阶或提高截止";
    case UI_FLT_ST_FAIL: return "参数非法:设计失败";
    default:             return "待下发";
    }
}

/* 频率显示: 用整数拼, 不用 %f(与工程习惯一致) */
static void flt_fmt_hz(char *b, s32 hz)
{
    if (hz >= 1000000) {
        (void)sprintf(b, "%d.%03dMHz", (int)(hz / 1000000), (int)((hz / 1000) % 1000));
    } else if (hz >= 1000) {
        (void)sprintf(b, "%d.%03dkHz", (int)(hz / 1000), (int)(hz % 1000));   /* ★ 1 位小数->3 位: 原来分辨率只有 100Hz, 输 1.05kHz 会显示成 "1.0kHz" 像没生效 */
    } else {
        (void)sprintf(b, "%dHz", (int)hz);
    }
}

/* 一行参数: 左标题 + 右数值 + 选中框 */
static void flt_row(u32 fb, int i, const char *title, const char *val, int cur)
{
    int y = UI_FLT_ROW_Y + i * (UI_FLT_ROW_H + UI_FLT_ROW_GAP);
    int tw, vw;

    lcd_fill_rect(fb, UI_FLT_ROW_X, y, UI_FLT_ROW_W, UI_FLT_ROW_H,
                  (cur != 0) ? UI_OSC_COLOR_TABBG : UI_OSC_COLOR_BG);
    lcd_rect(fb, UI_FLT_ROW_X, y, UI_FLT_ROW_W, UI_FLT_ROW_H,
             (cur != 0) ? UI_OSC_COLOR_CH1 : UI_OSC_COLOR_FRAME);
    tw = (int)ui_text_size(title, 1);
    vw = (int)ui_text_size(val, 1);
    (void)ui_draw_text(fb, UI_FLT_ROW_X + 10, y + (UI_FLT_ROW_H - 16) / 2, title,
                       UI_OSC_COLOR_LABEL, 1);
    (void)ui_draw_text(fb, UI_FLT_ROW_X + UI_FLT_ROW_W - vw - 12, y + (UI_FLT_ROW_H - 16) / 2,
                       val, UI_OSC_COLOR_TEXT, 1);
    (void)tw;
}

/* 参数区(左栏 6 行) */
static void flt_draw_rows(u32 fb, const ui_filter_t *a)
{
    char v[40];
    int  i = 0;

    flt_row(fb, i++, "类型", flt_type_name(a->type), (a->row == 0u) ? 1 : 0);
    flt_row(fb, i++, "结构", flt_kind_name(a->kind), (a->row == 1u) ? 1 : 0);
    (void)sprintf(v, "%d", (int)a->order);
    flt_row(fb, i++, "阶数", v, (a->row == 2u) ? 1 : 0);
    if (a->kind == UI_FLT_KIND_FIR) {
        flt_row(fb, i++, "窗", flt_win_name(a->win), (a->row == 3u) ? 1 : 0);
    } else {
        flt_row(fb, i++, "窗", "-(IIR无)", (a->row == 3u) ? 1 : 0);
    }
    if ((a->type == UI_FLT_TYPE_BP) || (a->type == UI_FLT_TYPE_BS)) {
        char b1[16], b2[16];

        flt_fmt_hz(b1, a->f1_hz);
        flt_fmt_hz(b2, a->f2_hz);
        (void)sprintf(v, "%s ~ %s", b1, b2);
        flt_row(fb, i++, "频率", v, (a->row == 4u) ? 1 : 0);
    } else {
        flt_fmt_hz(v, a->fc_hz);
        flt_row(fb, i++, "截止", v, (a->row == 4u) ? 1 : 0);
    }
    flt_row(fb, i++, "输出", (a->out_on != 0u) ? "开" : "关", (a->row == 5u) ? 1 : 0);
}

/* 右栏: 已下发的系数 / q / 状态(便于自查, 只显示不加逻辑) */
static void flt_draw_coef(u32 fb, const ui_filter_t *a)
{
    char l[64];
    int  y = UI_FLT_K_Y + 8;

    lcd_fill_rect(fb, UI_FLT_K_X, UI_FLT_K_Y, UI_FLT_K_W, UI_FLT_K_H, UI_OSC_COLOR_BG);
    lcd_rect(fb, UI_FLT_K_X, UI_FLT_K_Y, UI_FLT_K_W, UI_FLT_K_H, UI_OSC_COLOR_FRAME);

    (void)ui_draw_text(fb, UI_FLT_K_X + 10, y, "下发结果", UI_OSC_COLOR_LABEL, 1);
    y += 20;
    (void)sprintf(l, "结构 %s   类型 %s", flt_kind_name(a->kind), flt_type_name(a->type));
    (void)ui_draw_text(fb, UI_FLT_K_X + 10, y, l, UI_OSC_COLOR_TEXT, 1);
    y += 20;
    (void)sprintf(l, "系数个数 %d", (int)a->coef_n);
    (void)ui_draw_text(fb, UI_FLT_K_X + 10, y, l, UI_OSC_COLOR_TEXT, 1);
    y += 20;
    (void)sprintf(l, "|系数|max %d", (int)a->coef_max);
    (void)ui_draw_text(fb, UI_FLT_K_X + 10, y, l, UI_OSC_COLOR_TEXT, 1);
    y += 20;
    (void)sprintf(l, "IIR q(移位) %d", (int)a->q_shift);
    (void)ui_draw_text(fb, UI_FLT_K_X + 10, y, l, UI_OSC_COLOR_TEXT, 1);
    y += 24;
    /* ★ 实际下发的系数: 就是 filt_apply_from_ui() 里量化+夹取之后送给
       FIR_COEFFICIENT_RELOAD() / IIR_COEFFICIENT_SET() 的那一组
       (FIR 10 个; IIR 低通/高通 order+1 个, 带通/带阻 2*order+1 个)。
       两列显示(左列 c0..c7, 右列 c8..c15), 12 位有符号: -2048..2047。 */
    /* ★ 显示哪一组系数, 按当前是 FIR 还是 IIR 决定:
         FIR -> 就显示那 10 个抽头(hq[0..9]);
         IIR -> 显示完整的一组: 分子 b(coef[]) + 分母 a(coef_y[]);
       显示的都是量化+夹取之后真正下发给硬件的值(12 位有符号 -2048..2047)。 */
    {
        int ci;
        int nc = (int)a->coef_n;

        if (nc > 16) { nc = 16; }
        if (nc < 0)  { nc = 0; }
        if (a->kind == UI_FLT_KIND_IIR) {
            (void)ui_draw_text(fb, UI_FLT_K_X + 10, y, "分子 b", UI_OSC_COLOR_LABEL, 1);
            y += 18;
            for (ci = 0; ci < nc; ci++) {
                int col = ci / 4;                     /* IIR: 每列 4 个, 一共两列 */
                int row = ci % 4;

                (void)sprintf(l, "b%-2d %6d", ci, (int)a->coef[ci]);
                (void)ui_draw_text(fb, UI_FLT_K_X + 10 + (col * 190), y + (row * 18),
                                   l, UI_OSC_COLOR_TEXT, 1);
            }
            y += (4 * 18) + 6;
            (void)ui_draw_text(fb, UI_FLT_K_X + 10, y, "分母 a", UI_OSC_COLOR_LABEL, 1);
            y += 18;
            for (ci = 0; ci < nc; ci++) {
                int col = ci / 4;
                int row = ci % 4;

                (void)sprintf(l, "a%-2d %6d", ci, (int)a->coef_y[ci]);
                (void)ui_draw_text(fb, UI_FLT_K_X + 10 + (col * 190), y + (row * 18),
                                   l, UI_OSC_COLOR_TEXT, 1);
            }
            y += (4 * 18) + 6;
        } else {
            (void)ui_draw_text(fb, UI_FLT_K_X + 10, y, "抽头(FIR)", UI_OSC_COLOR_LABEL, 1);
            y += 18;
            for (ci = 0; ci < nc; ci++) {
                int col = ci / 8;                     /* FIR: 每列 8 个(10 个抽头两列放得下) */
                int row = ci % 8;

                (void)sprintf(l, "c%-2d %6d", ci, (int)a->coef[ci]);
                (void)ui_draw_text(fb, UI_FLT_K_X + 10 + (col * 190), y + (row * 18),
                                   l, UI_OSC_COLOR_TEXT, 1);
            }
            y += (8 * 18) + 6;   /* ★ 必须按【8 行】留位: 两列各画 8 行(c0..c7 / c8..c15), 之前写 5 行 -> "状态"压住了 c4..c7 */
        }
    }
    (void)ui_draw_text(fb, UI_FLT_K_X + 10, y, "状态", UI_OSC_COLOR_LABEL, 1);
    y += 20;
    (void)ui_draw_text(fb, UI_FLT_K_X + 10, y, flt_status_name(a->status),
                       (a->status == UI_FLT_ST_OK) ? UI_OSC_COLOR_TEXT : UI_OSC_COLOR_BAD, 1);
}

/* ---------------- 顶栏 + 信息行 ---------------- */
void ui_filter_draw_status(u32 fb, const ui_filter_t *a)
{
    char s[64];
    char fs1[24];

    lcd_fill_rect(fb, 0, 0, LCD_GFX_WIDTH, UI_OSC_HEADER_H, UI_OSC_COLOR_INFO);
    (void)ui_draw_text(fb, 12, 16, "< 返回", UI_OSC_COLOR_TEXT, 1);
    (void)ui_draw_text(fb, 380, 16, "数字滤波器", UI_OSC_COLOR_TEXT, 1);

    lcd_fill_rect(fb, 0, UI_OSC_INFO_Y, LCD_GFX_WIDTH, UI_OSC_INFO_H, UI_OSC_COLOR_BG);
    flt_fmt_hz(fs1, a->fs_hz);                      /* ★ 显示实际采样率(不再写死 8192000) */
    (void)sprintf(s, "fs=%s   %s   %s", fs1, flt_kind_name(a->kind),
                  flt_status_name(a->status));
    (void)ui_draw_text(fb, 12, UI_OSC_INFO_Y + 4, s, UI_OSC_COLOR_LABEL, 1);
}

void ui_filter_draw_params(u32 fb, const ui_filter_t *a)
{
    lcd_fill_rect(fb, 0, UI_FLT_Y0, LCD_GFX_WIDTH, UI_FLT_H, UI_OSC_COLOR_BG);
    flt_draw_rows(fb, a);
    flt_draw_coef(fb, a);
}

/* ---------------- 整屏重画(只由 ui_app_draw 在切页那一帧调用) ---------------- */
void ui_filter_draw(u32 fb, ui_filter_t *a)
{
    lcd_clear(fb, UI_OSC_COLOR_BG);
    ui_filter_draw_status(fb, a);
    ui_filter_draw_params(fb, a);
    ui_filter_flush(fb);
    ui_filter_flush_chrome(fb);
}

/* ---------------- 帧边界增量刷新(消费脏标志) ----------------
   每个 if 只做一件事并立刻清标志: 绝不把多件重活挤在同一帧(挤了就会超出帧时间, 液晶扫描追上来 = 闪一下)。 */
void ui_filter_draw_live(u32 fb, ui_filter_t *a)
{
    if (a->redraw_bar != 0u) {            /* 顶栏+信息行(最便宜) */
        a->redraw_bar = 0u;
        ui_filter_draw_status(fb, a);
        ui_filter_flush_chrome(fb);
        return;
    }
    if (a->redraw_params != 0u) {         /* 参数区+系数区(约 800x368) */
        a->redraw_params = 0u;
        ui_filter_draw_params(fb, a);
        ui_filter_flush(fb);
        return;
    }
}

/* ---------------- 液晶刷新(整行整条, 防撕裂) ---------------- */
void ui_filter_flush(u32 fb)
{
    Xil_DCacheFlushRange((INTPTR)(fb + (u32)UI_FLT_Y0 * LCD_GFX_WIDTH * 3u),
                         (u32)UI_FLT_H * LCD_GFX_WIDTH * 3u);
}

void ui_filter_flush_chrome(u32 fb)
{
    ui_osc_flush_chrome(fb);              /* 复用示波器的顶栏+信息行整行刷(0..71) */
}

/* ---------------- 触摸命中(只读判定, 不改任何状态) ----------------
   返回: 0 = 未命中; 1..UI_FLT_ROW_N = 命中第几行; 100 = 命中顶栏"返回" */
int ui_filter_hit(const ui_filter_t *a, int x, int y, u8 *id)
{
    int i;

    (void)a;
    if ((y >= 0) && (y < UI_OSC_HEADER_H) && (x < 120)) {
        if (id != 0) { *id = 0u; }
        return 100;
    }
    for (i = 0; i < (int)UI_FLT_ROW_N; i++) {
        int ry = UI_FLT_ROW_Y + i * (UI_FLT_ROW_H + UI_FLT_ROW_GAP);

        if ((x >= UI_FLT_ROW_X) && (x < (UI_FLT_ROW_X + UI_FLT_ROW_W)) &&
            (y >= ry) && (y < (ry + UI_FLT_ROW_H))) {
            if (id != 0) { *id = (u8)i; }
            return i + 1;
        }
    }
    return 0;
}

/* ---------------- 参数步进(触摸只调它, 只改状态+置标志, 不画图) ---------------- */
void ui_filter_row_step(ui_filter_t *a, int dir)
{
    s32 step = (dir > 0) ? 1 : -1;

    switch (a->row) {
    case 0u:                                                  /* 类型 */
        a->type = (u8)((a->type + UI_FLT_TYPE_N + step) % UI_FLT_TYPE_N);
        /* ★ 类型变了必须重算阶数: 高通/带通要求 order 为偶数(FIR), 带通/带阻 IIR 只能 <=3 */
        if (a->kind == UI_FLT_KIND_FIR) {
            /* ★ 低通/带阻给 9(10 抽头全用满); 高通/带通必须偶数阶给 8。
               以前两处规则不同(case 1 给带阻 8, case 2 给带阻 9) -> 同一个带阻显示 8 还是 9
               取决于操作顺序, 现在统一成这一条。 */
            a->order = ((a->type == UI_FLT_TYPE_LP) || (a->type == UI_FLT_TYPE_BS)) ? 9u : 8u;
        } else {
            if ((a->type == UI_FLT_TYPE_BP) || (a->type == UI_FLT_TYPE_BS)) {
                if (a->order > 3u) { a->order = 3u; }
            } else if (a->order > UI_FLT_IIR_MAX_ORDER) {
                a->order = UI_FLT_IIR_MAX_ORDER;
            }
            if (a->order < 1u) { a->order = 1u; }
        }
        break;
    case 1u:                                                  /* 结构 */
        a->kind = (u8)((a->kind + UI_FLT_KIND_N + step) % UI_FLT_KIND_N);
        /* ★ 结构变了也要重算: FIR<->IIR 的阶数取值范围完全不同 */
        if (a->kind == UI_FLT_KIND_FIR) {
            /* ★ 低通/带阻给 9(10 抽头全用满); 高通/带通必须偶数阶给 8。
               以前两处规则不同(case 1 给带阻 8, case 2 给带阻 9) -> 同一个带阻显示 8 还是 9
               取决于操作顺序, 现在统一成这一条。 */
            a->order = ((a->type == UI_FLT_TYPE_LP) || (a->type == UI_FLT_TYPE_BS)) ? 9u : 8u;
        } else {
            a->order = (a->order > UI_FLT_IIR_MAX_ORDER) ? UI_FLT_IIR_MAX_ORDER : a->order;
            if ((a->type == UI_FLT_TYPE_BP) || (a->type == UI_FLT_TYPE_BS)) {
                if (a->order > 3u) { a->order = 3u; }
            }
            if (a->order < 1u) { a->order = 1u; }
        }
        break;
    case 2u:                                                  /* 阶数 */
        if (a->kind == UI_FLT_KIND_FIR) {
            /* FIR 硬件固定 10 抽头: 低通 order=9(10 个系数); 高通/带通要求偶数 order,
               用 8(9 个系数, 第 10 个补 0); 带阻 8 或 9 均可 —— 这里固定给安全值 */
            a->order = ((a->type == UI_FLT_TYPE_LP) || (a->type == UI_FLT_TYPE_BS)) ? 9u : 8u;
        } else {
            int o = (int)a->order + step;

            if (o < 1)  { o = 1; }
            int omax = UI_FLT_IIR_MAX_ORDER;

            if ((a->type == UI_FLT_TYPE_BP) || (a->type == UI_FLT_TYPE_BS)) {
                omax = 3;      /* ★ 带通/带阻实际阶数 = 2*order, 硬件 7 阶 => order <= 3 */
            }
            if (o > omax) { o = omax; }
            a->order = (u8)o;
        }
        break;
    case 3u:                                                  /* 窗(仅 FIR 有意义) */
        if (a->kind == UI_FLT_KIND_FIR) {
            a->win = (u8)((a->win + UI_FLT_WIN_N + step) % UI_FLT_WIN_N);
        }
        break;
    case 4u:                                                  /* 截止/频率 */
        if ((a->type == UI_FLT_TYPE_BP) || (a->type == UI_FLT_TYPE_BS)) {
            /* 带通/带阻: 步进 f2(上边界), 保证 f1 < f2 < fs/2 */
            a->f2_hz += step * 1000;
            if (a->f2_hz <= (a->f1_hz + 1000)) { a->f2_hz = a->f1_hz + 1000; }
            /* ★ Nyquist 夹取按【当前实际采样率】算(原来写死 4000000 = 8.192MHz/2):
               切到 5MHz 后 Nyquist 只有 2.5MHz; f1/f2 若留在 4MHz, 设计函数会直接返回 0,
               界面就显示"参数非法:设计失败"。这里按 fs/2 留 2% 余量。 */
            {
                s32 lim = (a->fs_hz / 2) * 98 / 100;

                if (lim < 1000) { lim = 1000; }
                if (a->f2_hz > lim) { a->f2_hz = lim; }
            }
        } else {
            a->fc_hz += step * 1000;
            if (a->fc_hz < 1000)    { a->fc_hz = 1000; }
            {
                s32 lim = (a->fs_hz / 2) * 98 / 100;

                if (lim < 1000) { lim = 1000; }
                if (a->fc_hz > lim) { a->fc_hz = lim; }   /* ★ 原来写死 4000000 */
            }
        }
        break;
    case 5u:                                                  /* ★ 输出开关(只 on/off) */
        a->out_on = (a->out_on == 0u) ? 1u : 0u;
        break;
    default:
        break;
    }
    a->need_apply    = 1u;      /* ★ 请求重新设计+下发(主循环在帧边界消费) */
    a->redraw_params = 1u;      /* ★ 参数区重画(由 draw_live 消费) */
    a->redraw_bar    = 1u;      /* ★ 信息行也要跟着更新 */
}

/* ★ 当前该给 wave_sel 的选择码(GPIO[3:2]): 输出关 = 0(普通 DDS), 输出开 = FIR(1) 或 IIR(2) */
u8 ui_filter_sel(const ui_filter_t *a)
{
    if (a->out_on == 0u) { return UI_FLT_SRC_DDS; }
    return (a->kind == UI_FLT_KIND_FIR) ? UI_FLT_SRC_FIR : UI_FLT_SRC_IIR;
}

/* ---------------- 默认值 ---------------- */
void ui_filter_default(ui_filter_t *a)
{
    (void)memset(a, 0, sizeof(*a));      /* ★ 先 memset(不要像 ui_fra_default 那样先赋值后被清掉) */
    a->type      = UI_FLT_TYPE_LP;
    a->kind      = UI_FLT_KIND_FIR;
    a->order     = 9u;                    /* 低通 + FIR: 正好 10 个系数 */
    a->win       = 2u;                    /* 汉明 */
    a->fc_hz     = 20000;
    a->f1_hz     = 10000;
    a->f2_hz     = 50000;
    a->out_on    = 0u;                    /* 默认不输出滤波器(让信号发生器出声, 安全) */
    a->status    = UI_FLT_ST_IDLE;
    a->row       = 0u;
    a->need_apply    = 1u;                /* 上电先按默认参数下发一次 */
    a->redraw_params = 1u;
    a->redraw_bar    = 1u;
    /* ★ 采样率默认值(必须在这里初始化: 之前误加到了 ui_filter_row_step() 末尾):
       与上电 adc_fs_apply(8.192MHz) 的实际值一致; 主循环每帧会把 fs_hz/fs_idx 对齐到
       硬件真实值(方案 a), 所以从频谱仪的小数档切过来也会被自动纠正。 */
    a->fs_hz     = 8192000;
    a->fs_idx    = 0u;
    a->fs_req    = 0u;
}
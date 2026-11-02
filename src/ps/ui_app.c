/******************************************************************************
 * ui_app.c - 界面管理实现
 ******************************************************************************/
#include "ui_app.h"
#include "ui_touch.h"   /* ui_touch_draw_bar(): 刷新按钮栏标签 */
#include "lcd_gfx.h"
#include "xil_cache.h"
#include <string.h>
#include <stdio.h>      /* sprintf: Chirp-Z 键盘初值("1234.000") 需要它, 否则隐式声明告警 */

/* 界面名字(顺序必须与 UI_SCREEN_xxx 一致) */
static const char *const ui_screen_names[UI_SCREEN_CNT] = {
    "示波器",
    "信号发生器",
    "频谱仪", "频率特性分析仪", "数字滤波器",
};

static u8 ui_redraw_req;

/* 数字键盘窗口层的两个标志(定义提前到最上面: ui_app_switch 也要清它们) */
static u8 ui_kbd_saved;         /* 键盘底板是否有效 */
static u8 ui_kbd_shown;         /* 键盘像素当前是否在屏上 */

/* ---------------- 基础 ---------------- */
void ui_app_default(ui_app_t *a)
{
    (void)memset(a, 0, sizeof(*a));
    a->screen = UI_SCREEN_SCOPE;
    a->menu_open = 0;
    a->set_open = 0;
    ui_osc_default(&a->osc);
    ui_awg_default(&a->awg);
    ui_spec_default(&a->spec);
    ui_fra_default(&a->fra);
    ui_filter_default(&a->flt);     /* ★ 第五界面: 数字滤波器 */
    /* ★ 数字键盘: 默认关着(状态清零就够, 这里显式写一遍免得"未确认的旧值"被当成有效值) */
    a->kbd.open  = 0u;
    a->kbd.ok    = 0u;
    a->kbd.press = UI_NUMKBD_KEY_NONE;
    a->kbd.len   = 0u;
    a->kbd.dot   = 0u;
    a->kbd.value = 0;
    /* 键盘的提示/单位文本: 模块只认"档号", 含义由调用方登记(缺字先跑 fontcover.ps1) */
    ui_numkbd_set_hint(UI_APP_KBD_TGT_REF, "参考电压");
    ui_numkbd_set_unit_text(UI_SPEC_UNIT_V, "V");
    ui_numkbd_set_unit_text(UI_SPEC_UNIT_MV, "mV");
    ui_numkbd_set_unit_text(UI_SPEC_UNIT_VPP, "Vpp");
    ui_numkbd_set_unit_text(UI_SPEC_UNIT_MVPP, "mVpp");
    /* 频率起始/终止: 提示 + kHz/MHz 两个单位槽 */
    ui_numkbd_set_hint(UI_APP_KBD_TGT_FSTART, "频率起始");
    ui_numkbd_set_hint(UI_APP_KBD_TGT_FSTOP, "频率终止");
    ui_numkbd_set_hint(UI_APP_KBD_TGT_FRA_START, "频率起始");   /* ★ FRA 专用编号 */
    ui_numkbd_set_hint(UI_APP_KBD_TGT_FRA_STOP,  "频率终止");
    ui_numkbd_set_hint(UI_APP_KBD_TGT_CZT_FSTART, "CZT-F1");   /* Chirp-Z: 起始频率 */
    ui_numkbd_set_hint(UI_APP_KBD_TGT_CZT_FSTOP,  "CZT-F2");   /* Chirp-Z: 终止频率 */
    ui_numkbd_set_hint(UI_APP_KBD_TGT_CZT_M,      "CZT-M");    /* Chirp-Z: 点数 M */
    ui_numkbd_set_hint(UI_APP_KBD_TGT_FLT_FC,    "截止");   /* ★ 滤波器: 截止/频率 */
    ui_numkbd_set_unit_text(UI_APP_KBD_UNIT_KHZ, "kHz");
    ui_numkbd_set_unit_text(UI_APP_KBD_UNIT_MHZ, "MHz");
    /* 信号发生器参数区(③-3): 单位槽 8..12; 目标 3 的提示文字在每次弹键盘前按行名刷新 */
    ui_numkbd_set_unit_text(UI_AWG_KU_HZ,  "Hz");
    ui_numkbd_set_unit_text(UI_AWG_KU_PCT, "%");
    ui_numkbd_set_unit_text(UI_AWG_KU_DEG, "\xC2\xB0");   /* UTF-8 的度符号 */
    ui_numkbd_set_unit_text(UI_AWG_KU_INT, "");
    ui_numkbd_set_unit_text(UI_AWG_KU_X10, "x0.1");
    ui_numkbd_set_hint(UI_APP_KBD_TGT_AWG, "参数");
    a->set_row_dirty = 0u;
}

int ui_app_screen_count(void)
{
    return (int)UI_SCREEN_CNT;
}

const char *ui_app_screen_name(int i)
{
    if ((i < 0) || (i >= (int)UI_SCREEN_CNT)) {
        return "";
    }
    return ui_screen_names[i];
}

int ui_app_need_redraw(void)
{
    return (ui_redraw_req != 0u) ? 1 : 0;
}

/* 供各屏的触摸处理请求"帧边界整屏重画"(绝不能在触摸路径里自己写整屏) */
void ui_app_redraw_req(void)
{
    ui_redraw_req = 1;
}

void ui_app_clear_redraw(void)
{
    ui_redraw_req = 0;
}
/* ★ 前置声明: ui_app_switch() 要作废窗口层底板, 而它们的定义在本文件后面(同 TU 的暂定定义, C 允许) */
static u8 ui_win_saved;
static u8 ui_win_was_czt;

void ui_app_switch(ui_app_t *a, int screen)
{
    if ((screen < 0) || (screen >= (int)UI_SCREEN_CNT)) {
        return;
    }
    /* 切界面是整屏重画: 键盘直接作废(底板/在屏标志一起清, 否则关键盘时会贴回旧屏幕的像素) */
    a->kbd.open = 0u;
    ui_kbd_saved = 0u;
    ui_win_saved   = 0u;    /* ★ 窗口层底板跟着作废: 切屏后旧屏幕的像素绝不能再贴回(否则"界面"菜单切屏 -> 再开/关窗口 = 残留) */
    ui_win_was_czt = 0u;    /* ★ 同上: 免得在别的界面去补画频谱底部面板 */
    ui_kbd_shown = 0u;
    a->screen = (u8)screen;
    /* ★ 切屏后必须把"上一帧迹/频谱区"作废: 它们记的是切换前的坐标,        不作废的话下一帧 ui_osc_draw_live() 会去擦那些已经不存在的旧迹 ->        从别的界面回到示波器时花屏一帧(只有示波器会, 因为频谱仪自己整块重画兜住了)。 */
    ui_osc_invalidate();
    a->spec.prev_valid = 0u;
    ui_redraw_req = 1;      /* 交给主循环在帧边界整屏重画 */
}

/* ---------------- 绘制 ---------------- */
void ui_app_draw(u32 fb, ui_app_t *a)
{
    switch (a->screen) {
    case UI_SCREEN_AWG:
        ui_awg_draw(fb, &a->awg);
        break;
    case UI_SCREEN_SPEC:
        ui_spec_draw(fb, &a->spec);
        break;
    case UI_SCREEN_FRA:
        ui_fra_draw(fb, &a->fra);
        break;
        case UI_SCREEN_FLT:  ui_filter_draw(fb, &a->flt);                                 break;
    default:
        ui_osc_draw(fb, &a->osc);
        break;
    }
    if (a->menu_open != 0u) {
        ui_app_menu_draw(fb, a, -1);
    } else if ((a->set_open != 0u) && (a->screen == UI_SCREEN_SPEC)) {
        ui_app_set_draw(fb, &a->spec);      /* 频谱仪"设置"子窗口(整屏重画时叠上去) */
    }
}

void ui_app_draw_live(u32 fb, ui_app_t *a)
{
    switch (a->screen) {
    case UI_SCREEN_AWG:
        ui_awg_draw_live(fb, &a->awg);      /* 预览静态, 内部空实现 */
        break;
    case UI_SCREEN_SPEC:
        ui_spec_draw_live(fb, &a->spec);    /* 频谱区原地刷新(只重画变化的列) */
        /* 频率刻度/信息行跟显示区间有关, 但它们不在频谱区里: 没有弹窗时补画一次
           (设置窗/键盘开着时不要画, 会被它们或底板贴回盖掉) */
        if (ui_app_win_open(a) == 0) {
            ui_spec_sync_chrome(fb, &a->spec);
        }
        break;
    case UI_SCREEN_FRA:
        ui_fra_draw_live(fb, &a->fra);
        if (a->fra.redraw_bar != 0u) {          /* ★ 曲线名/参数刚变过: 按钮栏标签在这里刷新 */
            a->fra.redraw_bar = 0u;
            ui_touch_draw_bar(fb, a, UI_BTN_NONE);
        }
        /* ★ 扫频可能在 helloworld.c 里自动结束(扫完把 running 归 0), 那时按钮栏的
           "运行/停止" 标签必须跟着刷一次 —— 否则会一直显示"停止"。
           按下按钮那条路径本来就重画按钮栏, 这里只管"自动结束"这一种。 */
        {
            static u8 fra_run_prev = 0u;

            if ((a->fra.running != 0u) != (fra_run_prev != 0u)) {
                fra_run_prev = a->fra.running;
                ui_touch_draw_bar(fb, a, UI_BTN_NONE);
            }
        }
        break;
        case UI_SCREEN_FLT:  ui_filter_draw_live(fb, &a->flt);                           break;
    default:
        ui_osc_draw_live(fb, &a->osc);
        break;
    }
}

void ui_app_flush_live(u32 fb, const ui_app_t *a)
{
    switch (a->screen) {
    case UI_SCREEN_AWG:
        /* 预览不随时间变化, 上面没有写屏, 不需要 flush */
        break;
    case UI_SCREEN_SPEC:
        ui_spec_flush(fb);
        break;
    case UI_SCREEN_FRA:
        ui_fra_flush(fb);
        break;
        case UI_SCREEN_FLT:  ui_filter_flush(fb);                                       break;
    default:
        ui_osc_flush_wave(fb);
        ui_osc_flush_values(fb);
        break;
    }
}

void ui_app_after_button(u32 fb, ui_app_t *a)
{
    switch (a->screen) {
    case UI_SCREEN_AWG:
        /* ★ 预览是"整格清屏 + 重画"(800x264 ≈ 21 万像素), 不能在触摸处理里同步做:
           触摸轮询在每帧末尾, 写这么一大片会被液晶扫描追上 -> 花屏
           (切到第一个 BRAM 波形时同一帧还要生成 4096 点表并写 BRAM, 最容易顶穿)。
           改成只打标志, 由 ui_awg_draw_live() 在下一帧起点附近(帧边界)画。 */
        /* ★ 先清掉挂起的 redraw_params: 下面这一行【立刻】就把参数区画了, 若不清, 下一帧 ui_awg_draw_live()
           会因 redraw_params=1 再画一遍 -> 同一块 240x368 一帧内写两次 + ui_awg_flush(883KB) 刷两次(≈1.15MB 冗余,
           把"每帧一件重活"的预算花光 -> 撕裂窗口)。预览仍然只打标志, 由帧边界画。 */
        a->awg.redraw_params = 0u;
        a->awg.redraw_preview = 1u;        ui_awg_draw_params(fb, &a->awg);
        ui_awg_draw_status(fb, &a->awg);
        ui_awg_flush(fb);
        ui_awg_flush_chrome(fb);
        break;
    case UI_SCREEN_SPEC:
        /* ★ 只置"需要重画"标志, 由帧边界统一重画 + flush:
           原来这里在【触摸回调里】直接画顶栏(800x48) + 信息行(800x24) + 刻度带(800x71)
           约 343KB 像素写, 并 flush 1.06MB —— 都不在帧起点, 单缓冲屏上就是可见的闪屏带。
           chrome_req 由 ui_spec_sync_chrome()(帧边界) 消费, 该函数内部本来就会画这三块。 */
        a->spec.chrome_req = 1u;
        break;
    case UI_SCREEN_FRA:      /* ★ 补回: 上一步误删了这个 case 标签 */
        ui_fra_draw_chrome(fb, &a->fra);
        ui_fra_flush_chrome(fb);
        break;
        case UI_SCREEN_FLT:  ui_filter_draw_status(fb, &a->flt); ui_filter_flush_chrome(fb); break;
    default:
        ui_osc_draw_status(fb, &a->osc);
        ui_osc_draw_info(fb, &a->osc);
        ui_osc_draw_tabs(fb, &a->osc);
        ui_osc_flush_chrome(fb);
        break;
    }
}

/* ---------------- 上拉窗口 ---------------- */
int ui_app_menu_h(void)
{
    return (int)(UI_MENU_PAD * 2 + UI_MENU_ROW_H * (int)UI_SCREEN_CNT);
}

int ui_app_menu_y(void)
{
    return UI_OSC_BTN_Y - ui_app_menu_h();
}

static void ui_menu_row_rect(int i, int *x, int *y, int *w, int *h)
{
    *x = UI_MENU_X + 10;
    *y = ui_app_menu_y() + UI_MENU_PAD + i * UI_MENU_ROW_H;
    *w = UI_MENU_W - 20;
    *h = UI_MENU_ROW_H - 8;
}

void ui_app_menu_draw_row(u32 fb, const ui_app_t *a, int i, int pressed)
{
    int x = 0, y = 0, w = 0, h = 0;
    int cur = ((int)a->screen == i) ? 1 : 0;
    const char *name = ui_app_screen_name(i);
    u16 bg, fg;

    ui_menu_row_rect(i, &x, &y, &w, &h);

    if (pressed) {                       /* 按下: 反色 */
        bg = UI_OSC_COLOR_TEXT;
        fg = UI_OSC_COLOR_BG;
    } else if (cur) {                    /* 当前界面: 暗色不可选 */
        bg = UI_OSC_COLOR_BG;
        fg = UI_OSC_COLOR_BAD;
    } else {
        bg = UI_OSC_COLOR_TABBG;
        fg = UI_OSC_COLOR_TEXT;
    }
    lcd_fill_rect(fb, x, y, w, h, bg);
    lcd_rect(fb, x, y, w, h, cur ? UI_OSC_COLOR_BAD : UI_OSC_COLOR_FRAME);
    {
        int tw = (int)ui_text_size(name, 1);
        (void)ui_draw_text(fb, x + (w - tw) / 2, y + (h - 16) / 2, name, fg, 1);
    }
}

void ui_app_menu_draw(u32 fb, const ui_app_t *a, int pressed)
{
    int y0 = ui_app_menu_y();
    int h = ui_app_menu_h();
    int i;

    lcd_fill_rect(fb, UI_MENU_X, y0, UI_MENU_W, h, UI_OSC_COLOR_BG);
    lcd_rect(fb, UI_MENU_X, y0, UI_MENU_W, h, UI_OSC_COLOR_FRAME);
    for (i = 0; i < (int)UI_SCREEN_CNT; i++) {
        ui_app_menu_draw_row(fb, a, i, (i == pressed) ? 1 : 0);
    }
    ui_app_menu_flush(fb);
}

int ui_app_menu_hit(int x, int y)
{
    int i;
    int y0 = ui_app_menu_y();
    int h = ui_app_menu_h();

    if ((x < UI_MENU_X) || (x >= (UI_MENU_X + UI_MENU_W)) ||
        (y < y0) || (y >= (y0 + h))) {
        return -1;
    }
    for (i = 0; i < (int)UI_SCREEN_CNT; i++) {
        int rx = 0, ry = 0, rw = 0, rh = 0;
        ui_menu_row_rect(i, &rx, &ry, &rw, &rh);
        if ((x >= rx) && (x < (rx + rw)) && (y >= ry) && (y < (ry + rh))) {
            return i;
        }
    }
    return -1;      /* 点在窗口内但不在任何一项上 */
}

void ui_app_menu_flush(u32 fb)
{
    Xil_DCacheFlushRange((UINTPTR)(fb + (u32)ui_app_menu_y() * LCD_GFX_WIDTH * 3u),
                         (u32)ui_app_menu_h() * LCD_GFX_WIDTH * 3u);
}

/* ---------------- 弹窗层: 窗口的"存底板 / 贴底板" ----------------
   本文件里有两种弹窗: "界面"上拉菜单(ui_app_menu_*) 和 频谱仪"设置"子窗口(ui_app_set_*)。
   两者共用下面这一套机制 —— 需求要求复用, 而且这套写法已经解决了"开窗/关窗不闪":

   窗口是不透明的: 打开时把它要盖住的那块像素先存下来, 关窗时原样贴回。
   关窗只要写回窗口那一块(最大的设置窗 480x248 ≈ 0.36MB), 不用"擦成底色 + 把被盖住的
   波形/频谱重画一遍": 整屏重画 1.15MB 写不完就被液晶扫描追上 -> 整屏闪。
   贴回只改窗口区, 而窗口打开期间底下本来就不刷新(主循环用 ui_app_win_open() 判断),
   所以存下来的底板和屏幕上原先的像素完全一致 -> 关窗逐像素还原(验证台断言 0 像素差异)。

   ★ 这些函数只允许主循环在帧边界上(wait_vdma_frame() 之后)调用 —— 在触摸处理里写屏
     会被液晶扫描追上, 那正是"按按钮/开关窗口时闪一下"的来源。
   ★ 两个窗口共用同一块底板缓存: 同一时刻只会有一个窗口开着, 所以按"当前窗口矩形"
     (x/y/w/h 全部在运行时给)存/贴即可, 不必为设置窗再写一套。 */
#define UI_WIN_SAVE_W  (UI_SET_WIN_W)   /* ★ 容量跟着设置窗几何走(它是最宽的那个), 别再写死 */
#define UI_WIN_SAVE_H  (UI_SET_WIN_H)   /* ★ 以前写死 248, 设置窗加高到 276 后 ui_win_save_under 就越界写了 */
static u8 ui_win_save[UI_WIN_SAVE_W * 3 * UI_WIN_SAVE_H];   /* 当前窗口盖住的那块像素 */
static int ui_win_x, ui_win_y, ui_win_w, ui_win_h;          /* 最近一次画的窗口矩形 */
static u8 ui_win_saved;                                     /* 底板是否有效 */
static u8 ui_win_was_czt;                                   /* ★ 存底板时开的是 Chirp-Z 子窗口? (关闭时 kind 已变 0, 不能再问 kind) */
static u8 ui_win_redraw_req;                                /* 请求开/关窗口的重画 */

/* ---------------- "波形"选择子窗口(信号发生器) ----------------
   只列当前通道能产生的波形(ui_awg_wave_list): DAC1 最多 12 项, DAC2 只有 4 项。
   窗口尺寸随项数变化, 始终不超过 ui_win_save 的容量(480x300)。 */
int ui_wave_count(const ui_app_t *a)
{
    u8 list[UI_AWG_W_CNT];
    return (int)ui_awg_wave_list(a->awg.sel, list);
}

int ui_wave_h(const ui_app_t *a)
{
    int n = ui_wave_count(a);
    int rows = (n + (int)UI_WAVE_COLS - 1) / (int)UI_WAVE_COLS;

    if (rows < 1) {
        rows = 1;
    }
    return (int)UI_WAVE_TITLE_H + (int)UI_WAVE_GAP + (rows * (int)UI_WAVE_CELL_H) +
           ((rows - 1) * (int)UI_WAVE_GAP) + (int)UI_WAVE_GAP;
}

int ui_wave_y(const ui_app_t *a)
{
    return UI_OSC_BTN_Y - ui_wave_h(a);
}

static void ui_wave_row_rect(const ui_app_t *a, int i, int *x, int *y, int *w, int *h)
{
    int c = i % (int)UI_WAVE_COLS;
    int r = i / (int)UI_WAVE_COLS;

    *x = UI_WAVE_X + (int)UI_WAVE_GAP + c * ((int)UI_WAVE_CELL_W + (int)UI_WAVE_GAP);
    *y = ui_wave_y(a) + (int)UI_WAVE_TITLE_H + (int)UI_WAVE_GAP +
         r * ((int)UI_WAVE_CELL_H + (int)UI_WAVE_GAP);
    *w = (int)UI_WAVE_CELL_W;
    *h = (int)UI_WAVE_CELL_H;
}

void ui_wave_draw_row(u32 fb, const ui_app_t *a, int i, int pressed)
{
    u8 list[UI_AWG_W_CNT];
    int n = (int)ui_awg_wave_list(a->awg.sel, list);
    int x = 0, y = 0, w = 0, h = 0;
    int cur;
    u16 bg, fg;
    const char *name;

    if ((i < 0) || (i >= n)) {
        return;
    }
    cur = ((int)list[i] == (int)a->awg.ch[a->awg.sel & 1u].wave) ? 1 : 0;
    name = ui_awg_wave_name(list[i]);
    ui_wave_row_rect(a, i, &x, &y, &w, &h);

    if (pressed) {                       /* 按下: 反色 */
        bg = UI_OSC_COLOR_TEXT;
        fg = UI_OSC_COLOR_BG;
    } else if (cur) {                    /* 当前波形: 暗色(点它只是关窗) */
        bg = UI_OSC_COLOR_BG;
        fg = UI_OSC_COLOR_BAD;
    } else {
        bg = UI_OSC_COLOR_TABBG;
        fg = UI_OSC_COLOR_TEXT;
    }
    lcd_fill_rect(fb, x, y, w, h, bg);
    lcd_rect(fb, x, y, w, h, cur ? UI_OSC_COLOR_BAD : UI_OSC_COLOR_FRAME);
    {
        int tw = (int)ui_text_size(name, 1);
        (void)ui_draw_text(fb, x + ((w - tw) / 2), y + ((h - 16) / 2), name, fg, 1);
    }
}

void ui_wave_draw(u32 fb, const ui_app_t *a, int pressed)
{
    int n = ui_wave_count(a);
    int y0 = ui_wave_y(a);
    char title[24];
    int i;

    lcd_fill_rect(fb, UI_WAVE_X, y0, UI_WAVE_W, ui_wave_h(a), UI_OSC_COLOR_BG);
    lcd_rect(fb, UI_WAVE_X, y0, UI_WAVE_W, ui_wave_h(a), UI_OSC_COLOR_FRAME);
    (void)strcpy(title, "选择波形 ");
    (void)strcat(title, (a->awg.sel == 0u) ? "CH1" : "CH2");
    (void)ui_draw_text(fb, UI_WAVE_X + (int)UI_WAVE_GAP, y0 + 5, title, UI_OSC_COLOR_TEXT, 1);

    for (i = 0; i < n; i++) {
        ui_wave_draw_row(fb, a, i, (i == pressed) ? 1 : 0);
    }
    ui_wave_flush(fb, a);
}

void ui_wave_flush(u32 fb, const ui_app_t *a)
{
    Xil_DCacheFlushRange((UINTPTR)(fb + ((u32)ui_wave_y(a) * LCD_GFX_WIDTH * 3u)),
                         ((u32)ui_wave_h(a) * LCD_GFX_WIDTH * 3u));
}

int ui_wave_hit(const ui_app_t *a, int x, int y)
{
    int n = ui_wave_count(a);
    int y0 = ui_wave_y(a);
    int i;

    if ((x < UI_WAVE_X) || (x >= (UI_WAVE_X + (int)UI_WAVE_W)) ||
        (y < y0) || (y >= (y0 + ui_wave_h(a)))) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        int rx = 0, ry = 0, rw = 0, rh = 0;

        ui_wave_row_rect(a, i, &rx, &ry, &rw, &rh);
        if ((x >= rx) && (x < (rx + rw)) && (y >= ry) && (y < (ry + rh))) {
            return i;
        }
    }
    return -1;      /* 窗口内但不在任何一项上 */
}

void ui_wave_open(ui_app_t *a)
{
    a->wave_open = 1;           /* 三个窗口互斥 */
    a->menu_open = 0;
    a->set_open = 0;
    ui_win_redraw_req = 1;      /* 窗口由主循环在帧边界画 */
}

void ui_wave_close(u32 fb, ui_app_t *a)
{
    (void)fb;
    a->wave_open = 0;
    ui_win_redraw_req = 1;      /* 底板由主循环在帧边界贴回 */
}

/* 当前开着哪个窗口: 0 = 没有, 1 = "界面"菜单, 2 = 频谱仪"设置"子窗口, 3 = "波形"选择 */
static int ui_win_kind(const ui_app_t *a)
{
    if (a->set_open != 0u) {
        return (a->screen == UI_SCREEN_SPEC) ? 2 : 0;   /* 设置窗只属于频谱仪界面 */
    }
    if ((a->screen == UI_SCREEN_SPEC) && (a->spec.czt_popup != 0u)) {
        return 4;                  /* ★ Chirp-Z 模式子窗口: 并入窗口层 */
    }
    if (a->wave_open != 0u) {
        return 3;
    }
    if (a->menu_open != 0u) {
        return 1;
    }
    return 0;
}

static void ui_win_rect(const ui_app_t *a, int *x, int *y, int *w, int *h)
{
    if (ui_win_kind(a) == 4) {
        *x = UI_CZT_POP_X;
        *y = UI_CZT_POP_Y;
        *w = UI_CZT_POP_W;
        *h = UI_CZT_POP_H;
    } else if (ui_win_kind(a) == 2) {
        *x = UI_SET_WIN_X;
        *y = UI_SET_WIN_Y;
        *w = UI_SET_WIN_W;
        *h = UI_SET_WIN_H;
    } else if (ui_win_kind(a) == 3) {
        *x = UI_WAVE_X;
        *y = ui_wave_y(a);
        *w = UI_WAVE_W;
        *h = ui_wave_h(a);
    } else {
        *x = UI_MENU_X;
        *y = ui_app_menu_y();
        *w = UI_MENU_W;
        *h = ui_app_menu_h();
    }
}

static void ui_win_save_under(u32 fb, int x, int y, int w, int h)
{
    int r;

    if ((w <= 0) || (h <= 0) || (w > UI_WIN_SAVE_W) || (h > UI_WIN_SAVE_H) ||
        (x < 0) || (y < 0) || ((x + w) > LCD_GFX_WIDTH) || ((y + h) > LCD_GFX_HEIGHT)) {
        ui_win_saved = 0;
        return;
    }
    for (r = 0; r < h; r++) {
        const u8 *src = (const u8 *)(UINTPTR)(fb +
                         (u32)(y + r) * LCD_GFX_WIDTH * 3u + (u32)x * 3u);
        memcpy(&ui_win_save[r * UI_WIN_SAVE_W * 3], src, (size_t)w * 3u);
    }
    ui_win_saved = 1;
}

static void ui_win_restore_under(u32 fb)
{
    int r;

    if ((ui_win_saved == 0u) || (ui_win_w <= 0) || (ui_win_h <= 0)) {
        return;
    }
    for (r = 0; r < ui_win_h; r++) {
        u8 *dst = (u8 *)(UINTPTR)(fb +
                   (u32)(ui_win_y + r) * LCD_GFX_WIDTH * 3u + (u32)ui_win_x * 3u);
        memcpy(dst, &ui_win_save[r * UI_WIN_SAVE_W * 3], (size_t)ui_win_w * 3u);
    }
    ui_win_saved = 0;
}

/* 按行整条刷(窗口横向只占屏宽的一部分, 但 D-Cache 维护是整行的, 与旧代码一致)。
   矩形与对齐规则(设置窗和"界面"菜单都走这里):
     - 起点 = fb + y*LCD_GFX_WIDTH*3; 长度 = h*LCD_GFX_WIDTH*3。
       LCD_GFX_WIDTH*3 = 2400 = 32*75, 所以起点和长度都天然是 32 字节(cache line)的整数倍;
       一行的字节数也是 32 的整数倍, 于是"整行刷"既不会漏掉窗口左右两侧同一 cache 行里
       被改过的像素, 也不会把范围切在 cache line 中间。
     - 覆盖范围 = y .. y+h-1 整行, 是窗口矩形(x..x+w-1)的超集: 窗口上下边框、左右边框
       全都在里面, 不会出现"只刷了内容区、漏了边框"的问题。
     - 只按行刷、不按 x 裁: VDMA 读的是整行, 而且 cache line 是沿着行方向切的,
       按 x 裁反而可能把一行里同一 cache line 的其它像素留脏。 */
static void ui_win_flush(u32 fb, int y, int h)
{
    Xil_DCacheFlushRange((UINTPTR)(fb + (u32)y * LCD_GFX_WIDTH * 3u),
                         (u32)h * LCD_GFX_WIDTH * 3u);
}

int ui_app_win_need_redraw(void)
{
    return (ui_win_redraw_req != 0u) ? 1 : 0;
}

void ui_app_win_clear_redraw(void)
{
    ui_win_redraw_req = 0;
}

/* ★ 供频谱仪 Chirp-Z 子窗口开/关时请求窗口层重画(ui_win_redraw_req 是本文件的静态) */
void ui_app_win_req(void)
{
    ui_win_redraw_req = 1;
}

/* ---------------- 数字键盘(ui_numkbd.c)的窗口层 ----------------
   键盘是从屏幕下半弹出来的, 盖在设置窗"之上"(不压住第 1 项数值框 y171..220),
   所以它是"第二层窗口": 有自己的底板缓存, 也有自己的在屏标志。

   ★ 为什么不能和设置窗共用 ui_win_save:
     共用一块缓存时, 弹键盘会把"设置窗的底板"覆盖成"带键盘的画面", 关设置窗时贴回去的
     就是这张脏底 —— 屏幕上留下键盘残影。所以键盘用 ui_numkbd_save_under/restore_under
     自己的缓存, 两层各存各的。

   ★ 打开: 只打 kbd.open + 置"帧边界重画"标志(触摸处理里画 480x256 会被液晶扫描追上);
     帧边界: ui_numkbd_save_under() 存底板 -> ui_numkbd_draw() 画面板(自带 D-Cache 刷新)。
   ★ 关闭: 只清 kbd.open + 置标志; 帧边界: ui_numkbd_restore_under() 原样贴回 + 刷新。
   ★ 键盘期间的按键/回显重画在 ui_numkbd_touch() 里就地做(只动一个键/一行, 带 flush)。 */

/* 底板贴回之后必须叫一次: 让频谱区下一帧"整块重画"。
   ★ 为什么需要: 频谱仪的原地刷新靠 ui_spec_t.prev[] 记"屏幕这一列现在画到哪",
     只重画数值变了的列。而"贴底板"是把开窗那一刻的像素原样写回来 ——
     如果这期间频谱区已经被按新刻度重画过(比如图省事走过一次整屏重画),
     贴回就把像素打回旧刻度, 而 prev[] 还停在"新刻度"上, 于是原地刷新认为
     "这些列没变"、再也不补画: 屏幕上就是"倒三角(每帧都重画)跟着新刻度走了,
     但谱线长度一直停在旧刻度"。整块重画一次最省事也最保险。 */
static void ui_app_plot_invalidate(ui_app_t *a)
{
    a->spec.prev_valid = 0u;        /* 下一帧 ui_spec_draw_live() -> ui_spec_draw_plot() */
}

int ui_app_kbd_is_open(const ui_app_t *a)
{
    return (a->kbd.open != 0u) ? 1 : 0;
}


/* 键盘层的"存底板+画" / "贴底板"(只允许主循环在帧边界调, 见上面说明) */
static void ui_app_kbd_refresh(u32 fb, ui_app_t *a)
{
    if (a->kbd.open != 0u) {
        if (ui_kbd_saved == 0u) {
            ui_numkbd_save_under(fb, &a->kbd);      /* 1) 先把面板底下的像素存下来 */
            ui_kbd_saved = 1u;
        }
        ui_numkbd_draw(fb, &a->kbd);                /* 2) 再画面板(末尾自带 D-Cache 刷新) */
        ui_kbd_shown = 1u;
        return;
    }
    if (ui_kbd_saved != 0u) {
        ui_numkbd_restore_under(fb, &a->kbd);       /* 收键盘: 底板原样贴回 + 刷新 */
        ui_kbd_saved = 0u;
        ui_app_plot_invalidate(a);                  /* ★ 贴回的是旧像素: 频谱区下一帧整块重画 */
        /* ★ 键盘盖住的那一行(频率起始/终止)贴回后是"开键盘之前"的旧值:
           紧接着重画一次, 否则用户回车改了值、屏上还显示旧值。 */
        if (a->set_row_dirty != 0u) {
            int row = (int)a->set_row_dirty - 1;

            a->set_row_dirty = 0u;
            ui_app_set_draw_row(fb, &a->spec, row, 0);
            ui_app_set_flush(fb);
        }
        /* ★ FRA: 设置页必须整页重画, 否则上面贴回的旧底板(带旧数值)会把新值盖回去 ——
           频谱仪有自己的补画(ui_app_set_draw_row), FRA 没有, 所以在这里补上。 */
        if ((a->screen == UI_SCREEN_FRA) && (a->fra.set_open != 0u)) {
            ui_fra_redraw_settings(fb, &a->fra);
        }
        /* ★ Chirp-Z 子窗口: 键盘盖住了它的三行输入(起始/终止/M), 而贴回的底板是"键盘打开
           那一刻"的画面(里面还是旧值) -> 必须紧接着按当前值重画一次子窗口,
           否则用户回车改了值、子窗口还显示旧值(实测: 一直显示 0)。 */
        if ((a->screen == UI_SCREEN_SPEC) && (a->spec.czt_popup != 0u)) {
            a->spec.czt_pop_draw = 0u;
            ui_spec_draw_czt_popup(fb, &a->spec);
            ui_win_flush(fb, UI_CZT_POP_Y, UI_CZT_POP_H);
        }
    }
    ui_kbd_shown = 0u;
}

/* 点设置窗第 1 行数值框(抬起): 弹键盘, 初值 = 数值框当前显示串 */
void ui_app_kbd_open_refbox(ui_app_t *a)
{
    char init[16];

    if ((a->screen != UI_SCREEN_SPEC) || (a->set_open == 0u) || (a->kbd.open != 0u)) {
        return;                                     /* 只有"设置窗开着"时才弹 */
    }
    /* ★ 不预填! 原来预填 "3.300" 已占满 3 位小数, 而 kbd_push_char 的规则是"小数位满 dec_max 就丢弃" -> 打开后按数字没反应(与 FLT/FRA/CZT/AWG 同一个坑)。 */
    init[0] = '\0';
    ui_numkbd_open(&a->kbd, UI_APP_KBD_TGT_REF, a->spec.ref_unit, init,
                   (u8)UI_SPEC_REF_DEC, 8u);   /* ★ 6->8: mV 档下 "5500.000" 要 8 字符, 免得被 ui_numkbd_open 截断 */        /* 参考电压: 3 位小数(1mV) / 最长 6 字符 */
    ui_win_redraw_req = 1;                          /* 键盘由主循环在帧边界画 */
}

/* 点设置窗第 3/4 行(频率起始/终止)的数值框(抬起): 弹键盘, 初值 = 数值框当前显示串。
   ★ 键盘的定点值 = 输入数字 x 1000: kHz 档直接就是 Hz, MHz 档再乘 1000。 */
void ui_app_kbd_open_setrow(ui_app_t *a, int row)
{
    char init[16];
    u8   ku;

    if ((a->screen != UI_SCREEN_SPEC) || (a->set_open == 0u) || (a->kbd.open != 0u)) {
        return;                                     /* 只有"设置窗开着"时才弹 */
    }
    if (row == 2) {
        ui_spec_fmt_freq_u(init, ui_spec_f_start(&a->spec), a->spec.f_start_unit);
        init[0] = '\0';    /* XXFSTART: 不预填(3位小数已满 -> 按数字会没反应) */
        ku = (a->spec.f_start_unit == UI_SPEC_FU_MHZ) ? UI_APP_KBD_UNIT_MHZ
                                                      : UI_APP_KBD_UNIT_KHZ;
        ui_numkbd_open(&a->kbd, UI_APP_KBD_TGT_FSTART, ku, init,
                       (u8)((a->spec.f_start_unit == UI_SPEC_FU_MHZ) ? 3u : 3u), 8u);   /* ★ kHz 档 2->3 位: 显示已是 3 位(1Hz), 输入侧不改会"开键盘+确定就掉 5Hz" */
    } else if (row == 3) {
        ui_spec_fmt_freq_u(init, ui_spec_f_stop(&a->spec), a->spec.f_stop_unit);
        init[0] = '\0';    /* XXFSTOP: 同上 */
        ku = (a->spec.f_stop_unit == UI_SPEC_FU_MHZ) ? UI_APP_KBD_UNIT_MHZ
                                                     : UI_APP_KBD_UNIT_KHZ;
        ui_numkbd_open(&a->kbd, UI_APP_KBD_TGT_FSTOP, ku, init,
                       (u8)((a->spec.f_stop_unit == UI_SPEC_FU_MHZ) ? 3u : 3u), 8u);   /* ★ kHz 档 2->3 位: 显示已是 3 位(1Hz), 输入侧不改会"开键盘+确定就掉 5Hz" */
    } else {
        return;
    }
    ui_win_redraw_req = 1;
}

/* 点信号发生器参数区的数值框(抬起): 弹键盘, 初值 = 该参数当前值(按键盘档缩放)。
   提示文字用静态缓冲, 因为 ui_numkbd 只存指针(谐波行名是动态拼的)。 */
/* ★ FRA"设置"页: 点"起始/终止频率"行 -> 弹数字键盘。
   与频谱仪那条路径的唯一区别: 这里要自己置 ui_win_redraw_req,
   否则主循环不知道"有窗口要画", 键盘就不会出现在屏上(只打 kbd.open 是没用的)。
   init 由调用方给(频率格式化函数在 ui_fra.c 里)。 */

/* Chirp-Z mode2 rows: open the numeric keypad.
   NOTE: must set ui_win_redraw_req here (it is a static in this file), otherwise the
   main loop never learns there is a window to draw and the keypad stays invisible. */
void ui_app_kbd_open_czt(ui_app_t *a, int row)
{
    char init[16];

    if ((a->screen != UI_SCREEN_SPEC) || (a->kbd.open != 0u)) {
        return;
    }
    if (row == 0) {
    /* ★ 不预填! 预填 "150.000" 会占满 3 位小数 -> kbd_push_char 丢弃所有新数字(实测"输入没反应")。 */
    (void)strcpy(init, "");
        ui_numkbd_open(&a->kbd, UI_APP_KBD_TGT_CZT_FSTART, UI_APP_KBD_UNIT_KHZ, init, 3u, 10u);   /* XXCZTLEN: 8->10: kHz 单位下 8 字符最多 9999.999kHz(9.999MHz) 打不进 10MHz 以上; 10 字符可到 9999999.999kHz */
    } else if (row == 1) {
    /* ★ 不预填(同 f_start): 否则新输入的数字会被 dec_max 丢掉。 */
    (void)strcpy(init, "");
        ui_numkbd_open(&a->kbd, UI_APP_KBD_TGT_CZT_FSTOP, UI_APP_KBD_UNIT_KHZ, init, 3u, 10u);   /* XXCZTLEN: 同 f_start */
    } else {
        (void)sprintf(init, "%d", (int)a->spec.czt_m);
        ui_numkbd_open(&a->kbd, UI_APP_KBD_TGT_CZT_M, 6u, init, 0u, 5u);
    }
    ui_win_redraw_req = 1;
}

/* ★ 滤波器页第 4 行"截止/频率": 弹数字键盘精确定频率(单位 kHz, 3 位小数 = 1Hz 分辨率)。
   低通/高通编辑 fc(which=0); 带通/带阻编辑 f1(which=0) 与 f2(which=1)。
   只调用已有的 ui_numkbd 模块, 不新写键盘。 */
void ui_app_kbd_open_flt(ui_app_t *a, int which)
{
    char init[16];

    if ((a->screen != UI_SCREEN_FLT) || (a->kbd.open != 0u)) {
        return;
    }
    if (which == 1) {
        init[0] = '\0';                            /* ★ 同上: f2 也不预填 */(void)0;
    } else if ((a->flt.type == UI_FLT_TYPE_BP) || (a->flt.type == UI_FLT_TYPE_BS)) {
        init[0] = '\0';                            /* ★ 同上: f1 也不预填 */(void)0;
    } else {
        /* ★ 不预填当前值! 预填 "%d.%03d" 会占满 3 位小数, 而 kbd_push_char 的规则是
           "小数位已满 dec_max 就丢弃新输入的数字" -> 用户按什么都没反应(实测就是这个 bug)。
           改成留空: 键盘上直接输入新值(单位 kHz, 3 位小数), 回车生效。 */
        init[0] = '\0';
    }
    ui_numkbd_open(&a->kbd,
                    (which == 1) ? UI_APP_KBD_TGT_FLT_F2 : UI_APP_KBD_TGT_FLT_FC,
                    UI_APP_KBD_UNIT_KHZ, init, 3u, 8u);
    ui_win_redraw_req = 1;
}
void ui_app_kbd_open_fra(ui_app_t *a, int row, const char *init)
{
    if ((a->screen != UI_SCREEN_FRA) || (a->kbd.open != 0u)) {
        return;
    }
    ui_numkbd_open(&a->kbd,
                   (row == 0) ? UI_APP_KBD_TGT_FRA_START : UI_APP_KBD_TGT_FRA_STOP,
                   UI_APP_KBD_UNIT_KHZ, init, 3u, 8u);   /* ★ 7->8: 原值会被 ui_numkbd_open 的 n<len_max 截断("1000.000"->"1000.00") */
    ui_win_redraw_req = 1;              /* ★ 键盘由主循环在帧边界画 */
}

void ui_app_kbd_open_awg(ui_app_t *a, u8 id)
{
    static char hint[24];
    const char *nm;
    char init[16];
    u8   ku = UI_AWG_KU_INT;
    u8   dec = 0;
    u8   len = 6;

    if ((a->screen != UI_SCREEN_AWG) || (a->kbd.open != 0u)) {
        return;
    }
    ui_awg_row_kbd(&a->awg, id, &ku, &dec, &len, init);
    nm = ui_awg_row_name(&a->awg, id, hint, (int)sizeof(hint));
    ui_numkbd_set_hint(UI_APP_KBD_TGT_AWG, nm);
    a->awg_kbd_id = id;
    ui_numkbd_open(&a->kbd, UI_APP_KBD_TGT_AWG, ku, init, dec, len);
    a->awg.redraw_params = 1u;          /* 收键盘后参数区重画, 数值框里就是新值 */
    ui_win_redraw_req = 1;              /* 键盘由主循环在帧边界画 */
}

void ui_app_kbd_close(ui_app_t *a){
    a->kbd.open = 0u;
    ui_win_redraw_req = 1;                          /* 底板由主循环在帧边界贴回 */
}

/* 回车确认: 把键盘的定点值按单位档换算成 mV 写进目标, 并重画受影响的那一块。
   目前只有一个目标(参考电压 -> 设置窗第 1 行), 以后 AWG 频率/幅度在这里加分支。 */
void ui_app_kbd_commit(u32 fb, ui_app_t *a)
{
    /* ★ FRA"设置"页的起始/终止频率(专用 target 6/7):
       ★ 必须加在这里 —— 键盘开着时触摸被 ui_touch.c 直接交给 ui_app_kbd_touch,
         收键盘时它调的就是本函数, 所以写在 ui_fra_set_touch 里永远不会执行。
       归一化(用户要求): 任意输入 -> 夹到 1kHz..1MHz, 再四舍五入到最近的 500Hz 整数倍。 */
    if ((a->kbd.target == UI_APP_KBD_TGT_FRA_START) ||
        (a->kbd.target == UI_APP_KBD_TGT_FRA_STOP)) {
        /* ★ 键盘定点值 value = "输入数字 x 1000"; 本项单位固定 kHz,
           所以数字就是 kHz -> value 本身就已经是 Hz(500 kHz -> 500000)。
           原来这里又除了 1000 -> 变成 500 -> 被下面夹成 1000 -> 起始/终止永远显示 1kHz。 */
        s32 hz = a->kbd.value;

        if (hz < 1000)    { hz = 1000; }
        if (hz > 1000000) { hz = 1000000; }
        hz = ((hz + 250) / 500) * 500;                 /* ★ 最近的 500Hz 整数倍 */
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
        a->fra.draw_set = 1u;                          /* 值变了: 帧边界重画设置页 */
        return;
    }
    /* Chirp-Z mode2 three rows: keypad fixed point = number x 1000 (unit is kHz for
       the two frequencies, so value itself is already Hz). */
    if ((a->kbd.target == UI_APP_KBD_TGT_CZT_FSTART) ||
        (a->kbd.target == UI_APP_KBD_TGT_CZT_FSTOP) ||
        (a->kbd.target == UI_APP_KBD_TGT_CZT_M)) {
        /* ★ 键盘定点值 = 输入数字 x 1000。两个频率项单位固定 kHz ⇒ value 本身就是 Hz
           (与上面 FRA 分支同一套: 500kHz -> 500000); 只有"点数 M"是无单位整数, 才要 /1000。
           以前这里对频率也除了 1000 -> 存进去的是 kHz, 子窗口按 /1000 显示 -> 一直是 0。 */
        s32 v = (a->kbd.target == UI_APP_KBD_TGT_CZT_M) ? (a->kbd.value / 1000) : a->kbd.value;

        if (a->kbd.target == UI_APP_KBD_TGT_CZT_M) {
            /* ★ CZT mode2: the PL FFT runs in the 8192-point gear, and czt.c uses
               L = next_pow2(n + m - 1), so we must keep n + m - 1 <= 8192.
                Mode2: n = 8192 => m <= 8192 (L = 16384).
               (was 8193 -> L became 16384 and the FFT gear flipped -> DMA timeouts) */
            if (v < 2)   { v = 2; }
                if (v > 8192) { v = 8192; }     /* ★ 与 n=8192/L=16384 配套 */
            a->spec.czt_m = (u16)v;
        } else {
            if (v < 0)       { v = 0; }
            else {   /* XXCZTLIM: 上限按【当前实际采样率】的 fs/2 留 2% 余量(原来写死 4096000 = 8.192MHz/2) */
                s32 lim = (a->spec.sample_rate / 2) * 98 / 100;

                if (lim < 1000) { lim = 1000; }
                if (v > lim)    { v = lim; }
            }
            if (a->kbd.target == UI_APP_KBD_TGT_CZT_FSTART) { a->spec.czt_f_start = v; }
            else                                            { a->spec.czt_f_end   = v; }
            if (a->spec.czt_f_end <= a->spec.czt_f_start) {
                a->spec.czt_f_end = a->spec.czt_f_start + 1000;
            }
        }
        a->spec.czt_pop_draw = 1u;                  /* repaint popup at frame boundary */
        ui_win_redraw_req = 1;                      /* ★ 请求窗口层消费它(否则子窗口卡住) */
        return;
    }
    /* ★ 滤波器页"截止/频率"键盘回写(target 11): 键盘定点值 = 输入数字 x 1000,
       单位固定 kHz => a->kbd.value 已经是 Hz(例如 "150.000" -> 150000)。 */
    if ((a->kbd.target == UI_APP_KBD_TGT_FLT_FC) ||
        (a->kbd.target == UI_APP_KBD_TGT_FLT_F2)) {
        s32 hz = a->kbd.value;

        /* ★ 空输入(数字被删光)或 0 一律【不修改】: 否则会被下面的下限夹成 1000,
           变成"设置截止频率"却把 fc 偷偷改成 1kHz(显示 1.0kHz, 系数确实变了)。 */
        if (hz <= 0) { a->kbd.ok = 0u; return; }
        if (hz < 1000)    { hz = 1000; }        /* 下限 1kHz(与 ±1kHz 步进时一致) */
        {   /* ★ 上限按【当前实际采样率】的 fs/2 留 2% 余量(原来写死 4000000 = 8.192MHz/2);
               切到 5MHz 后 Nyquist 只有 2.5MHz, 不夹会让设计函数直接返回 0。 */
            s32 lim = (a->flt.fs_hz / 2) * 98 / 100;

            if (lim < 1000) { lim = 1000; }
            if (hz > lim) { hz = lim; }
        }
        if (a->kbd.target == UI_APP_KBD_TGT_FLT_F2) {
            a->flt.f2_hz = hz;                  /* ★ 带通/带阻上边界 f2 */
            if (a->flt.f2_hz <= a->flt.f1_hz) { a->flt.f2_hz = a->flt.f1_hz + 1000; }
        } else if ((a->flt.type == UI_FLT_TYPE_BP) || (a->flt.type == UI_FLT_TYPE_BS)) {
            a->flt.f1_hz = hz;                  /* ★ 带通/带阻下边界 f1 */
            if (a->flt.f2_hz <= a->flt.f1_hz) { a->flt.f2_hz = a->flt.f1_hz + 1000; }
        } else {
            a->flt.fc_hz = hz;                  /* 低通/高通截止 fc */
            {   /* ★ 同样按实际采样率的 fs/2(原来写死 4000000) */
                s32 lim = (a->flt.fs_hz / 2) * 98 / 100;

                if (lim < 1000) { lim = 1000; }
                if (a->flt.fc_hz > lim) { a->flt.fc_hz = lim; }
            }
        }
        a->flt.need_apply    = 1u;              /* 重新设计+下发 */
        a->flt.redraw_params = 1u;              /* 参数区(含新频率)重画 */
        ui_win_redraw_req = 1;                  /* 键盘底板贴回 */
        return;
    }
    if (a->kbd.target == UI_APP_KBD_TGT_REF) {
        if (a->kbd.value <= 0) { a->kbd.ok = 0u; return; }   /* XXREF: 空回车不改参考电压(否则0mV -> 谱线全乱) */
        s32 mv = ui_spec_ref_mv_from_kbd(a->kbd.value, a->kbd.unit);
        ui_spec_ref_set_mv(&a->spec, mv);           /* 夹到 [0, 5500] mV */
        /* 重画设置窗第 1 行: 数值框立刻显示新值 + 整个设置窗区域刷 cache。
           第 1 行在 y171..220, 键盘在 y224..479, 两者不重叠 -> 重画不会画到键盘上,
           键盘底板贴回时也不会把新值擦掉。 */
        ui_app_set_draw_row(fb, &a->spec, 0, 0);
        ui_app_set_flush(fb);
        /* 参考电压只影响"线性"纵轴的上端:
           dB 模式下曲线一个像素都不会变(不必白重画);
           线性模式下要让频谱区下一帧整块重画 —— 原地刷新只重画"数据变了"的那些列,
           刻度变了但数据没变的列会一直留着旧高度的曲线, 看起来就像"参考电压没生效"。 */
        if (a->spec.db == 0u) {
            a->spec.prev_valid = 0u;        /* 下一帧 ui_spec_draw_live() 整块重画 */
        }
    } else if ((a->kbd.target == UI_APP_KBD_TGT_FSTART) ||
               (a->kbd.target == UI_APP_KBD_TGT_FSTOP)) {
        /* 频率起始/终止: 键盘定点值 -> Hz -> 写进显示区间(宽度 = 终止-起始 跟着变)。
           ★ 这两行(y214~319)被键盘(y>=224)盖着: 现在画上去也会被键盘底板贴回冲掉,
             所以只记"键盘关掉后要补画哪一行"(见 ui_app_kbd_refresh),
             频率刻度/信息行则由 ui_spec_set_f_*() 置的 chrome_req 在没弹窗的帧里补画。 */
        u8  fu = (a->kbd.unit == UI_APP_KBD_UNIT_MHZ) ? UI_SPEC_FU_MHZ : UI_SPEC_FU_KHZ;
        s32 hz = ui_spec_freq_hz_from_kbd(a->kbd.value, fu);

        if (a->kbd.target == UI_APP_KBD_TGT_FSTART) {
            ui_spec_set_f_start(&a->spec, hz);
            a->set_row_dirty = 3u;                  /* 0 基第 2 行 = 频率起始 */
        } else {
            ui_spec_set_f_stop(&a->spec, hz);
            a->set_row_dirty = 4u;                  /* 0 基第 3 行 = 频率终止 */
        }
    } else if (a->kbd.target == UI_APP_KBD_TGT_AWG) {
          /* 信号发生器参数区: 键盘定点值 -> 参数内部量纲 -> 写回模型
             (范围夹取在 ui_awg_row_set() 里; 它同时置 need_apply, 由主循环下发到 PL) */
          s32 v = ui_awg_kbd_to_val(&a->awg, a->awg_kbd_id, a->kbd.unit, a->kbd.value);

          ui_awg_row_set(&a->awg, a->awg_kbd_id, v);
          a->awg.redraw_params = 1u;      /* 参数区(数值框)下一帧重画 */
      }
}

/* 键盘开着时的触摸(ui_touch.c 在"键盘开着"时把触摸全交给这里)。
   返回 1 = 本次有动作(收键盘 / 改了回显), 0 = 只是高亮/按下, 与 ui_touch_poll 的口径一致。 */
int ui_app_kbd_touch(u32 fb, ui_app_t *a, int ev, int x, int y)
{
    int keep = ui_numkbd_touch(fb, &a->kbd, x, y, ev);

    if (keep == 0) {                                /* 已收键盘(回车确认 或 取消) */
        if (a->kbd.ok != 0u) {
            ui_app_kbd_commit(fb, a);               /* 回车: 值生效 + 重画第 1 行 */
        }
        ui_app_kbd_close(a);                        /* 是否写值都只打标志, 底板由帧边界贴回 */
        return 1;
    }
    return 0;
}

int ui_app_win_open(const ui_app_t *a)
{
    if (a->kbd.open != 0u) {
        return 1;               /* ★ 键盘开着也算"有窗口": 主循环要暂停原地刷新 */
    }
    /* ★ Chirp-Z 子窗口归窗口层管(kind==4): 开着时暂停底下原地刷新 */
    if ((a->screen == UI_SCREEN_SPEC) && (a->spec.czt_popup != 0u)) {
        return 1;
    }

    return (ui_win_kind(a) != 0) ? 1 : 0;
}

/* 开/关窗口: 只重画窗口那一块(由主循环在帧边界上调用) */
void ui_app_win_refresh(u32 fb, ui_app_t *a)
{
    int x = 0, y = 0, w = 0, h = 0;
    int kind;

    /* ---- 键盘层优先 ----
       键盘是在设置窗"之上"弹出来的, 所以:
         - 开着的时候: 存一次自己的底板 + 画键盘(设置窗原样留在下面);
         - 收起来的时候: 只把自己的底板贴回(底板里已经是"带设置窗"的画面) ——
           绝不能在这一步顺手重画设置窗: 那会把设置窗的底板冲成"带键盘"的画面,
           之后关设置窗就贴不回原样了。 */
    if (a->kbd.open != 0u) {
        ui_app_kbd_refresh(fb, a);
        return;
    }
    if (ui_kbd_shown != 0u) {
        ui_app_kbd_refresh(fb, a);          /* 贴回键盘底板 + 清在屏标志 */
        /* ★ 贴回键盘底板后, 这一屏的真实内容就已经恢复了(键盘下面的设置窗/波形选择窗/
           界面菜单都在底板里), 所以必须在这里结束。
           以前只有"频谱仪设置窗还开着"才 return, 信号发生器界面会掉到函数末尾
           "没有底板"的兜底分支: 黑填一块 + ui_redraw_req = 1 -> 下一帧整屏重画,
           表现就是"数字键盘消失时整个界面闪一下"。 */
        return;
    }

    kind = ui_win_kind(a);
    if (kind != 0) {
        ui_win_rect(a, &x, &y, &w, &h);
        ui_win_x = x;
        ui_win_y = y;
        ui_win_w = w;
        ui_win_h = h;
        if (ui_win_saved == 0u) {   /* ★ 子窗开着再进窗口层时不能把"带窗口的画面"存成底板(否则关窗贴回=擦不掉); 切屏处已清标志, 所以不会误跳过 */
            ui_win_save_under(fb, x, y, w, h);
        }
        if (kind == 2) {
            ui_app_set_draw(fb, &a->spec);      /* 再画窗口 */
          } else if (kind == 4) {
            ui_win_was_czt = 1u;               /* ★ 记住: 关闭时要按它补画底部面板 */
            /* ★ Chirp-Z 子窗口: 内容变了才重画本体 + 刷窗口那几行 */
            if (a->spec.czt_pop_draw != 0u) {
                a->spec.czt_pop_draw = 0u;
                ui_spec_draw_czt_popup(fb, &a->spec);
                ui_win_flush(fb, UI_CZT_POP_Y, UI_CZT_POP_H);
            }
          } else if (kind == 3) {
              ui_wave_draw(fb, a, -1);            /* 波形选择窗 */
          } else {
            ui_app_menu_draw(fb, a, -1);
        }
        return;
    }
    if (ui_win_saved != 0u) {
        ui_win_restore_under(fb);               /* 关窗: 底板原样贴回 */
        ui_win_flush(fb, ui_win_y, ui_win_h);
        if (ui_win_was_czt != 0u) {
            /* ★ 子窗口下沿(360)压进底部读数面板(336..439): 贴回底板只是恢复打开前的面板像素,
               必须按当前内容重画面板并刷那一段 —— 这里必须用"存底板时"的记录,
               因为此刻 kind 已经变回 0(czt_popup 已清零), 用 kind==4 判断永远不会成立。 */
            ui_win_was_czt = 0u;
            ui_spec_draw_params(fb, &a->spec);
        ui_win_flush(fb, 369, (LCD_GFX_HEIGHT - 369));   /* ★ 起点 336->369: 刻度带已下移到 369, 上面 336..368 是绘图区(由 plot 的 flush 覆盖), 这里白刷 33 行 */

        }
            /* ★ 回退(复查A 的❌1/❌2): 原来这里被我改成"prev_valid=1 + 只清被弹窗盖住的 420 列 on", 但 prev_valid=1 会把已挂起的 prev_valid=0 吃掉
               -> 切模式进模式2 时那次整块重画永不发生(旧频谱/旧光标与新CZT曲线叠加); 且"只清 on 不擦旧段"会让弹窗区域残留旧谱线。
               现在恢复为整块作废: 关窗后第一帧整块重画一次(代价 712KB, 画面干净)。 */
            ui_app_plot_invalidate(a);              /* ★ 频谱区下一帧整块重画 */

    /* ★ 示波器也要作废"上一帧迹": 贴回的是开窗那一刻的像素, 而 ui_prev[ch][x]/触发几何
       仍记着开窗前的状态。若开窗期间波形移动过, 下一帧逐列比较会认为"这些列没变",
       把贴回的旧迹永久留在屏上(与频谱仪 prev_valid 同型的故障)。 */
    ui_osc_invalidate();
        return;
    }
    /* 没有底板(窗口是整屏重画时画上去的): 擦掉窗口, 下一帧整屏重画兜底 */
    if (ui_win_w <= 0) {                        /* 还没画过窗口: 按"界面"菜单的位置擦 */
        ui_win_x = UI_MENU_X;
        ui_win_y = ui_app_menu_y();
        ui_win_w = UI_MENU_W;
        ui_win_h = ui_app_menu_h();
    }
    lcd_fill_rect(fb, ui_win_x, ui_win_y, ui_win_w, ui_win_h, UI_OSC_COLOR_BG);
    ui_win_flush(fb, ui_win_y, ui_win_h);
    ui_redraw_req = 1;
}

/* 旧名字: 语义与 ui_app_win_* 完全相同(验证台 host_main.c 与老调用点还在用) */
int ui_app_menu_need_redraw(void)
{
    return ui_app_win_need_redraw();
}

void ui_app_menu_clear_redraw(void)
{
    ui_app_win_clear_redraw();
}

void ui_app_menu_refresh(u32 fb, ui_app_t *a)
{
    ui_app_win_refresh(fb, a);
}

void ui_app_menu_open(ui_app_t *a)
{
    a->menu_open = 1;
    a->set_open = 0;            /* 两个窗口互斥: 菜单拉起时不该还开着设置窗 */
    ui_win_redraw_req = 1;      /* 窗口由主循环在帧边界上画 */
}

void ui_app_menu_close(u32 fb, ui_app_t *a)
{
    (void)fb;
    a->menu_open = 0;
    ui_win_redraw_req = 1;      /* 底板由主循环在帧边界上贴回 */
}

void ui_app_set_open(ui_app_t *a)
{
    if ((a->screen != UI_SCREEN_SPEC) || (a->menu_open != 0u) || (a->set_open != 0u)) {
        return;                 /* 只有频谱仪界面、且没有别的窗口开着时才弹 */
    }
    a->set_open = 1;
    ui_win_redraw_req = 1;      /* 窗口由主循环在帧边界上画(触摸处理里画会被扫描追上) */
}

void ui_app_set_close(u32 fb, ui_app_t *a)
{
    (void)fb;
    a->set_open = 0;
    ui_win_redraw_req = 1;      /* 底板由主循环在帧边界上贴回 */
}

/* ---------------- 频谱仪"设置"子窗口 ----------------
   需求: 屏幕正中弹出子窗口, 里面是"相关可设置选项"的界面(标题 + 选项名/当前值);
         选项本身的点击功能暂不实现(点行只做高亮), 点窗口外或右上角"关"按钮把窗口关掉。
   第 1 行"参考电压": 行名 + 数值框(显示当前参考电压) + 单位切换按钮(显示当前单位, 点一下循环 V->mV->Vpp->mVpp);
         点单位按钮在 V/mV 之间切换(只改显示/输入解释, 内部恒为整数 mV);
         数值框本轮点了只高亮, 不弹数字键盘。
   这里只负责"画"和"命中判断"; 底板保存/贴回交给上面的 ui_win_* (与"界面"菜单共用)。 */

/* 第 i 行占几个"单位行高"(UI_SET_ROW_H): 现在只剩四项(参考电压/采样率/频率起始/频率终止),
   都是"两行合并"的项, 各占 2 —— 共 4 x 2 = 8 个单位行(224px)。 */
static int ui_set_row_units(int i)
{
    return (i < UI_SET_ROW_N) ? 2 : 1;
}

/* 第 i 行的矩形(行内上下各留 3px); y 由前面各行占的高度累加出来 */
static void ui_set_row_rect(int i, int *x, int *y, int *w, int *h)
{
    int units = 0;
    int k;

    for (k = 0; k < i; k++) {
        units += ui_set_row_units(k);
    }
    *x = UI_SET_WIN_X + 10;
    *y = UI_SET_ROW_Y + (units * UI_SET_ROW_H) + 3;
    *w = UI_SET_WIN_W - 20;
    *h = (ui_set_row_units(i) * UI_SET_ROW_H) - 6;
}

const char *ui_app_set_row_name(int i)
{
    /* ★ 第 1 项"参考电压"; 第 2 项"采样率"由原来的"纵轴 + 跨度"两行合并而来
       (纵轴的 dB/线性按钮和跨度按钮都在频谱仪的按钮栏上, 设置窗里不再各占一行);
       第 3 项"采样"显示时钟**实际**在用的采样率(宿主反馈), 与第 2 项"选择哪一档"互补;
       第 4 项"分辨" = 采样率 / 谱线数; 第 5 项"峰值" = 本帧峰值幅度。 */
    static const char *const name[UI_SET_ROW_N] = {
        "参考电压", "采样率", "频率起始", "频率终止", "窗"
    };

    return ((i >= 0) && (i < UI_SET_ROW_N)) ? name[i] : "";
}

/* 每行的"当前值"直接取自频谱仪当前状态, 所以窗口里显示的就是屏上的实时设置 */
static void ui_set_row_value(const ui_spec_t *s, int i, char *buf)
{
    switch (i) {
    case 0:
        /* 第 1 行"参考电压": 内部恒为整数 mV, 显示串跟着单位档走(V/Vpp: "3.300" / mV/mVpp: "3300") */
        ui_spec_fmt_ref_mv(buf, s->ref_mv, s->ref_unit);
        break;
    case 1:
        /* 第 2 项"采样率": 由 ui_app_set_draw_fs_row() 专门画(数值框 + 上下箭头) */
        ui_spec_fs_text(s, buf);
        break;
    case 2:
        /* 第 3 项"频率起始": 显示区间的起点(单位跟这一项自己的 kHz/MHz 档走) */
        ui_spec_fmt_freq_u(buf, ui_spec_f_start(s), s->f_start_unit);
        break;
    case 3:
        /* 第 4 项"频率终止": 显示区间的终点 */
        ui_spec_fmt_freq_u(buf, ui_spec_f_stop(s), s->f_stop_unit);
        break;
    case 4:
        /* ★ 第 5 行窗: 当前 FFT 窗名(点这一行循环切换) */
        (void)strcpy(buf, ui_spec_win_name(s->win));
        break;
    default:
        (void)strcpy(buf, "--");
        break;
    }
}

/* 第 1 行"参考电压"的绘制: 行名 + 数值框 + 单位切换按钮。
   row_pressed  = 行名区(整行)被按下 -> 整行反色(与其它行完全一致);
   box_pressed  = 数值框单独被按下   -> 只反色数值框(点了不弹键盘, 只高亮);
   unit_pressed = 单位按钮单独被按下 -> 只反色单位按钮。
   ★ 反色规则与按钮栏/其它行同一套: 底色 = UI_OSC_COLOR_TEXT, 字色 = UI_OSC_COLOR_BG。 */
void ui_app_set_draw_ref_row(u32 fb, const ui_spec_t *s, int row_pressed,
                             int box_pressed, int unit_pressed)
{
    int x = 0, y = 0, w = 0, h = 0;
    int tw = 0;
    char val[24];
    u16 bg, fg, box_bg, box_fg, unit_bg, unit_fg;
    const char *un;

    ui_set_row_rect(0, &x, &y, &w, &h);

    if (row_pressed != 0) {
        bg = UI_OSC_COLOR_TEXT;   fg = UI_OSC_COLOR_BG;
    } else {
        bg = UI_OSC_COLOR_TABBG;  fg = UI_OSC_COLOR_TEXT;
    }
    /* 整行按下时数值框/单位按钮跟着一起反色, 整行看起来是均匀反色的 */
    if ((row_pressed != 0) || (box_pressed != 0)) {
        box_bg = UI_OSC_COLOR_TEXT;   box_fg = UI_OSC_COLOR_BG;
    } else {
        box_bg = UI_OSC_COLOR_TABBG;  box_fg = UI_OSC_COLOR_TEXT;
    }
    if ((row_pressed != 0) || (unit_pressed != 0)) {
        unit_bg = UI_OSC_COLOR_TEXT;  unit_fg = UI_OSC_COLOR_BG;
    } else {
        unit_bg = UI_OSC_COLOR_TABBG; unit_fg = UI_OSC_COLOR_TEXT;
    }

    /* 行底 + 行边框 + 行名 */
    lcd_fill_rect(fb, x, y, w, h, bg);
    lcd_rect(fb, x, y, w, h, UI_OSC_COLOR_FRAME);
    (void)ui_draw_text(fb, x + 12, y + ((h - 16) / 2), ui_app_set_row_name(0), fg, 1);

    /* 数值框: 有边框, 底色与行一致(只在按下时反色); 框内居中显示当前参考电压 */
    lcd_fill_rect(fb, UI_SET_REF_BOX_X, UI_SET_REF_Y,
                  UI_SET_REF_BOX_W, UI_SET_REF_BOX_H, box_bg);
    lcd_rect(fb, UI_SET_REF_BOX_X, UI_SET_REF_Y,
             UI_SET_REF_BOX_W, UI_SET_REF_BOX_H, UI_OSC_COLOR_FRAME);
    ui_set_row_value(s, 0, val);
    tw = (int)ui_text_size(val, 1);
    (void)ui_draw_text(fb, UI_SET_REF_BOX_X + ((UI_SET_REF_BOX_W - tw) / 2),
                       UI_SET_REF_Y + ((UI_SET_REF_BOX_H - 16) / 2), val, box_fg, 1);

    /* 单位切换按钮: 有边框, 显示当前单位(V/mV/Vpp/mVpp), 点一下走一格, 居中 */
    lcd_fill_rect(fb, UI_SET_REF_UNIT_X, UI_SET_REF_Y,
                  UI_SET_REF_UNIT_W, UI_SET_REF_UNIT_H, unit_bg);
    lcd_rect(fb, UI_SET_REF_UNIT_X, UI_SET_REF_Y,
             UI_SET_REF_UNIT_W, UI_SET_REF_UNIT_H, UI_OSC_COLOR_FRAME);
    un = ui_spec_ref_unit_name(s->ref_unit);
    tw = (int)ui_text_size(un, 1);
    (void)ui_draw_text(fb, UI_SET_REF_UNIT_X + ((UI_SET_REF_UNIT_W - tw) / 2),
                       UI_SET_REF_Y + ((UI_SET_REF_UNIT_H - 16) / 2), un, unit_fg, 1);
}

/* 箭头按钮里的实心三角(不依赖字库: 字库里没有 "▲/▼" 这两个字形)。
   up != 0 = 尖朝上(升档), 否则尖朝下(降档); pressed != 0 反色。 */
static void ui_set_draw_arrow(u32 fb, int x, int y, int w, int h, int up, int pressed)
{
    u16 bg = (pressed != 0) ? UI_OSC_COLOR_TEXT : UI_OSC_COLOR_TABBG;
    u16 fg = (pressed != 0) ? UI_OSC_COLOR_BG   : UI_OSC_COLOR_TEXT;
    int aw = 16;                        /* 三角底宽(偶数, 好居中) */
    int ah = 10;                        /* 三角高 */
    int ax = x + ((w - aw) / 2);
    int ay = y + ((h - ah) / 2);
    int i;

    lcd_fill_rect(fb, x, y, w, h, bg);
    lcd_rect(fb, x, y, w, h, UI_OSC_COLOR_FRAME);
    for (i = 0; i < ah; i++) {
        int half = (i * (aw / 2)) / (ah - 1);        /* 从尖到宽 */
        int yy = (up != 0) ? (ay + i) : (ay + ah - 1 - i);
        lcd_hline(fb, ax + (aw / 2) - half, ax + (aw / 2) + half, yy, fg);
    }
}

/* 第 2 行"采样率": 行名 + 数值框(与参考电压的框对齐) + 最右侧上下箭头 */
void ui_app_set_draw_fs_row(u32 fb, const ui_spec_t *s, int row_pressed,
                            int up_pressed, int dn_pressed)
{
    int x = 0, y = 0, w = 0, h = 0;
    int tw;
    char val[24];
    u16 bg, fg, box_bg, box_fg;

    ui_set_row_rect(1, &x, &y, &w, &h);

    if (row_pressed != 0) {
        bg = UI_OSC_COLOR_TEXT;   fg = UI_OSC_COLOR_BG;
    } else {
        bg = UI_OSC_COLOR_TABBG;  fg = UI_OSC_COLOR_TEXT;
    }
    if (row_pressed != 0) {
        box_bg = UI_OSC_COLOR_TEXT;  box_fg = UI_OSC_COLOR_BG;
    } else {
        box_bg = UI_OSC_COLOR_TABBG; box_fg = UI_OSC_COLOR_TEXT;
    }

    /* 行底 + 行边框 + 行名 */
    lcd_fill_rect(fb, x, y, w, h, bg);
    lcd_rect(fb, x, y, w, h, UI_OSC_COLOR_FRAME);
    (void)ui_draw_text(fb, x + 12, y + ((h - 16) / 2), ui_app_set_row_name(1), fg, 1);

    /* 数值框: 显示当前档(如 "8.192MHz"); 与参考电压的数值框左右对齐 */
    lcd_fill_rect(fb, UI_SET_FS_BOX_X, y, UI_SET_FS_BOX_W, h, box_bg);
    lcd_rect(fb, UI_SET_FS_BOX_X, y, UI_SET_FS_BOX_W, h, UI_OSC_COLOR_FRAME);
    ui_spec_fs_text(s, val);
    tw = (int)ui_text_size(val, 1);
    (void)ui_draw_text(fb, UI_SET_FS_BOX_X + ((UI_SET_FS_BOX_W - tw) / 2),
                       y + ((h - 16) / 2), val, box_fg, 1);

    /* 最右侧: 上箭头(升档) / 下箭头(降档) */
    ui_set_draw_arrow(fb, UI_SET_FS_UP_X, y, UI_SET_FS_ARW_W, h, 1,
                      ((row_pressed != 0) || (up_pressed != 0)) ? 1 : 0);
    ui_set_draw_arrow(fb, UI_SET_FS_DN_X, y, UI_SET_FS_ARW_W, h, 0,
                      ((row_pressed != 0) || (dn_pressed != 0)) ? 1 : 0);
}

/* 第 3/4 行"频率起始 / 频率终止": 行名 + 数值框 + 单位按钮(kHz/MHz)。
   与第 1 行"参考电压"同一套排法(数值框/单位按钮的 x 与它对齐, y 跟着本行)。 */
void ui_app_set_draw_freq_row(u32 fb, const ui_spec_t *s, int i, int row_pressed,
                              int box_pressed, int unit_pressed)
{
    int x = 0, y = 0, w = 0, h = 0;
    int tw;
    char val[24];
    u16 bg, fg, box_bg, box_fg, unit_bg, unit_fg;
    const char *un;
    s32 hz   = (i == 3) ? ui_spec_f_stop(s) : ui_spec_f_start(s);
    u8  unit = (i == 3) ? s->f_stop_unit : s->f_start_unit;

    ui_set_row_rect(i, &x, &y, &w, &h);

    if (row_pressed != 0) {
        bg = UI_OSC_COLOR_TEXT;   fg = UI_OSC_COLOR_BG;
    } else {
        bg = UI_OSC_COLOR_TABBG;  fg = UI_OSC_COLOR_TEXT;
    }
    if ((row_pressed != 0) || (box_pressed != 0)) {
        box_bg = UI_OSC_COLOR_TEXT;   box_fg = UI_OSC_COLOR_BG;
    } else {
        box_bg = UI_OSC_COLOR_TABBG;  box_fg = UI_OSC_COLOR_TEXT;
    }
    if ((row_pressed != 0) || (unit_pressed != 0)) {
        unit_bg = UI_OSC_COLOR_TEXT;  unit_fg = UI_OSC_COLOR_BG;
    } else {
        unit_bg = UI_OSC_COLOR_TABBG; unit_fg = UI_OSC_COLOR_TEXT;
    }

    /* 行底 + 行边框 + 行名 */
    lcd_fill_rect(fb, x, y, w, h, bg);
    lcd_rect(fb, x, y, w, h, UI_OSC_COLOR_FRAME);
    (void)ui_draw_text(fb, x + 12, y + ((h - 16) / 2), ui_app_set_row_name(i), fg, 1);

    /* 数值框: 与参考电压的数值框左右对齐 */
    lcd_fill_rect(fb, UI_SET_REF_BOX_X, y, UI_SET_REF_BOX_W, h, box_bg);
    lcd_rect(fb, UI_SET_REF_BOX_X, y, UI_SET_REF_BOX_W, h, UI_OSC_COLOR_FRAME);
    ui_spec_fmt_freq_u(val, hz, unit);
    tw = (int)ui_text_size(val, 1);
    (void)ui_draw_text(fb, UI_SET_REF_BOX_X + ((UI_SET_REF_BOX_W - tw) / 2),
                       y + ((h - 16) / 2), val, box_fg, 1);

    /* 单位按钮: kHz <-> MHz */
    lcd_fill_rect(fb, UI_SET_REF_UNIT_X, y, UI_SET_REF_UNIT_W, h, unit_bg);
    lcd_rect(fb, UI_SET_REF_UNIT_X, y, UI_SET_REF_UNIT_W, h, UI_OSC_COLOR_FRAME);
    un = ui_spec_freq_unit_name(unit);
    tw = (int)ui_text_size(un, 1);
    (void)ui_draw_text(fb, UI_SET_REF_UNIT_X + ((UI_SET_REF_UNIT_W - tw) / 2),
                       y + ((h - 16) / 2), un, unit_fg, 1);
}

void ui_app_set_draw_row(u32 fb, const ui_spec_t *s, int i, int pressed)
{
    int x = 0, y = 0, w = 0, h = 0;
    char val[24];
    u16 bg, fg;

    if ((i < 0) || (i >= UI_SET_ROW_N)) {
        return;
    }
    if (i == 0) {
        /* 第 1 行是"参考电压"(行名 + 数值框 + 单位按钮), 交给专门的画法。
           整行按下 = 三块一起反色, 所以三个标志都传 pressed。 */
        ui_app_set_draw_ref_row(fb, s, pressed, pressed, pressed);
        return;
    }
    if (i == 1) {
        /* 第 2 行是"采样率"(行名 + 数值框 + 上下箭头): 整行按下 -> 三块一起反色 */
        ui_app_set_draw_fs_row(fb, s, pressed, pressed, pressed);
        return;
    }
    if ((i == 2) || (i == 3)) {
        /* 第 3/4 行是"频率起始/频率终止"(行名 + 数值框 + kHz/MHz 按钮), 同一套做法 */
        ui_app_set_draw_freq_row(fb, s, i, pressed, pressed, pressed);
        return;
    }
    ui_set_row_rect(i, &x, &y, &w, &h);
    if (pressed != 0) {                     /* 高亮/按下: 反色(与按钮栏同一套观感) */
        bg = UI_OSC_COLOR_TEXT;
        fg = UI_OSC_COLOR_BG;
    } else {
        bg = UI_OSC_COLOR_TABBG;
        fg = UI_OSC_COLOR_TEXT;
    }
    lcd_fill_rect(fb, x, y, w, h, bg);
    lcd_rect(fb, x, y, w, h, UI_OSC_COLOR_FRAME);
    (void)ui_draw_text(fb, x + 12, y + ((h - 16) / 2), ui_app_set_row_name(i), fg, 1);

    ui_set_row_value(s, i, val);
    (void)ui_draw_text(fb, x + w - 12 - (int)ui_text_size(val, 1), y + ((h - 16) / 2),
                       val, fg, 1);
}

void ui_app_set_draw(u32 fb, const ui_spec_t *s)
{
    int i;

    /* 黑底 + 白框, 与"界面"上拉菜单完全一致的配色 */
    lcd_fill_rect(fb, UI_SET_WIN_X, UI_SET_WIN_Y, UI_SET_WIN_W, UI_SET_WIN_H,
                  UI_OSC_COLOR_BG);
    lcd_rect(fb, UI_SET_WIN_X, UI_SET_WIN_Y, UI_SET_WIN_W, UI_SET_WIN_H,
             UI_OSC_COLOR_FRAME);

    /* 标题: 白字 32px(与顶栏标题同字号), 下面一条灰色分隔线 */
    (void)ui_draw_text(fb, UI_SET_WIN_X + 12, UI_SET_WIN_Y + 6, "设置",
                       UI_OSC_COLOR_TEXT, 2);
    lcd_hline(fb, UI_SET_WIN_X + 1, UI_SET_WIN_X + UI_SET_WIN_W - 2,
              UI_SET_WIN_Y + UI_SET_TITLE_H - 1, UI_OSC_COLOR_GRID);

    /* 关闭按钮: 白框 + "关"字(字库里没有"闭", 用"关"最直观) */
    lcd_fill_rect(fb, UI_SET_CLOSE_X, UI_SET_CLOSE_Y, UI_SET_CLOSE_W, UI_SET_CLOSE_H,
                  UI_OSC_COLOR_TABBG);
    lcd_rect(fb, UI_SET_CLOSE_X, UI_SET_CLOSE_Y, UI_SET_CLOSE_W, UI_SET_CLOSE_H,
             UI_OSC_COLOR_FRAME);
    (void)ui_draw_text(fb, UI_SET_CLOSE_X + ((UI_SET_CLOSE_W - 16) / 2),
                       UI_SET_CLOSE_Y + ((UI_SET_CLOSE_H - 16) / 2), "关",
                       UI_OSC_COLOR_TEXT, 1);

    /* 选项行: 只显示"名字 + 当前值", 点击暂不实现(点行只高亮) */
    for (i = 0; i < UI_SET_ROW_N; i++) {
        ui_app_set_draw_row(fb, s, i, 0);
    }

    /* 底部提示(只用 ASCII: 字库里没有"点/窗/外/暂"这些字, 缺字会画成空方框) */
    (void)ui_draw_text(fb, UI_SET_WIN_X + 12, UI_SET_WIN_Y + UI_SET_WIN_H - 22,
                       "preview only - tap outside to close", UI_OSC_COLOR_DIM, 1);

    /* ★★ 画完必须立刻刷 D-Cache ★★
       与 ui_app_menu_draw() 末尾调用 ui_app_menu_flush() 完全对称 —— 之前漏了这一步,
       所以"点齿轮开设置窗"这条路径在 PC 上没问题(PC 没有 cache), 在板子上花屏:
       ui_app_win_refresh() 在帧边界只调 ui_app_set_draw() 就 return 了, 窗口那 480x248
       像素全留在 Cortex-A9 的 write-back D-Cache 里, VDMA 从 DDR 读到的还是旧内容;
       紧接着主循环的 ADC/FFT 处理(adc_buf/spec_mag/DMA 缓冲)会把 cache 行随机挤出去,
       于是屏幕上只有"被挤出去的那几行"是新窗口内容, 其余还是旧画面 -> 花屏/残影/撕裂。
       PC 验证台逐像素比对帧缓冲, 看不到 cache 这一层, 所以抓不到这个 bug。 */
    ui_app_set_flush(fb);
}

void ui_app_set_flush(u32 fb)
{
    ui_win_flush(fb, UI_SET_WIN_Y, UI_SET_WIN_H);
}

int ui_app_set_win_hit(int x, int y)
{
    return ((x >= UI_SET_WIN_X) && (x < (UI_SET_WIN_X + UI_SET_WIN_W)) &&
            (y >= UI_SET_WIN_Y) && (y < (UI_SET_WIN_Y + UI_SET_WIN_H))) ? 1 : 0;
}

int ui_app_set_close_hit(int x, int y)
{
    return ((x >= UI_SET_CLOSE_X) && (x < (UI_SET_CLOSE_X + UI_SET_CLOSE_W)) &&
            (y >= UI_SET_CLOSE_Y) && (y < (UI_SET_CLOSE_Y + UI_SET_CLOSE_H))) ? 1 : 0;
}

/* 第 1 行"参考电压"里两块控件的命中(矩形就是画它们时用的矩形, 保证"画哪就点哪") */
int ui_app_set_ref_box_hit(int x, int y)
{
    return ((x >= UI_SET_REF_BOX_X) && (x < (UI_SET_REF_BOX_X + UI_SET_REF_BOX_W)) &&
            (y >= UI_SET_REF_Y) && (y < (UI_SET_REF_Y + UI_SET_REF_BOX_H))) ? 1 : 0;
}

int ui_app_set_ref_unit_hit(int x, int y)
{
    return ((x >= UI_SET_REF_UNIT_X) && (x < (UI_SET_REF_UNIT_X + UI_SET_REF_UNIT_W)) &&
            (y >= UI_SET_REF_Y) && (y < (UI_SET_REF_Y + UI_SET_REF_UNIT_H))) ? 1 : 0;
}

/* 第 2 行"采样率"两个箭头的命中(矩形就是画它们时用的矩形) */
int ui_app_set_fs_up_hit(int x, int y)
{
    return ((x >= UI_SET_FS_UP_X) && (x < (UI_SET_FS_UP_X + UI_SET_FS_ARW_W)) &&
            (y >= UI_SET_FS_Y) && (y < (UI_SET_FS_Y + UI_SET_FS_ARW_H))) ? 1 : 0;
}

int ui_app_set_fs_dn_hit(int x, int y)
{
    return ((x >= UI_SET_FS_DN_X) && (x < (UI_SET_FS_DN_X + UI_SET_FS_ARW_W)) &&
            (y >= UI_SET_FS_Y) && (y < (UI_SET_FS_Y + UI_SET_FS_ARW_H))) ? 1 : 0;
}

/* 第 3/4 行(i = 2 频率起始 / 3 频率终止)的数值框与单位按钮命中:
   矩形跟画的时候同源(行矩形 + 数值框/单位按钮的 x, 高度 = 行高), 保证"画哪就点哪" */
int ui_app_set_freq_box_hit(int i, int x, int y)
{
    int rx = 0, ry = 0, rw = 0, rh = 0;

    if ((i != 2) && (i != 3)) {
        return 0;
    }
    ui_set_row_rect(i, &rx, &ry, &rw, &rh);
    return ((x >= UI_SET_REF_BOX_X) && (x < (UI_SET_REF_BOX_X + UI_SET_REF_BOX_W)) &&
            (y >= ry) && (y < (ry + rh))) ? 1 : 0;
}

int ui_app_set_freq_unit_hit(int i, int x, int y)
{
    int rx = 0, ry = 0, rw = 0, rh = 0;

    if ((i != 2) && (i != 3)) {
        return 0;
    }
    ui_set_row_rect(i, &rx, &ry, &rw, &rh);
    return ((x >= UI_SET_REF_UNIT_X) && (x < (UI_SET_REF_UNIT_X + UI_SET_REF_UNIT_W)) &&
            (y >= ry) && (y < (ry + rh))) ? 1 : 0;
}

int ui_app_set_hit(int x, int y)
{
    int i;

    if (ui_app_set_win_hit(x, y) == 0) {
        return UI_SET_HIT_NONE;
    }
    /* 第 1 行里的数值框/单位按钮有各自的命中值: 它们落在第 1 行矩形内部,
       所以必须排在"逐行判断"前面, 否则会被当成"第 0 行"。 */
    if (ui_app_set_ref_box_hit(x, y) != 0) {
        return UI_SET_HIT_REFBOX;
    }
    if (ui_app_set_ref_unit_hit(x, y) != 0) {
        return UI_SET_HIT_REFUNIT;
    }
    /* 第 2 行"采样率"的上下箭头同理: 也落在第 2 行矩形内部, 必须先判 */
    if (ui_app_set_fs_up_hit(x, y) != 0) {
        return UI_SET_HIT_FSUP;
    }
    if (ui_app_set_fs_dn_hit(x, y) != 0) {
        return UI_SET_HIT_FSDN;
    }
    /* 第 3/4 行"频率起始/频率终止"的数值框与单位按钮: 同样排在各行判断之前 */
    if (ui_app_set_freq_box_hit(2, x, y) != 0) {
        return UI_SET_HIT_FSTBOX;
    }
    if (ui_app_set_freq_unit_hit(2, x, y) != 0) {
        return UI_SET_HIT_FSTUNIT;
    }
    if (ui_app_set_freq_box_hit(3, x, y) != 0) {
        return UI_SET_HIT_FSPBOX;
    }
    if (ui_app_set_freq_unit_hit(3, x, y) != 0) {
        return UI_SET_HIT_FSPUNIT;
    }
    for (i = 0; i < UI_SET_ROW_N; i++) {
        int rx = 0, ry = 0, rw = 0, rh = 0;
        ui_set_row_rect(i, &rx, &ry, &rw, &rh);
        if ((x >= rx) && (x < (rx + rw)) && (y >= ry) && (y < (ry + rh))) {
            return i;
        }
    }
    return UI_SET_HIT_NONE;    /* 点在窗口内但不在任何一行/控件上 */
}

/* ---------------- 通道条上的通道名(屏幕开关通道) ---------------- */
/* 只有示波器界面有通道条; 命中范围 = 通道条那一行的左侧标签区(色条 + "CH1"/"CH2" 文字) */
#define UI_APP_TAB_HIT_W 140

int ui_app_tab_hit(const ui_app_t *a, int x, int y)
{
    int ch;

    if (a->screen != UI_SCREEN_SCOPE) {
        return -1;
    }
    if ((y < UI_OSC_BOTTOM_Y) || (y >= (UI_OSC_BOTTOM_Y + UI_OSC_CH_TAB_H))) {
        return -1;
    }
    for (ch = 0; ch < 2; ch++) {
        int px = (ch == 0) ? 0 : UI_OSC_CH_PANEL_W;
        if ((x >= (px + 2)) && (x < (px + UI_APP_TAB_HIT_W))) {
            return ch;
        }
    }
    return -1;
}

void ui_app_channel_toggle(u32 fb, ui_app_t *a, int ch)
{
    if (a->screen != UI_SCREEN_SCOPE) {
        return;
    }
    ui_osc_ch_toggle(&a->osc, ch);
    ui_osc_draw_info(fb, &a->osc);      /* 信息行: 触发源可能已自动切到另一通道 */
    ui_osc_draw_tabs(fb, &a->osc);      /* 通道条: 关闭时整条变暗 */
    ui_osc_draw_values(fb, &a->osc);    /* 测量值: 关闭时显示 "--" */
    ui_osc_flush_values(fb);
    ui_osc_flush_chrome(fb);
    /* 波形本身由下一帧的原地刷新擦掉: ui_draw_trace 会跳过该通道并清掉记录 */
}

/* ---------------- 按钮: 标签 / 动作 ---------------- */
int ui_app_btn_count(const ui_app_t *a)
{
    if (a->screen == UI_SCREEN_AWG) {
        return UI_BTN_COUNT_AWG;
    }
    if (a->screen == UI_SCREEN_FRA) {
        return UI_BTN_COUNT_FRA;
    }
    if (a->screen == UI_SCREEN_FLT) {           /* ★ 第五界面: 数字滤波器 */
        return UI_BTN_COUNT_FLT;
    }
    return (a->screen == UI_SCREEN_SPEC) ? UI_BTN_COUNT_SPEC : UI_BTN_COUNT;
}

int ui_app_btn_menu(const ui_app_t *a)
{
    return ui_app_btn_count(a) - 1;
}

/* 按钮顺序(每种界面各自定义, 最后一个固定是"界面"):
 *   示波器:     自动设置 触发+ 触发- 运行停止 时基+ 时基- 档位+ 档位- CH1/CH2 界面
 *   信号发生器: 波形 通道 输出 频率 幅度 相位 界面 (7 个; 后三个直接弹数字键盘)
 *   频谱仪:     通道 运行停止 纵轴 跨度- 跨度+ 跨度复位 Search Peak Next Peak Last Peak 界面
 */
void ui_app_btn_label(const ui_app_t *a, int btn, char *buf)
{
    if (btn == ui_app_btn_menu(a)) {            /* 最后一个是"界面" */
        (void)strcpy(buf, "界面");
        return;
    }

    if (a->screen == UI_SCREEN_AWG) {
        const ui_awg_t *w = &a->awg;
        const ui_awg_ch_t *c = &w->ch[w->sel & 1u];

        switch (btn) {
        case 0: (void)strcpy(buf, "波形 "); (void)strcat(buf, ui_awg_wave_short(c->wave)); break;
        case 1: (void)strcpy(buf, "通道 "); (void)strcat(buf, (w->sel == 0u) ? "CH1" : "CH2");  break;
        case 2: (void)strcpy(buf, "输出 "); (void)strcat(buf, c->on ? "开" : "关");            break;
        case 3: (void)strcpy(buf, "频率");  break;
        case 4: (void)strcpy(buf, "幅度");  break;
        case 5: (void)strcpy(buf, "相位");  break;
        default: buf[0] = '\0';            break;
        }
        return;
    }
    if (a->screen == UI_SCREEN_FLT) {           /* ★ 第五界面: 数字滤波器(应用 / 输出 / 界面) */
        switch (btn) {
        case 0: (void)strcpy(buf, "应用");  break;
        case 1: (void)strcpy(buf, "输出 "); (void)strcat(buf, (a->flt.out_on != 0u) ? "开" : "关"); break;
        case 2:                                  /* ★ 采样率: 标签显示当前档(只读那张表, 表不改) */
            (void)sprintf(buf, "采样率 %d.%03dM",
                          (int)(a->flt.fs_hz / 1000000), (int)((a->flt.fs_hz % 1000000) / 1000));
            break;
        default: buf[0] = '\0';            break;
        }
        return;
    }



    if (a->screen == UI_SCREEN_FRA) {
        const ui_fra_t *f = &a->fra;

        switch (btn) {
        case 0: (void)strcpy(buf, (f->running != 0u) ? "停止" : "运行");                          break;
        case 1: (void)strcpy(buf, "阶数 "); (void)strcat(buf, (f->order == 1u) ? "1" : ((f->order == 2u) ? "2" : ((f->order == 3u) ? "3" : ((f->order == 4u) ? "4" : ((f->order == 5u) ? "5" : "6"))))); break;
        case 2: (void)strcpy(buf, "平均 "); (void)strcat(buf, (f->avg == 1u) ? "1" : ((f->avg == 2u) ? "2" : ((f->avg == 4u) ? "4" : "8"))); break;
        case 3: (void)strcpy(buf, "曲线 "); (void)strcat(buf, ui_fra_view_short(f->view)); break;
        case 4: (void)strcpy(buf, "设置");   break;
        case 5: (void)strcpy(buf, "<-");     break;      /* ★ 光标左移(驱动待做) */
        case 6: (void)strcpy(buf, "->");     break;      /* ★ 光标右移(驱动待做) */
        default: buf[0] = '\0';             break;
        }
        return;
    }

    if (a->screen == UI_SCREEN_SPEC) {
        const ui_spec_t *s = &a->spec;
        switch (btn) {
        case 0:
            (void)strcpy(buf, "通道 ");
            (void)strcat(buf, ui_spec_ch_name(s->ch));
            break;
        case 1:
            (void)strcpy(buf, "运行停止");
            break;
        case 2:
            (void)strcpy(buf, "纵轴 ");
            (void)strcat(buf, (s->db != 0u) ? "dB" : "线性");
            break;
        case 3:
            (void)strcpy(buf, "跨度 -");
            break;
        case 4:
            (void)strcpy(buf, "跨度 +");
            break;
        case 5:
            (void)strcpy(buf, "跨度复位");
            break;
        case 6:
            (void)strcpy(buf, "Search");
            break;
        case 7:
            (void)strcpy(buf, "Peak ");
            (void)strcat(buf, (ui_spec_cur_marked(s) != 0) ? "开" : "关");
            break;
        case 8:
            (void)strcpy(buf, "Next Peak");
            break;
        case 9:
            (void)strcpy(buf, "Last Peak");
            break;
        default:
            buf[0] = '\0';
            break;
        }
        return;
    }

    /* 示波器 */
    switch (btn) {
    case 0:
        (void)strcpy(buf, "自动设置");
        break;
    case 1:
        (void)strcpy(buf, "触发+");
        break;
    case 2:
        (void)strcpy(buf, "触发-");
        break;
    case 3:
        (void)strcpy(buf, "运行停止");
        break;
    case 4:
        (void)strcpy(buf, "时基+ ");
        (void)strcat(buf, (a->osc.sel_ch == 0u) ? "CH1" : "CH2");
        break;
    case 5:
        (void)strcpy(buf, "时基- ");
        (void)strcat(buf, (a->osc.sel_ch == 0u) ? "CH1" : "CH2");
        break;
    case 6:
        (void)strcpy(buf, "档位+ ");
        (void)strcat(buf, (a->osc.sel_ch == 0u) ? "CH1" : "CH2");
        break;
    case 7:
        (void)strcpy(buf, "档位- ");
        (void)strcat(buf, (a->osc.sel_ch == 0u) ? "CH1" : "CH2");
        break;
    case 8:
        (void)strcpy(buf, "CH1/CH2");
        break;
    default:
        buf[0] = '\0';
        break;
    }
}

void ui_app_btn_action(ui_app_t *a, int btn)
{
        if (a->screen == UI_SCREEN_AWG) {
            switch (btn) {
              case 0: ui_wave_open(a);                break;   /* 波形: 弹出选择子窗口 */
            case 1: ui_awg_sel_toggle(&a->awg);     break;   /* 通道: 只换编辑对象 */
            case 2: ui_awg_output_toggle(&a->awg);
                    if ((a->awg.ch[0].on != 0u) || (a->awg.ch[1].on != 0u)) {
                        a->flt.out_on        = 0u;      /* ★ 交出 DAC1, 完全禁用滤波器 */
                        a->flt.need_apply    = 1u;      /* 让它按 out_on=0 重新处理(交回 AWG) */
                        a->flt.redraw_params = 1u;
                        a->flt.redraw_bar    = 1u;
                    }
                    break;   /* 输出开关 */
            case 3: ui_app_kbd_open_awg(a, UI_AWG_P_FREQ);  break;   /* 直接弹频率键盘 */
            case 4: ui_app_kbd_open_awg(a, UI_AWG_P_VPP);   break;   /* 幅度 */
            case 5: ui_app_kbd_open_awg(a, UI_AWG_P_PHASE); break;   /* 相位 */
            default:                                break;
            }
            return;
        }
    if (a->screen == UI_SCREEN_FLT) {           /* ★ 第五界面: 数字滤波器 */
        if (btn == 0) {                         /* 应用: 请求重新设计+下发系数 */
            a->flt.need_apply    = 1u;
            a->flt.redraw_params = 1u;
            a->flt.redraw_bar    = 1u;      /* ★ 状态(已下发/饱和/非法)是在 filt_apply_from_ui 之后才更新的,
                                               必须刷信息行, 否则顶栏永远停在旧状态 */
        } else if (btn == 1) {                  /* ★ 输出: 只切 on/off(其他输出是信号发生器的事) */
            a->flt.out_on        = (a->flt.out_on == 0u) ? 1u : 0u;
            a->flt.need_apply    = 1u;
            a->flt.redraw_params = 1u;      /* 信息行文本不含 out_on -> 不需要 redraw_bar */
        } else if (btn == 2) {                  /* ★ 采样率: 在【已有的整数档表】里循环(表本身一个字节都不改) */
            int nfs = ui_osc_fs_scope_count();
            int ifs;

            if (nfs <= 0) { return; }
            ifs = (int)a->flt.fs_idx + 1;
            if (ifs >= nfs) { ifs = 0; }
            a->flt.fs_idx = (u8)ifs;
            a->flt.fs_hz  = (s32)((ui_osc_fs_scope_at(ifs) * 1000000.0) + 0.5);
            a->flt.fs_req = 1u;             /* 请求主循环在帧边界切换(统一走 adc_fs_apply) */
            a->flt.need_apply    = 1u;      /* ★ 采样率一变, 必须重新设计+下发, 否则系数与 fs 不符 */
            a->flt.redraw_params = 1u;
            a->flt.redraw_bar    = 1u;      /* 按钮标签与顶栏都要刷 */
        }
        return;
    }



    if (a->screen == UI_SCREEN_FRA) {
        /* F5/F8: 扫频期间主循环冻结所有绘制(helloworld.c 的"运行中: 屏幕刷新全部冻结"),
           此时打开"曲线选择/设置页"会变成隐形窗口: 屏幕上看不见, 却把后续所有触摸
           都吞掉, 连"停止"都点不到(用户会以为死机)。所以扫频中只允许"运行/停止"。
           注意: 这一句必须放在 switch 之前 —— 放进 switch 里第一个 case 之前是死代码。 */
        if ((a->fra.running != 0u) && (btn != 0)) {
            return;         /* 扫频中: 其余按钮一律忽略(要改参数请先按"停止") */
        }
        switch (btn) {
        case 0:
            if (a->fra.running != 0u) {
                a->fra.running = 0u;                       /* 停止: 直接停(界面不变) */
                a->fra.run_win = 0u;                       /* ★ 停止后必须复位窗口状态, 否则"正在运行"窗残留在屏上无人擦 */
                a->fra.redraw_pane = 1u;                   /* ★ 让曲线区重画一次, 把残留窗口覆盖掉 */
            } else {
                int est = ui_fra_est_sec(&a->fra);         /* 预计用时(秒) */

                if ((a->fra.warn_s > 0) && (est > (int)a->fra.warn_s)) {
                    /* ★ 超阈值: 只把确认窗画到曲线上(帧边界), 不整屏重画 -> 不闪 */
                    a->fra.confirm_open = 1u;
                    a->fra.draw_confirm = 1u;
                } else {
                    /* ★ 开始扫频: 屏幕内容一点没变(只有按钮标签要变, 那条路径自己会刷),
                       所以这里什么都不要重画 —— 以前置 ui_redraw_req 会整屏清屏, 就是闪的原因。 */
                    a->fra.running = 1u;
                    a->fra.confirm_open = 0u;
                }
            }
            break;
        /* 纵轴按钮已删除(单位恒定: 幅频 dB / 相频 度 / 时域 V) */
        case 1: a->fra.order   = 2u; break;   /* ★ 阶数固定二阶(PL 只出二阶), 按钮不再改数 */
        case 2: a->fra.avg     = (u8)((a->fra.avg   >= 8u) ? 1u : (a->fra.avg * 2u));   break;
        case 3:                                  /* 曲线: 弹选择窗(只画弹窗/只重画曲线区, 不整屏重画, 免得闪) */
            if (a->fra.pick_open == 0u) {
                a->fra.pick_open = 1u;
                a->fra.draw_popup = 1u;          /* 打开: 帧边界把弹窗画到曲线上 */
                /* ★ 互斥: 关掉"设置页"并整块铺底擦掉它 —— 否则设置页会"隐形但还活着":
                   曲线窗一关, 设置页被曲线区覆盖看不见, 却仍在吃掉上半屏触摸。 */
                a->fra.set_open    = 0u;
                a->fra.redraw_pane = 1u;
            } else {
                a->fra.pick_open = 0u;
                a->fra.redraw_pane = 1u;         /* 关闭: 帧边界只恢复曲线区 */
            }
            break;
        case 4:                                  /* 设置页: 只覆盖曲线区(不整屏重画, 免得闪) */
            if (a->fra.set_open == 0u) {
                a->fra.set_open = 1u;
                a->fra.pick_open   = 0u;             /* ★ 互斥: 关掉"曲线选择"弹窗, 否则它虽被设置页盖住却仍在吃触摸 */
                a->fra.draw_set = 1u;            /* 打开: 帧边界把设置页画到曲线区上 */
            } else {
                a->fra.set_open = 0u;
                a->fra.redraw_pane = 1u;         /* 关闭: 帧边界只恢复曲线区 */
            }
                break;
        case 5:                                  /* <- 光标左移(仅在幅频/相频页有效) */
            if ((a->fra.npt > 0u) && (a->fra.view <= 1u)) {   /* ★ 冲激/阶跃页: 箭头失灵(频率光标在那里没有意义) */
                a->fra.cur_on = 1u;
                if (a->fra.cur_idx > 0u) { a->fra.cur_idx--; }
                a->fra.cur_draw = 1u;      /* 只重画光标: 先擦旧->存干净像素->画新, 窄带flush */
            }
            break;
        case 6:                                  /* -> 光标右移(仅在幅频/相频页有效) */
            if ((a->fra.npt > 0u) && (a->fra.view <= 1u)) {   /* ★ 冲激/阶跃页: 箭头失灵(频率光标在那里没有意义) */
                if (a->fra.cur_on == 0u) { a->fra.cur_on = 1u; a->fra.cur_idx = 0u; }
                else if (a->fra.cur_idx < (u16)(a->fra.npt - 1u)) { a->fra.cur_idx++; }
                a->fra.cur_draw = 1u;      /* 只重画光标: 先擦旧->存干净像素->画新, 窄带flush */
            }
            break;
        default: break;
        }
        /* ★ 这里绝不能置 ui_redraw_req: 那会触发整屏 lcd_clear+重画 -> 按键时整屏闪一下。
           变化只影响顶栏和信息行, 交给 ui_app_after_button() 局部重画那一条带。 */
        return;
    }

    if (a->screen == UI_SCREEN_SPEC) {
        switch (btn) {
        case 0: ui_spec_ch_toggle(&a->spec);        break;
        case 1: ui_spec_run_stop(&a->spec);         break;
        case 2: ui_spec_db_toggle(&a->spec);        break;
        case 3: ui_spec_span_step(&a->spec, -1);    break;
        case 4: ui_spec_span_step(&a->spec, +1);    break;
        case 5: ui_spec_span_reset(&a->spec);       break;
        case 6: ui_spec_peak_search(&a->spec);      break;
        case 7: ui_spec_peak_val_toggle(&a->spec);  break;
        case 8: ui_spec_peak_next(&a->spec, +1);    break;
        case 9: ui_spec_peak_next(&a->spec, -1);    break;
        default:                                    break;
        }
        return;
    }

    switch (btn) {
    case 0: ui_osc_autoset(&a->osc);            break;
    case 1: ui_osc_trig_level_step(&a->osc, +1); break;
    case 2: ui_osc_trig_level_step(&a->osc, -1); break;
    case 3: ui_osc_run_stop(&a->osc);            break;
    case 4: ui_osc_timebase_step(&a->osc, +1);   break;
    case 5: ui_osc_timebase_step(&a->osc, -1);   break;
    case 6: ui_osc_vdiv_step(&a->osc, (int)a->osc.sel_ch, +1); break;
    case 7: ui_osc_vdiv_step(&a->osc, (int)a->osc.sel_ch, -1); break;
    case 8: ui_osc_sel_ch_toggle(&a->osc);       break;
    default:                                     break;
    }
}
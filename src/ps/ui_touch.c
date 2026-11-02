/******************************************************************************
 * ui_touch.c - 触摸按钮栏(10 个) + "界面"上拉窗口 + 频谱仪"设置"子窗口
 *
 *   自动设置|触发+|触发-|运行停止|时基+|时基-|档位+|档位-|CH1/CH2|界面   (示波器)
 *   通道|波形|频率-|频率+|频率/10|频率x10|幅度-|幅度+|输出|界面          (信号发生器)
 *
 *   频谱仪另外在顶栏右上角有一个"设置"齿轮按钮(ui_spec_draw_set_btn), 点它在屏幕正中
 *   弹出设置子窗口(ui_app_set_*); 窗口里第 1 行是"参考电压"(数值框 + 单位切换按钮 V/mV/Vpp/mVpp,
 *   点单位按钮切换显示单位, 内部恒为整数 mV); 点数值框弹出九宫格数字键盘(ui_numkbd.c,
 *   可复用), 键盘里回车把值写进参考电压, 取消/点面板外丢弃草稿;
 *   其余选项行只高亮, "关"按钮和"点窗口外"才有动作。
 *
 *   ★ 触摸层级(从上到下): 数字键盘 > 频谱仪设置窗 > "界面"菜单 > 齿轮/通道条 > 按钮栏。
 *
 * 动作后只刷新受影响的静态区域与按钮栏本身, 不整屏重画 -> 不闪。
 * 切界面时只置"需要整屏重画"标志, 由主循环在帧边界执行(避免撕裂)。
 * 开/关窗口也只置标志(ui_app_menu_open / ui_app_set_open / ..._close), 窗口与底板
 * 由主循环在帧边界处理 -> 不会写到一半被液晶扫描追上。
 ******************************************************************************/
#include "ui_touch.h"
#include "ui_app.h"
#include "touch.h"
#include "lcd_gfx.h"
#include "xil_cache.h"
#include <string.h>

static int ui_btn_pressed = UI_BTN_NONE;    /* 按钮栏当前按下的按钮 */
/* ★ 长按连发用: 按下时刻 + 本次按住是否已连发过(文件级, DOWN/HOLD/UP 三处共用) */
#define GT_LO        (*(volatile u32 *)0xF8F00200u)   /* Cortex-A9 全局定时器 低 32 位 */
#define GT_HI        (*(volatile u32 *)0xF8F00204u)   /* 高 32 位(不用 xtime_l.h) */
#define GT_PER_MS    333000u                          /* 每毫秒计数 = CPU/2/1000 (667MHz) */
#define REP_START_MS  1000u                           /* 长按多久开始连发 */
#define REP_STEP_MS     80u                           /* 连发间隔(改小=更快) */
static u64 ui_rep_down = 0u;
static int ui_rep_did  = 0;
static u64 ui_rep_next = 0u;   /* 下一次连发的起跑线(按下时清零) */
static int ui_menu_pressed = -1;            /* 窗口里当前按下的界面序号 */
static int ui_wave_pressed = -1;           /* 波形选择窗里当前按下的项 */
static int ui_set_btn_pressed = 0;          /* 频谱仪"设置"齿轮按钮当前是否按下 */
static int ui_czt_btn_pressed = 0;           /* ★ Chirp-Z 按钮当前是否按下 */
static int ui_flt_tap = 0;                   /* ★ 数字滤波器页: 按下时命中的部位(0=无) */
static int ui_set_hit_pressed = UI_SET_HIT_NONE; /* 设置窗里当前按下的行(>=0)/控件(UI_SET_HIT_*) */

static int ui_awg_tap_hit = UI_AWG_HIT_NONE;
static u8  ui_awg_tap_id = 0u;

/* 按"命中值"画按下态(pressed!=0)/普通态(pressed==0)。
   行号 -> 整行反色; 第 1 行的数值框/单位按钮 -> 只反色那一块。 */
static void ui_set_draw_hit(u32 fb, const ui_app_t *a, int h, int pressed)
{
    if (h == UI_SET_HIT_REFBOX) {
        ui_app_set_draw_ref_row(fb, &a->spec, 0, pressed, 0);
    } else if (h == UI_SET_HIT_REFUNIT) {
        ui_app_set_draw_ref_row(fb, &a->spec, 0, 0, pressed);
    } else if (h == UI_SET_HIT_FSUP) {
        ui_app_set_draw_fs_row(fb, &a->spec, 0, pressed, 0);
    } else if (h == UI_SET_HIT_FSDN) {
        ui_app_set_draw_fs_row(fb, &a->spec, 0, 0, pressed);
    } else if (h == UI_SET_HIT_FSTBOX) {
        ui_app_set_draw_freq_row(fb, &a->spec, 2, 0, pressed, 0);
    } else if (h == UI_SET_HIT_FSTUNIT) {
        ui_app_set_draw_freq_row(fb, &a->spec, 2, 0, 0, pressed);
    } else if (h == UI_SET_HIT_FSPBOX) {
        ui_app_set_draw_freq_row(fb, &a->spec, 3, 0, pressed, 0);
    } else if (h == UI_SET_HIT_FSPUNIT) {
        ui_app_set_draw_freq_row(fb, &a->spec, 3, 0, 0, pressed);
    } else if (h >= 0) {
        ui_app_set_draw_row(fb, &a->spec, h, pressed);
    }
    /* h == UI_SET_HIT_NONE: 什么都不画 */
}

/* ---------------- 内部 ---------------- */
/* 按钮矩形(上下各留 4px, 左右各留 2px) */
static void ui_btn_rect(int id, int n, int *x, int *y, int *w, int *h)
{
    *x = UI_BTN_X(id, n) + 2;
    *y = UI_OSC_BTN_Y + 4;
    *w = UI_BTN_WID(id, n) - 4;
    *h = UI_OSC_BTN_H - 8;
}

/* 画一个按钮; pressed != 0 时反色(白底黑字) */
static void ui_btn_draw(u32 fb, const ui_app_t *a, int id, int pressed)
{
    int x = 0, y = 0, w = 0, h = 0;
    int tw;
    char lab[24];
    u16 bg = (pressed != 0) ? UI_OSC_COLOR_TEXT : UI_OSC_COLOR_TABBG;
    u16 fg = (pressed != 0) ? UI_OSC_COLOR_BG   : UI_OSC_COLOR_TEXT;

    ui_app_btn_label(a, id, lab);
    ui_btn_rect(id, ui_app_btn_count(a), &x, &y, &w, &h);
    lcd_fill_rect(fb, x, y, w, h, bg);
    lcd_rect(fb, x, y, w, h, UI_OSC_COLOR_FRAME);

    tw = (int)ui_text_size(lab, 1);
    if (tw < w) {
        (void)ui_draw_text(fb, x + (w - tw) / 2, y + (h - 16) / 2, lab, fg, 1);
    }
}

/* 命中测试: 返回按钮 ID, 未命中返回 UI_BTN_NONE */
static int ui_btn_hit(int x, int y, int n)
{
    int i;

    if ((y < UI_OSC_BTN_Y) || (y >= (UI_OSC_BTN_Y + UI_OSC_BTN_H))) {
        return UI_BTN_NONE;
    }
    for (i = 0; i < n; i++) {
        int bx = UI_BTN_X(i, n);
        if ((x >= bx) && (x < (bx + UI_BTN_WID(i, n)))) {
            return i;
        }
    }
    return UI_BTN_NONE;
}

/* 切换高亮(内部用) */
static void ui_btn_highlight(u32 fb, const ui_app_t *a, int old_id, int new_id)
{
    if (old_id == new_id) {
        return;
    }
    if (old_id != UI_BTN_NONE) {
        ui_btn_draw(fb, a, old_id, 0);
    }
    if (new_id != UI_BTN_NONE) {
        ui_btn_draw(fb, a, new_id, 1);
    }
    ui_touch_flush_bar(fb);
}

/* ---------------- 对外接口 ---------------- */
void ui_touch_draw_bar(u32 fb, const ui_app_t *a, int pressed)
{
    int i;

    lcd_fill_rect(fb, 0, UI_OSC_BTN_Y, LCD_GFX_WIDTH, UI_OSC_BTN_H,
                  UI_OSC_COLOR_BG);
    lcd_hline(fb, 0, LCD_GFX_WIDTH - 1, UI_OSC_BTN_Y, UI_OSC_COLOR_FRAME);
    for (i = 0; i < ui_app_btn_count(a); i++) {
        ui_btn_draw(fb, a, i, (i == pressed) ? 1 : 0);
    }
    ui_touch_flush_bar(fb);
}

void ui_touch_flush_bar(u32 fb)
{
    Xil_DCacheFlushRange((UINTPTR)(fb + (u32)UI_OSC_BTN_Y * LCD_GFX_WIDTH * 3u),
                         (u32)UI_OSC_BTN_H * LCD_GFX_WIDTH * 3u);
}

/* "波形"选择子窗口的触摸处理: 按下高亮 / 滑动换高亮 / 同项抬起则选中并关窗 / 点空白关窗。
   ★ 选完只打标志(redraw_preview/redraw_params), 由主循环在帧边界重画预览与参数区。 */
static int ui_wave_touch(u32 fb, ui_app_t *a, int ev, int mx, int my)
{
    int wh = ui_wave_hit(a, mx, my);

    if (ev == TOUCH_EV_DOWN) {
        ui_wave_pressed = wh;
        if (wh >= 0) {
            ui_wave_draw_row(fb, a, wh, 1);
            ui_wave_flush(fb, a);
        }
        return 0;
    }
    if (ev == TOUCH_EV_MOVE) {
        if (wh != ui_wave_pressed) {
            if (ui_wave_pressed >= 0) {
                ui_wave_draw_row(fb, a, ui_wave_pressed, 0);
            }
            ui_wave_pressed = wh;
            if (wh >= 0) {
                ui_wave_draw_row(fb, a, wh, 1);
            }
            ui_wave_flush(fb, a);
        }
        return 0;
    }
    if (ev == TOUCH_EV_UP) {
        u8 list[UI_AWG_W_CNT];
        int n = (int)ui_awg_wave_list(a->awg.sel, list);
        int sel = ui_wave_pressed;
        int picked = 0;

        ui_wave_pressed = -1;
        if ((wh >= 0) && (wh == sel) && (wh < n)) {
            ui_awg_wave_set(&a->awg, list[wh]);     /* 选中该通道的这个波形 */
            a->awg.redraw_params = 1u;
            a->awg.redraw_preview = 1u;
            picked = 1;
        }
        ui_wave_close(fb, a);                       /* 选完或点空白: 都关窗 */
        if (picked != 0) {
            /* ★ 按钮标签里带波形名、信息行里也带: 必须立刻重画, 否则显示的还是旧波形名。
               (旧版是按钮动作那条路顺带重画按钮栏的; 走弹窗就没有那一步了)
               这两块都很小(按钮栏 800x40 / 顶栏+信息行 800x72), 在触摸处理里画不会撕屏。 */
            ui_touch_draw_bar(fb, a, UI_BTN_NONE);
            ui_awg_draw_status(fb, &a->awg);
            ui_awg_flush_chrome(fb);
        }
        return 1;
    }
    return 0;                                       /* TOUCH_EV_HOLD */
}

/* "界面"窗口打开时的触摸处理 */
static int ui_menu_touch(u32 fb, ui_app_t *a, int ev, int mx, int my)
{
    int mh = ui_app_menu_hit(mx, my);

    if (ev == TOUCH_EV_DOWN) {
        ui_menu_pressed = mh;
        if (mh >= 0) {
            ui_app_menu_draw_row(fb, a, mh, 1);
            ui_app_menu_flush(fb);
        }
        return 0;
    }
    if (ev == TOUCH_EV_MOVE) {
        if (mh != ui_menu_pressed) {
            if (ui_menu_pressed >= 0) {
                ui_app_menu_draw_row(fb, a, ui_menu_pressed, 0);
            }
            ui_menu_pressed = mh;
            if (mh >= 0) {
                ui_app_menu_draw_row(fb, a, mh, 1);
            }
            ui_app_menu_flush(fb);
        }
        return 0;
    }
    if (ev == TOUCH_EV_UP) {
        int sel = ui_menu_pressed;
        ui_menu_pressed = -1;
        if ((mh >= 0) && (mh == sel)) {
            if (mh != (int)a->screen) {
                a->menu_open = 0;               /* 切界面: 由主循环整屏重画 */
                ui_app_switch(a, mh);
            } else {
                ui_app_menu_close(fb, a);       /* 点的就是当前界面: 只关窗 */
            }
        } else {
            ui_app_menu_close(fb, a);           /* 点空白处: 关窗 */
        }
        ui_touch_draw_bar(fb, a, UI_BTN_NONE);
        return 1;
    }
    return 0;                                   /* TOUCH_EV_HOLD */
}

/* 频谱仪"设置"子窗口打开时的触摸处理。
   需求: 选项行的点击功能暂不实现 —— 点行只做高亮, 不做任何动作;
         第 1 行"参考电压"的数值框点一下只高亮(数字键盘第二步再做),
         单位按钮点一下在 V/mV 之间切换(只影响显示与输入解释, 内部恒为 mV);
         点"关"按钮或点窗口外把窗口关掉。
   ★ 关窗只置标志(ui_app_set_close), 底板由主循环在帧边界贴回 —— 与"界面"菜单同一套。
   ★ 行/控件的高亮与"切单位后的重画"都走已有的 ui_app_set_draw_*() + ui_app_set_flush(),
     不直接写帧缓冲, 所以 D-Cache 该刷的地方一次都不会漏。 */
static int ui_set_touch(u32 fb, ui_app_t *a, int ev, int mx, int my)
{
    int h = ui_app_set_hit(mx, my);

    if (ev == TOUCH_EV_DOWN) {
        ui_set_hit_pressed = h;
        if (h != UI_SET_HIT_NONE) {
            ui_set_draw_hit(fb, a, h, 1);                   /* 高亮按下的那一行/那一块 */
            ui_app_set_flush(fb);
        }
        return 0;
    }
    if (ev == TOUCH_EV_MOVE) {
        if (h != ui_set_hit_pressed) {
            ui_set_draw_hit(fb, a, ui_set_hit_pressed, 0);
            ui_set_hit_pressed = h;
            ui_set_draw_hit(fb, a, h, 1);
            ui_app_set_flush(fb);
        }
        return 0;
    }
    if (ev == TOUCH_EV_UP) {
        int was = ui_set_hit_pressed;

        ui_set_hit_pressed = UI_SET_HIT_NONE;
        if (was == UI_SET_HIT_REFUNIT) {
            /* 单位按钮: 在按钮上抬起才算"点中" -> 切换 V/mV */
            if (h == UI_SET_HIT_REFUNIT) {
                ui_spec_ref_unit_toggle(&a->spec);          /* 只翻单位档, ref_mv 不变 */
            }
            /* 重画第 1 行: 既恢复普通态, 又让数值框按新单位重新格式化 */
            ui_app_set_draw_row(fb, &a->spec, 0, 0);
            ui_app_set_flush(fb);
        } else if (was == UI_SET_HIT_REFBOX) {
            /* 数值框: 抬起先恢复原样, 然后在框上抬起就弹出数字键盘(第二步);
               弹键盘只打状态 + 置"帧边界重画"标志(见 ui_app_kbd_open_refbox)。 */
            ui_app_set_draw_ref_row(fb, &a->spec, 0, 0, 0);
            ui_app_set_flush(fb);
            if (h == UI_SET_HIT_REFBOX) {
                ui_app_kbd_open_refbox(a);
            }
        } else if ((was == UI_SET_HIT_FSUP) || (was == UI_SET_HIT_FSDN)) {
            /* 第 2 行"采样率"的上下箭头: 在同一个箭头上抬起才算点中 -> 升/降一档。
               ★ 这里只改 ui_spec_t.fs_req, 真正写 MMCM 由主循环做(触摸处理在帧中间,
                 不能在这里动采样时钟)。 */
            if (h == was) {
                ui_spec_fs_step(&a->spec, (was == UI_SET_HIT_FSUP) ? 1 : -1);
            }
            ui_app_set_draw_fs_row(fb, &a->spec, 0, 0, 0);  /* 恢复普通态 + 显示新档 */
            ui_app_set_flush(fb);
        } else if ((was == UI_SET_HIT_FSTBOX) || (was == UI_SET_HIT_FSPBOX)) {
            /* 频率起始/终止的数值框: 先恢复原样, 在框上抬起就弹数字键盘
               (键盘只在帧边界画/收, 见 ui_app_kbd_open_setrow) */
            int row = (was == UI_SET_HIT_FSTBOX) ? 2 : 3;

            ui_app_set_draw_freq_row(fb, &a->spec, row, 0, 0, 0);
            ui_app_set_flush(fb);
            if (h == was) {
                ui_app_kbd_open_setrow(a, row);
            }
        } else if ((was == UI_SET_HIT_FSTUNIT) || (was == UI_SET_HIT_FSPUNIT)) {
            /* 频率单位按钮: 在同一个按钮上抬起才算点中 -> kHz <-> MHz
               (只翻单位档, 频率值一个 Hz 都不动) */
            int row = (was == UI_SET_HIT_FSTUNIT) ? 2 : 3;

            if (h == was) {
                if (was == UI_SET_HIT_FSTUNIT) {
                    ui_spec_freq_unit_toggle(&a->spec.f_start_unit);
                } else {
                    ui_spec_freq_unit_toggle(&a->spec.f_stop_unit);
                }
            }
            ui_app_set_draw_freq_row(fb, &a->spec, row, 0, 0, 0);   /* 恢复普通态 + 新单位 */
            ui_app_set_flush(fb);
        } else if (was >= 0) {
            /* ★ 最后一行(第 5 行"窗"): 点一下循环切换 汉宁 -> 汉明 -> 布莱克曼。
               这里只改状态(ui_spec_win_toggle), 真正的乘窗在 helloworld.c 里调用
               fft_windows.c 的 hanning_windows()/hamming_windows()/blackman_windows()。 */
            if ((was == (UI_SET_ROW_N - 1)) && (h == was)) {
                ui_spec_win_toggle(&a->spec);
                a->spec.info_req = 1u;      /* 信息栏要显示"当前窗", 轻量刷一次 */
            }
            ui_app_set_draw_row(fb, &a->spec, was, 0);      /* 先把高亮还原(不执行任何选项功能) */
            ui_app_set_flush(fb);
        }
        if ((ui_app_set_close_hit(mx, my) != 0) || (ui_app_set_win_hit(mx, my) == 0)) {
            ui_app_set_close(fb, a);                        /* 关闭按钮 或 窗口外: 关窗 */
        }
        return 1;
    }
    return 0;                                   /* TOUCH_EV_HOLD */
}

int ui_touch_poll(u32 fb, ui_app_t *a)
{
    touch_point_t pt;
    int ev;
    int hit;
    int tab;
    int action = 0;

    ev = touch_poll_event(&pt);
    if (ev == TOUCH_EV_NONE) {
        return 0;
    }

    /* 触摸层级: 数字键盘在最上层(它是在设置窗之上弹出来的), 开着时触摸只给它 */
    if (ui_app_kbd_is_open(a) != 0) {
        return ui_app_kbd_touch(fb, a, ev, (int)pt.x, (int)pt.y);
    }

    /* 窗口打开时, 触摸只作用于窗口 */
      if (a->wave_open != 0u) {
          return ui_wave_touch(fb, a, ev, (int)pt.x, (int)pt.y);
      }
    if ((a->fra.pick_open != 0u) && (a->screen == UI_SCREEN_FRA)) {
        /* ★ 模态: 曲线选择窗在最上层, 触摸只给它(必须先于设置页/按钮栏判断) */
        return ui_fra_pick_touch(fb, a, ev, (int)pt.x, (int)pt.y);
    }
    if ((a->fra.confirm_open != 0u) && (a->screen == UI_SCREEN_FRA)) {
        /* ★ 模态: 弹窗在最上层, 触摸只给它(必须先于设置页判断) */
        return ui_fra_confirm_touch(fb, a, ev, (int)pt.x, (int)pt.y);
    }
    if ((a->fra.set_open != 0u) && (a->screen == UI_SCREEN_FRA)) {
        /* ★ 设置页只吃它自己那一块: 返回 0 = 这次触摸不归它管(例如底部按钮栏),
           必须继续往下交给按钮栏, 否则"设置"按钮点不到 -> 退不出设置页 */
        if (ui_fra_set_touch(fb, a, ev, (int)pt.x, (int)pt.y) != 0) {

            return 1;
        }
    }
    if (a->menu_open != 0u) {
        return ui_menu_touch(fb, a, ev, (int)pt.x, (int)pt.y);
    }
    if ((a->set_open != 0u) && (a->screen == UI_SCREEN_SPEC)) {
        return ui_set_touch(fb, a, ev, (int)pt.x, (int)pt.y);
    }

    /* ★ Chirp-Z 模式子窗口开着时: 触摸只给它(模态)。
       子窗口本体由帧边界画(ui_spec_czt_popup_refresh), 这里只改状态,
       另外只重画"按钮那一小块"(与齿轮按钮同一套, 不会闪)。 */
    if ((a->screen == UI_SCREEN_SPEC) && (a->spec.czt_popup != 0u)) {
        static int pop_down = -2;      /* row seen on DOWN; -2 = no DOWN yet */
        int pr = ui_spec_cztpop_hit((int)pt.x, (int)pt.y);

        if (ev == TOUCH_EV_DOWN) {
            pop_down = pr;             /* remember where the press started */
            return action;
        }
        if (ev != TOUCH_EV_UP) {
            return action;             /* MOVE / HOLD: ignore */
        }
        /* Only act on a matching DOWN+UP pair. The touch driver sends extra UP/MOVE
           events right after the numeric keypad closes; without this check those
           stray events land "outside" the popup and close it (keypad closed ->
           Chirp-Z popup vanished). */
        if (pr != pop_down) {
            pop_down = -2;
            return action;
        }
        pop_down = -2;
        {
            if (pr < 0) {
                a->spec.czt_popup = 0u;                 /* 点窗外: 收起 */
            a->spec.czt_pop_draw = 0u;   /* ★ 关窗时清掉"待画"标志: 否则它一直挂着, 下次开窗被立刻消费(伪重画) */
                ui_app_win_req();                       /* ★ 交给窗口层贴底板 */
            } else if ((pr == 1) && ((int)pt.x >= UI_CZT_POP_BIN_X)) {
                /* ★ 模式1 的频段宽度: 只认【两个箭头热区】(以前用"左半/右半"分界,
                   而两个箭头都在分界点左边 -> 点哪个都是 -1, 减到 1 后就没反应了)。 */
                if (((int)pt.x >= UI_CZT_POP_BIN_X) &&
                    ((int)pt.x < (UI_CZT_POP_BIN_X + UI_CZT_POP_ARW_W))) {
                    if (a->spec.czt_peak_bins > 1u) { a->spec.czt_peak_bins--; }
                } else if (((int)pt.x >= (UI_CZT_POP_BIN_X + UI_CZT_POP_ARW2_DX)) &&
                           ((int)pt.x < (UI_CZT_POP_BIN_X + UI_CZT_POP_ARW2_DX + UI_CZT_POP_ARW_W))) {
                    if (a->spec.czt_peak_bins < 8u) { a->spec.czt_peak_bins++; }
                }
                a->spec.czt_pop_draw = 1u;   /* 请求重画子窗口本体 */
                ui_app_win_req();            /* ★ 还要请求窗口层: 否则帧边界两支都不走, czt_pop_draw 没人消费 -> 子窗口卡住 */
            } else if ((pr >= 3) && (a->spec.czt_mode == 2u)) {
                ui_app_kbd_open_czt(a, pr - 3);         /* mode2 row -> numeric keypad */
                action = 1;
            } else if (pr <= 2) {   /* ★ 只接受 0/1/2: 子窗口下半部空白区会命中 3/4/5, 原来会把 cz t_mode 写成 3/4/5 -> 三个圆点全空心("没有选中项") */
                a->spec.czt_mode = (u8)pr;              /* 0=关 1=模式1 2=模式2 */
                a->spec.info_req = 1u;                  /* ★ 信息行含 "Z2峰"(模式2) / "Z精测"(模式1) 文本 -> 切模式必须请重画, 否则关掉 CZT 后一直显示过期文本(以前只有模式2 每帧置 info_req, 关闭/模式1 没人置) */
                a->spec.czt_pop_draw = 1u;              /* 请求重画(圆点跟着变) */
                ui_app_win_req();                       /* ★ 请求窗口层消费它 */
            }
            ui_spec_draw_czt_btn(fb, 0, a->spec.czt_mode);
            ui_spec_flush_czt_btn(fb);
            action = 1;
        }
        return action;
    }

    /* ★ Chirp-Z 按钮: 与"设置"齿轮同一层的小按钮(频谱仪右上角)。
       防闪做法: 按下/抬起的重画只发生在按钮自己的矩形里, 且只刷按钮那几行;
       不整屏重画、不碰频谱区。 */
    if ((a->screen == UI_SCREEN_SPEC) &&
        ((ui_spec_cztbtn_hit((int)pt.x, (int)pt.y) != 0) || (ui_czt_btn_pressed != 0))) {
        int ch = ui_spec_cztbtn_hit((int)pt.x, (int)pt.y);

        if (ev == TOUCH_EV_DOWN) {
            ui_czt_btn_pressed = ch;
            ui_spec_draw_czt_btn(fb, ch, a->spec.czt_mode);
            ui_spec_flush_czt_btn(fb);
        } else if (ev == TOUCH_EV_MOVE) {
            if (ch != ui_czt_btn_pressed) {          /* 滑出按钮: 取消按下态 */
                ui_czt_btn_pressed = ch;
                ui_spec_draw_czt_btn(fb, ch, a->spec.czt_mode);
                ui_spec_flush_czt_btn(fb);
            }
        } else if (ev == TOUCH_EV_UP) {
            int was = ui_czt_btn_pressed;

            ui_czt_btn_pressed = 0;
            if (was != 0) {                          /* 抬起: 先恢复常态 */
                ui_spec_draw_czt_btn(fb, 0, a->spec.czt_mode);
                ui_spec_flush_czt_btn(fb);
            }
            if (ch != 0) {
                /* ★ 点一下: 开关模式子窗口(本体由帧边界画, 这里只置标志 -> 不闪) */
                if (a->spec.czt_popup != 0u) {
                    a->spec.czt_popup = 0u;              /* 收起: 帧边界贴回底板 */
            /* ★ 这里【不该】置 chrome_req: 按钮已在抬起时就地重画, 弹窗(y120..359)不盖顶栏, 关窗路径也已补画刻度带 -> 置了就是白刷 343KB+173KB flush(关窗可见闪) */
                    ui_app_win_req();                    /* ★ 交给窗口层贴底板 */
                } else {
                    a->spec.czt_popup = 1u;
            /* ★ 同上: 开窗也不改顶栏/信息行内容 -> 不置 chrome_req */
                    a->spec.czt_pop_draw = 1u;           /* 请求窗口层画本体 */
                    ui_app_win_req();                    /* ★ 交给窗口层存底板 + 画 */
                }
                action = 1;
            }
        }
        return action;
    }

    /* 频谱仪顶栏右上角的"设置"齿轮按钮: 热区只在它自己那 40x32 里(不在频谱绘图区,
       也不和按钮栏/通道条/药丸重叠)。按下反色, 在按钮上抬起才弹设置子窗口。
       ★ 与"界面"按钮一样, 这里只置标志; 窗口由主循环在帧边界画(触摸处理里画会被
         液晶扫描追上 -> 闪)。这里画的只有按钮自己那一小块, 不会有这个问题。 */
    if ((a->screen == UI_SCREEN_SPEC) &&
        ((ui_spec_setbtn_hit((int)pt.x, (int)pt.y) != 0) || (ui_set_btn_pressed != 0))) {
        int gh = ui_spec_setbtn_hit((int)pt.x, (int)pt.y);

        if (ev == TOUCH_EV_DOWN) {
            ui_set_btn_pressed = gh;
            ui_spec_draw_set_btn(fb, gh);
            ui_spec_flush_set_btn(fb);
        } else if (ev == TOUCH_EV_MOVE) {
            if (gh != ui_set_btn_pressed) {     /* 滑出按钮: 取消按下态 */
                ui_set_btn_pressed = gh;
                ui_spec_draw_set_btn(fb, gh);
                ui_spec_flush_set_btn(fb);
            }
        } else if (ev == TOUCH_EV_UP) {
            int was = ui_set_btn_pressed;
            ui_set_btn_pressed = 0;
            if (was != 0) {                     /* 先恢复普通态(开窗前后按钮像素一致) */
                ui_spec_draw_set_btn(fb, 0);
                ui_spec_flush_set_btn(fb);
            }
            if (gh != 0) {
                ui_app_set_open(a);
                action = 1;
            }
        }
        return action;                          /* 齿轮上的触摸不再交给按钮栏 */
    }

    /* 测量区上方的通道名(CH1/CH2): 点一下开/关该通道 */
    tab = ui_app_tab_hit(a, (int)pt.x, (int)pt.y);
    if (tab >= 0) {
        if (ev == TOUCH_EV_UP) {
            ui_app_channel_toggle(fb, a, tab);
            action = 1;
        }
        return action;                  /* 通道条上的触摸不再交给按钮栏 */
    }
    /* 信号发生器参数区: 点数值框弹键盘 / 枚举行左右切档 / 点右侧滚动条上下箭头滚动。
       ★ 拖动已取消: 只认"按下与抬起在同一部位"的点击(重画只打标志, 帧边界做)。 */
    if (a->screen == UI_SCREEN_AWG) {
        int part, vis = -1;
        u8  id = 0u;

        part = ui_awg_hit(&a->awg, (int)pt.x, (int)pt.y, &vis, &id);

        if (part != UI_AWG_HIT_NONE) {
            if (ev == TOUCH_EV_DOWN) {
                ui_awg_tap_hit = part;
                ui_awg_tap_id  = id;
                return 0;
            }
            if (ev == TOUCH_EV_UP) {
                if (part == ui_awg_tap_hit) {
                    if (part == UI_AWG_HIT_BOX) {
                        ui_app_kbd_open_awg(a, ui_awg_tap_id);       /* 弹数字键盘 */
                        action = 1;
                    } else if (part == UI_AWG_HIT_LT) {
                        ui_awg_row_step(&a->awg, ui_awg_tap_id, -1);
                        a->awg.redraw_params = 1u;
                        action = 1;
                    } else if (part == UI_AWG_HIT_GT) {
                        ui_awg_row_step(&a->awg, ui_awg_tap_id, +1);
                        a->awg.redraw_params = 1u;
                        action = 1;
                    } else if (part == UI_AWG_HIT_SBUP) {
                        ui_awg_scroll(&a->awg, -1);                  /* 上箭头 */
                        action = 1;
                    } else if (part == UI_AWG_HIT_SBDN) {
                        ui_awg_scroll(&a->awg, +1);                  /* 下箭头 */
                        action = 1;
                    }
                }
                ui_awg_tap_hit = UI_AWG_HIT_NONE;
                return action;
            }
            return 0;               /* MOVE / HOLD: 不动 */
        }
    }

    /* ★ 数字滤波器页: 顶栏"返回" + 6 行参数(点左半 = 减, 右半 = 加)。
       只改状态 + 置脏标志, 一个像素都不在这里画(帧边界由 ui_filter_draw_live 消费)。 */
    if (a->screen == UI_SCREEN_FLT) {
        u8  id = 0u;
        int part = ui_filter_hit(&a->flt, (int)pt.x, (int)pt.y, &id);

        if (part != 0) {
            if (ev == TOUCH_EV_DOWN) {
                ui_flt_tap = part;
                return 0;
            }
            if (ev == TOUCH_EV_UP) {
                if (part == ui_flt_tap) {
                    if (part == 100) {                        /* 顶栏"返回" */
                        ui_app_switch(a, UI_SCREEN_SCOPE);
                    } else if ((part - 1) == 4) {             /* ★ 第 5 行(截止/频率): 弹数字键盘精确输入 */
                        a->flt.row = (u8)(part - 1);
                        if ((a->flt.type == UI_FLT_TYPE_BP) || (a->flt.type == UI_FLT_TYPE_BS)) {
                            /* 带通/带阻有 f1~f2 两个频率: 点左半编辑 f1, 右半编辑 f2 */
                            ui_app_kbd_open_flt(a, ((int)pt.x > (UI_FLT_ROW_X + UI_FLT_ROW_W / 2)) ? 1 : 0);
                        } else {
                            ui_app_kbd_open_flt(a, 0);            /* 低通/高通: 编辑 fc */
                        }
                        a->flt.redraw_params = 1u;
                    } else {                                  /* 其它参数行: 左减右加 */
                        a->flt.row = (u8)(part - 1);
                        ui_filter_row_step(&a->flt,
                                           ((int)pt.x > (UI_FLT_ROW_X + UI_FLT_ROW_W / 2)) ? +1 : -1);
                    }
                    action = 1;
                }
                ui_flt_tap = 0;
                return action;
            }
            return 0;                       /* MOVE / HOLD: 不动 */
        }
    }

    hit = ui_btn_hit((int)pt.x, (int)pt.y, ui_app_btn_count(a));

    if (ev == TOUCH_EV_DOWN) {
        ui_btn_highlight(fb, a, ui_btn_pressed, hit);
        ui_btn_pressed = hit;
        if (ev == TOUCH_EV_DOWN) {              /* ★ 记下真正按下的时刻(长按计时基准) */
            ui_rep_down = (((u64)GT_HI) << 32) | (u64)GT_LO;
            ui_rep_did  = 0;
            ui_rep_next = 0u;                  /* ★ 每次按下重排起跑线 */
        }
    } else if (ev == TOUCH_EV_MOVE) {
        ui_btn_highlight(fb, a, ui_btn_pressed, hit);   /* 滑出按钮就取消高亮 */
        ui_btn_pressed = hit;
    } else if (ev == TOUCH_EV_UP) {
        int exec = UI_BTN_NONE;
        int was = ui_btn_pressed;

        ui_btn_pressed = UI_BTN_NONE;
        if ((was != UI_BTN_NONE) && (hit == was)) {
            if (ui_rep_did != 0) {
                ui_rep_did = 0;                 /* ★ 长按已连续移动过: 抬起不再补一格 */
            } else {
                exec = was;                     /* 短按: 在按钮上抬起 -> 执行一次 */
            }
        }
        ui_btn_highlight(fb, a, was, UI_BTN_NONE);

        /* ★ 去抖: 触摸驱动对一次点击可能给出多次 DOWN/UP(抖动) ——
           例如"停止"被连续执行两次 -> 停了又立刻开始下一轮扫频。
           同一按钮 250ms 内只执行一次。 */
        if (exec != UI_BTN_NONE) {
            u64 now2 = (((u64)GT_HI) << 32) | (u64)GT_LO;
            static u64 last_t = 0u;
            static int last_b = UI_BTN_NONE;

            if ((exec == last_b) && (last_t != 0u) &&
                ((now2 - last_t) < ((u64)GT_PER_MS * 250u))) {
                exec = UI_BTN_NONE;          /* 250ms 内的重复 -> 丢掉 */
            } else {
                last_t = now2;
                last_b = exec;
            }
        }
        if (exec != UI_BTN_NONE) {
            action = 1;
            if (exec == ui_app_btn_menu(a)) {
                /* ★ F8: FRA 扫频期间不许开"界面"菜单 —— 主循环冻结绘制, 菜单画不出来却是活的,
                   点中某一行会直接切页, 而 fra.running 还留着 -> 各种后续动作都被门控掉。 */
                if (!((a->screen == UI_SCREEN_FRA) && (a->fra.running != 0u))) {
                    ui_app_menu_open(a);
                }
                /* 窗口只打标志, 由主循环在帧边界上画(触摸处理里画会被液晶扫描追上) */
            } else {
                ui_app_btn_action(a, exec);
                /* 只重画被动作影响的静态文字区域, 不整屏重画 -> 不闪 */
                ui_app_after_button(fb, a);
                /* 按钮文字带当前参数/通道号, 参数变了要重画按钮栏 */
                ui_touch_draw_bar(fb, a, UI_BTN_NONE);
            }
        }
    } else {
        /* TOUCH_EV_HOLD: 保持高亮, 无需重画 */
        /* ★ 长按连发(按真实时间计时, 不用事件计数 —— HOLD 事件来得非常快, 数事件等于立刻连发):
             短按(<1s): 抬起时走一格, 精细点按不受影响;
             长按 >=1s: 开始连发, 之后每 150ms 走一格。
             ★ 时间源: 直接读 Cortex-A9 全局定时器(0xF8F00200), 不依赖 xtime_l.h
               (这个 BSP 里没有那个头文件)。计数频率 = CPU/2, 7020 为 667MHz -> 333000/ms;
               若实际 CPU 频率不同, 只需按比例改下面的 GT_PER_MS。 */
        /* ★★ 长按连发(逻辑修正):
             时间基准 = "手指真正按下的时刻"(DOWN 事件记录 ui_rep_down), 不是第一个 HOLD;
             短按(<REP_START_MS): 抬起时走一格(原有路径, 不受影响);
             长按(>=REP_START_MS): 开始连发, 之后每 REP_STEP_MS 一格;
             长按期间已连发过 -> 抬起时不再补那一格(ui_rep_did 在 UP 分支里挡掉)。
             ★ 时间源: 直接读 Cortex-A9 全局定时器(0xF8F00200), 不用 xtime_l.h。 */
        if ((a->screen == UI_SCREEN_FRA) &&
            ((ui_btn_pressed == 5) || (ui_btn_pressed == 6)) && (ui_rep_down != 0u)) {
            u64 now = (((u64)GT_HI) << 32) | (u64)GT_LO;

            if (ui_rep_next == 0u) { ui_rep_next = REP_START_MS; }   /* 本次按住的第一个 HOLD: 定起跑线 */
            {
                u64 ms = (now - ui_rep_down) / (u64)GT_PER_MS;   /* 相对"按下时刻"的毫秒数 */

                if ((ms >= REP_START_MS) && (ms >= ui_rep_next)) {
                    ui_app_btn_action(a, ui_btn_pressed);        /* 连续移动光标 */
                    ui_rep_did = 1;                              /* ★ 标记已连发 */
                    ui_rep_next += REP_STEP_MS;
                    action = 1;
                }
            }
        }
    }

    return action;
}

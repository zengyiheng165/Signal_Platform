/******************************************************************************
 * ui_numkbd.h - 可复用"九宫格数字键盘"(弹出式, 输入一个定点数)
 *
 * 用途: 频谱仪"设置"子窗口第 1 项"参考电压"的数值输入;
 *       信号发生器界面以后要输入频率/幅度时直接复用(换 target + 单位文本即可)。
 *
 * 分工(重要):
 *   本模块只管"输入一串数字 -> 变成一个定点值", 不认识 V / mV / Hz 这些含义:
 *     - 单位档 unit 由调用方定义(频谱仪: 0=V, 1=mV), 键盘左下角只按登记好的文本显示;
 *     - 回车确认后的 k->value 是"输入数字 x 1000"的定点值(即固定 3 位小数),
 *       由调用方按 unit 换算成自己内部单位(频谱仪: V 档 = value, mV 档 = value/1000);
 *     - target 只是调用方自己的标识(0=参考电压, 1=AWG频率, ...), 用来登记回显行提示。
 *
 * 布局(屏幕下半, 从下往上弹):
 *   +--------------------------------------------------+  UI_KBD_X=160, UI_KBD_W=480
 *   | 回显行: 提示 | 当前输入串 | 单位                  |  y=UI_KBD_Y+6, 高 32
 *   |  [ 1 ] [ 2 ] [ 3 ]                               |
 *   |  [ 4 ] [ 5 ] [ 6 ]                               |  键 W=150 H=36, 间距 6
 *   |  [ 7 ] [ 8 ] [ 9 ]                               |
 *   |  [ . ] [ 0 ] [ <-]                               |
 *   |  [   ESC   ]   [   OK   ]                        |  两个宽键 W=228
 *   +--------------------------------------------------+  y=UI_KBD_Y..UI_KBD_Y+255
 *
 *   ★ 面板顶 UI_KBD_Y = 480-256 = 224, 必须 >= 设置窗第 1 项数值框下边界+1(y=221):
 *     弹键盘时用户还要能看见框里正在输入的参考电压值。计划书给的 UI_KBD_H=240 / 键高 44
 *     装不下"回显行 + 4 行键 + 底部两键"(需要约 294px), 所以这里加高到 256、键高收到 36,
 *     既装得下, 又正好不压住 y171..220 —— 参数化了坐标, 改面板高度只需改这两个宏。
 *
 * 输入规则(与计划书一致):
 *   - 只收 0-9 与一个小数点; 小数点不能在首位(自动补 "0.");
 *   - 小数位超过 dec_max 的后续数字忽略; 总长超过 len_max 忽略;
 *   - "退格" 删一个字符, 删空后回显 "0";
 *   - 回车: 解析成定点值写 k->value, k->ok=1, 收键盘(返回 0);
 *   - 取消 或 点面板外: value 不动, k->ok=0, 收键盘(返回 0)。
 *
 * 依赖: lcd_gfx.c(图元) + ui_screens.c(ui_draw_text/ui_text_size, 与界面共用字库)。
 * 字库: 只用 ASCII(数字 / '.' / "<-" / "ESC" / "OK"), 回显行提示文本由调用方登记,
 *       登记前请用 _hwsim\fontcover.ps1 核对有没有缺字。
 ******************************************************************************/
#ifndef UI_NUMKBD_H
#define UI_NUMKBD_H

#include "xil_types.h"
#include "lcd_gfx.h"
#include "touch.h"          /* TOUCH_EV_DOWN / MOVE / HOLD / UP */

/* ---------------- 面板几何 ---------------- */
#define UI_KBD_W        480
#define UI_KBD_H        256
#define UI_KBD_X        160
#define UI_KBD_Y        (LCD_GFX_HEIGHT - UI_KBD_H)          /* 224 */

/* 回显行(面板顶部): 提示 + 输入串 + 单位 */
#define UI_KBD_ECHO_X   (UI_KBD_X + 10)                      /* 170 */
#define UI_KBD_ECHO_Y   (UI_KBD_Y + 6)                       /* 230 */
#define UI_KBD_ECHO_W   (UI_KBD_W - 20)                      /* 460 */
#define UI_KBD_ECHO_H   32

/* 键区: 3 列 x 4 行, 居中(左右各留 9px) */
#define UI_KBD_KEY_W    150
#define UI_KBD_KEY_H    36
#define UI_KBD_KEY_GAP  6
#define UI_KBD_PAD_X    ((UI_KBD_W - (3 * UI_KBD_KEY_W + 2 * UI_KBD_KEY_GAP)) / 2)  /* 9 */
#define UI_KBD_GRID_X   (UI_KBD_X + UI_KBD_PAD_X)            /* 169 */
#define UI_KBD_GRID_Y   (UI_KBD_Y + 44)                      /* 268 */
#define UI_KBD_GRID_H   (4 * UI_KBD_KEY_H + 3 * UI_KBD_KEY_GAP)   /* 162 */

/* 底部两个宽键(左"ESC"取消 / 右"OK"回车), 各 228 宽, 左右各留 8px */
#define UI_KBD_BTN_W    ((UI_KBD_W - 3 * 8) / 2)             /* 228 */
#define UI_KBD_BTN_H    36
#define UI_KBD_BTN_X0   (UI_KBD_X + 8)                       /* 168 */
#define UI_KBD_BTN_X1   (UI_KBD_BTN_X0 + UI_KBD_BTN_W + 8)   /* 404 */
#define UI_KBD_BTN_Y    (UI_KBD_GRID_Y + UI_KBD_GRID_H + UI_KBD_KEY_GAP)   /* 436 */

/* ---------------- 键 id ----------------
   0..9 = 数字键(键面就是数字), 下面是功能键 */
#define UI_NUMKBD_KEY_NONE  0xFFu
#define UI_NUMKBD_KEY_DOT   10u      /* 小数点 */
#define UI_NUMKBD_KEY_BS    11u      /* 退格 "<-" */
#define UI_NUMKBD_KEY_ESC   12u      /* 取消(丢弃草稿) */
#define UI_NUMKBD_KEY_OK    13u      /* 回车(确认写值) */
#define UI_NUMKBD_KEY_CNT   14

/* ---------------- 状态 ---------------- */
typedef struct {
    u8   open;        /* 1 = 弹出中 */
    u8   target;      /* 调用方标识: 0=参考电压, 1=AWG频率, 2=AWG幅度 ... */
    u8   unit;        /* 显示单位档(含义由调用方定义) */
    u8   len;         /* buf 有效长度 */
    u8   dot;         /* 已输入小数点 */
    u8   dec_max;     /* 允许的最大小数位(参考电压=3, 因为 1mV 分辨率) */
    u8   len_max;     /* 输入串最大长度(参考电压=6: "5.500"/"5500") */
    u8   press;       /* 当前按下键的 id, 用于反色; 0xFF = 无 */
    u8   ok;          /* ★ 收键盘的原因: 1 = 回车确认(value 有效), 0 = 取消/点面板外 */
    char buf[12];     /* 输入串(不含结尾 '\0' 的长度 = len) */
    s32  value;       /* 回车确认后的定点值 = 输入数字 x 1000, 未确认时无效 */
} ui_numkbd_t;

/* ---------------- 接口 ---------------- */
/* 打开键盘(只是打状态, 真正画到屏上由窗口层在帧边界做; 见 ui_app_kbd_open_*) */
void ui_numkbd_open(ui_numkbd_t *k, u8 target, u8 unit, const char *init,
                    u8 dec_max, u8 len_max);

/* 画整个面板(内含 D-Cache 刷新, 板子上必须走这条路径) */
void ui_numkbd_draw(u32 fb, const ui_numkbd_t *k);

/* 处理一次触摸(键盘开着时, 触摸只给它)。
   返回 1 = 键盘仍开着(继续编辑); 0 = 已收键盘(确认/取消都返回 0, 由 k->ok 区分)。
   ★ 按键/回显的小块重画与 D-Cache 刷新都在这里面做完。 */
int  ui_numkbd_touch(u32 fb, ui_numkbd_t *k, int x, int y, int ev);

/* 回显行内容(空串显示 "0") */
void ui_numkbd_text(const ui_numkbd_t *k, char *out, int outsz);

/* 命中/几何(给窗口层与验证台用) */
int  ui_numkbd_win_hit(int x, int y);                 /* 1 = 落在面板矩形内 */
int  ui_numkbd_key_hit(int x, int y);                 /* 键 id, -1 = 没落在任何键上 */
int  ui_numkbd_key_rect(int id, int *x, int *y, int *w, int *h);  /* 1 = 有效 */

/* D-Cache 刷新: 按行整条刷(y 起, h 行)。行字节数 = 800*3 = 2400 = 32*75,
   所以起点/长度天然是 32 字节(cache line)整数倍 —— 与 ui_win_flush 同一套规则。 */
void ui_numkbd_flush(u32 fb, int y, int h);

/* ---- 底板(窗口层用): 键盘自己的缓存, 不能和"界面"菜单/设置窗的 ui_win_save 共用 ----
   键盘是在设置窗"之上"弹出来的: 共用一块底板会把设置窗的底板冲掉,
   之后关设置窗就贴不回原样了。 */
void ui_numkbd_save_under(u32 fb, const ui_numkbd_t *k);      /* 存面板底下的像素(不刷 cache) */
void ui_numkbd_restore_under(u32 fb, const ui_numkbd_t *k);   /* 原样贴回 + 刷 cache */

/* ---- 文本(模块不认识单位/目标的含义, 由调用方登记; 只存指针, 字符串要常驻) ----
   默认: 单位 0 -> "V", 1 -> "mV"(与频谱仪的单位档一致), 其余 -> "";
         目标提示默认 ""。 */
#define UI_NUMKBD_UNIT_MAX  16    /* 8..12 留给信号发生器(Hz/%/度/整数/x0.1) */
#define UI_NUMKBD_TGT_MAX   12   /* 8/9/10 = Chirp-Z(起始/终止/点数), 见 ui_app.h */
void ui_numkbd_set_unit_text(u8 unit, const char *txt);
void ui_numkbd_set_hint(u8 target, const char *txt);
const char *ui_numkbd_unit_text(u8 unit);
const char *ui_numkbd_hint(u8 target);

#endif /* UI_NUMKBD_H */

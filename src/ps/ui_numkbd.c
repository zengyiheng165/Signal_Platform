/******************************************************************************
 * ui_numkbd.c - 可复用九宫格数字键盘(实现)
 *
 * 设计要点:
 *   - 面板是不透明的: 弹出时把底下的像素存进自己的底板缓存, 收键盘时原样贴回
 *     (与"界面"菜单/设置窗同一套"存底板/贴底板"思路, 但用独立缓存 —— 见 .h 里的说明);
 *   - 所有写屏路径(整面板 / 单键反色 / 回显行)末尾都刷 D-Cache:
 *     面板高 256 行, 整块刷; 单键/回显只刷它占的那几行(整行刷, 天然 32B 对齐);
 *   - 不引入浮点/printf: 定点解析直接按字符累加。
 ******************************************************************************/
#include "ui_numkbd.h"
#include "ui_screens.h"      /* ui_draw_text / ui_text_size / UI_OSC_COLOR_* */
#include "xil_cache.h"
#include <string.h>

/* ---------------- 文本登记表 ---------------- */
static const char *kbd_unit_txt[UI_NUMKBD_UNIT_MAX] = {
    "V", "mV", "Vpp", "mVpp", "", "", "", ""
};
static const char *kbd_hint_txt[UI_NUMKBD_TGT_MAX] = {
    "", "", "", "", "", "", "", ""
};

void ui_numkbd_set_unit_text(u8 unit, const char *txt)
{
    if (unit < (u8)UI_NUMKBD_UNIT_MAX) {
        kbd_unit_txt[unit] = (txt != 0) ? txt : "";
    }
}

const char *ui_numkbd_unit_text(u8 unit)
{
    return (unit < (u8)UI_NUMKBD_UNIT_MAX) ? kbd_unit_txt[unit] : "";
}

void ui_numkbd_set_hint(u8 target, const char *txt)
{
    if (target < (u8)UI_NUMKBD_TGT_MAX) {
        kbd_hint_txt[target] = (txt != 0) ? txt : "";
    }
}

const char *ui_numkbd_hint(u8 target)
{
    return (target < (u8)UI_NUMKBD_TGT_MAX) ? kbd_hint_txt[target] : "";
}

/* ---------------- 底板缓存 ----------------
   容量直接跟着面板宏走(★ 不许写死: 写死了改几何就会越界踩内存)。 */
#define KBD_SAVE_W  (UI_KBD_W)
#define KBD_SAVE_H  (UI_KBD_H)
static u8  kbd_save[KBD_SAVE_W * 3 * KBD_SAVE_H];
static u8  kbd_saved;               /* 底板是否有效 */

void ui_numkbd_flush(u32 fb, int y, int h)
{
    if ((h <= 0) || (y < 0) || ((y + h) > LCD_GFX_HEIGHT)) {
        return;
    }
    /* 一行 = LCD_GFX_WIDTH*3 = 2400 = 32*75 字节: 起点与长度都是 cache line 整数倍 */
    Xil_DCacheFlushRange((UINTPTR)(fb + (u32)y * LCD_GFX_WIDTH * 3u),
                         (u32)h * LCD_GFX_WIDTH * 3u);
}

void ui_numkbd_save_under(u32 fb, const ui_numkbd_t *k)
{
    int r;

    (void)k;
    kbd_saved = 0u;
    if ((UI_KBD_W > KBD_SAVE_W) || (UI_KBD_H > KBD_SAVE_H)) {
        return;
    }
    if ((UI_KBD_X < 0) || (UI_KBD_Y < 0) ||
        ((UI_KBD_X + UI_KBD_W) > LCD_GFX_WIDTH) ||
        ((UI_KBD_Y + UI_KBD_H) > LCD_GFX_HEIGHT)) {
        return;
    }
    for (r = 0; r < UI_KBD_H; r++) {
        const u8 *src = (const u8 *)(UINTPTR)(fb +
                         (u32)(UI_KBD_Y + r) * LCD_GFX_WIDTH * 3u + (u32)UI_KBD_X * 3u);
        memcpy(&kbd_save[r * KBD_SAVE_W * 3], src, (size_t)UI_KBD_W * 3u);
    }
    kbd_saved = 1u;
}

void ui_numkbd_restore_under(u32 fb, const ui_numkbd_t *k)
{
    int r;

    (void)k;
    if (kbd_saved == 0u) {
        return;
    }
    for (r = 0; r < UI_KBD_H; r++) {
        u8 *dst = (u8 *)(UINTPTR)(fb +
                   (u32)(UI_KBD_Y + r) * LCD_GFX_WIDTH * 3u + (u32)UI_KBD_X * 3u);
        memcpy(dst, &kbd_save[r * KBD_SAVE_W * 3], (size_t)UI_KBD_W * 3u);
    }
    kbd_saved = 0u;
    ui_numkbd_flush(fb, UI_KBD_Y, UI_KBD_H);       /* 贴回也是写屏, 必须刷 */
}

/* ---------------- 几何 / 命中 ---------------- */
int ui_numkbd_win_hit(int x, int y)
{
    return ((x >= UI_KBD_X) && (x < (UI_KBD_X + UI_KBD_W)) &&
            (y >= UI_KBD_Y) && (y < (UI_KBD_Y + UI_KBD_H))) ? 1 : 0;
}

int ui_numkbd_key_rect(int id, int *x, int *y, int *w, int *h)
{
    int row;
    int col;

    if ((id < 0) || (id >= (int)UI_NUMKBD_KEY_CNT)) {
        return 0;
    }
    if (id <= 9) {                              /* 1..9 占前三行, 0 在第 4 行中间 */
        if (id == 0) {
            row = 3; col = 1;
        } else {
            row = (id - 1) / 3; col = (id - 1) % 3;
        }
        *x = UI_KBD_GRID_X + col * (UI_KBD_KEY_W + UI_KBD_KEY_GAP);
        *y = UI_KBD_GRID_Y + row * (UI_KBD_KEY_H + UI_KBD_KEY_GAP);
        *w = UI_KBD_KEY_W;
        *h = UI_KBD_KEY_H;
        return 1;
    }
    if (id == (int)UI_NUMKBD_KEY_DOT) {
        *x = UI_KBD_GRID_X;
        *y = UI_KBD_GRID_Y + 3 * (UI_KBD_KEY_H + UI_KBD_KEY_GAP);
        *w = UI_KBD_KEY_W; *h = UI_KBD_KEY_H;
        return 1;
    }
    if (id == (int)UI_NUMKBD_KEY_BS) {
        *x = UI_KBD_GRID_X + 2 * (UI_KBD_KEY_W + UI_KBD_KEY_GAP);
        *y = UI_KBD_GRID_Y + 3 * (UI_KBD_KEY_H + UI_KBD_KEY_GAP);
        *w = UI_KBD_KEY_W; *h = UI_KBD_KEY_H;
        return 1;
    }
    /* 底部两个宽键 */
    *x = (id == (int)UI_NUMKBD_KEY_ESC) ? UI_KBD_BTN_X0 : UI_KBD_BTN_X1;
    *y = UI_KBD_BTN_Y;
    *w = UI_KBD_BTN_W;
    *h = UI_KBD_BTN_H;
    return 1;
}

int ui_numkbd_key_hit(int x, int y)
{
    int id;
    int kx = 0, ky = 0, kw = 0, kh = 0;

    for (id = 0; id < (int)UI_NUMKBD_KEY_CNT; id++) {
        if (ui_numkbd_key_rect(id, &kx, &ky, &kw, &kh) == 0) {
            continue;
        }
        if ((x >= kx) && (x < (kx + kw)) && (y >= ky) && (y < (ky + kh))) {
            return id;
        }
    }
    return -1;
}

/* ---------------- 输入串 ---------------- */
void ui_numkbd_text(const ui_numkbd_t *k, char *out, int outsz)
{
    int n = (int)k->len;

    if (outsz <= 0) {
        return;
    }
    if ((n <= 0) || (n >= outsz)) {             /* 空串按规则显示 "0" */
        out[0] = '0';
        out[1] = '\0';
        return;
    }
    memcpy(out, k->buf, (size_t)n);
    out[n] = '\0';
}

/* 小数点后已经输入了几位 */
static int kbd_dec_count(const ui_numkbd_t *k)
{
    int i;
    int n = 0;

    for (i = 0; i < (int)k->len; i++) {
        if (k->buf[i] == '.') {
            n = 0;
        } else {
            n++;
        }
    }
    return n;
}

/* 收一个字符(数字或小数点); 越界/超位数直接忽略 */
static void kbd_push_char(ui_numkbd_t *k, char c)
{
    if (k->len >= k->len_max) {
        return;                                  /* 总长上限 */
    }
    if (c == '.') {
        if (k->dot != 0u) {
            return;                              /* 只允许一个小数点 */
        }
        if (k->len == 0u) {                      /* 首位就点小数点: 自动补 "0." */
            if (k->len_max < 2u) {
                return;
            }
            k->buf[k->len] = '0';
            k->len = (u8)(k->len + 1u);
        }
        k->buf[k->len] = '.';
        k->len = (u8)(k->len + 1u);
        k->dot = 1u;
    } else {
        if ((k->dot != 0u) && (kbd_dec_count(k) >= (int)k->dec_max)) {
            return;                              /* 小数位上限 */
        }
        k->buf[k->len] = c;
        k->len = (u8)(k->len + 1u);
    }
    k->buf[k->len] = '\0';
}

static void kbd_backspace(ui_numkbd_t *k)
{
    if (k->len == 0u) {
        return;
    }
    k->len = (u8)(k->len - 1u);
    if (k->buf[k->len] == '.') {
        k->dot = 0u;
    }
    k->buf[k->len] = '\0';
}

/* 输入串 -> 定点值(输入数字 x 1000, 即固定 3 位小数): "1.65" -> 1650, "5500" -> 5500000 */
static s32 kbd_parse(const ui_numkbd_t *k)
{
    s32 ip = 0;
    s32 fp = 0;
    s32 mul = 100;
    int i;
    int dot = 0;

    for (i = 0; i < (int)k->len; i++) {
        char c = k->buf[i];
        if (c == '.') {
            dot = 1;
            continue;
        }
        if (dot == 0) {
            ip = ip * 10 + (s32)(c - '0');
        } else {
            fp += (s32)(c - '0') * mul;
            mul /= 10;
        }
    }
    return ip * 1000 + fp;
}

/* ---------------- 绘制 ---------------- */
static const char *kbd_key_label(int id)
{
    static const char *const dig[10] = {
        "0", "1", "2", "3", "4", "5", "6", "7", "8", "9"
    };

    if ((id >= 0) && (id <= 9)) {
        return dig[id];
    }
    if (id == (int)UI_NUMKBD_KEY_DOT) {
        return ".";
    }
    if (id == (int)UI_NUMKBD_KEY_BS) {
        return "<-";                      /* 字库没有"退", 用 ASCII 箭头 */
    }
    if (id == (int)UI_NUMKBD_KEY_ESC) {
        return "ESC";                     /* 字库没有"取消", 用 ASCII */
    }
    if (id == (int)UI_NUMKBD_KEY_OK) {
        return "OK";                      /* 回车 */
    }
    return "";
}

/* 画一个键: 白底黑字; 按下时反色(黑底白字 + 白描边); 不刷 cache, 由调用方按行刷 */
static void kbd_draw_key(u32 fb, int id, int pressed)
{
    int x = 0, y = 0, w = 0, h = 0;
    int tw;
    const char *lab;
    u16 bg = (pressed != 0) ? UI_OSC_COLOR_BG   : UI_OSC_COLOR_TEXT;   /* 平时: 白底 */
    u16 fg = (pressed != 0) ? UI_OSC_COLOR_TEXT : UI_OSC_COLOR_BG;     /* 平时: 黑字 */
    u16 bd = (pressed != 0) ? UI_OSC_COLOR_TEXT : UI_OSC_COLOR_BG;     /* 白底要黑框才看得见 */

    if (ui_numkbd_key_rect(id, &x, &y, &w, &h) == 0) {
        return;
    }
    lcd_fill_rect(fb, x, y, w, h, bg);
    lcd_rect(fb, x, y, w, h, bd);
    lab = kbd_key_label(id);
    tw = (int)ui_text_size(lab, 1);
    (void)ui_draw_text(fb, x + (w - tw) / 2, y + (h - 16) / 2, lab, fg, 1);
}

/* 输入框(回显行): 白底黑字 —— 提示(左) + 当前输入串(右, 挨着单位) + 单位(最右) */
static void kbd_draw_echo(u32 fb, const ui_numkbd_t *k)
{
    int x = UI_KBD_ECHO_X;
    int y = UI_KBD_ECHO_Y;
    int w = UI_KBD_ECHO_W;
    int h = UI_KBD_ECHO_H;
    int ty = y + ((h - 16) / 2);
    int hw, vw, uw, ux, vx;
    const char *hint = ui_numkbd_hint(k->target);
    const char *un = ui_numkbd_unit_text(k->unit);
    char val[16];

    lcd_fill_rect(fb, x, y, w, h, UI_OSC_COLOR_TEXT);   /* 输入框底色: 白 */
    lcd_rect(fb, x, y, w, h, UI_OSC_COLOR_BG);          /* 白底上要用黑框 */

    hw = (int)ui_text_size(hint, 1);
    (void)ui_draw_text(fb, x + 8, ty, hint, UI_OSC_COLOR_BG, 1);     /* 提示: 黑字 */

    uw = (int)ui_text_size(un, 1);
    ux = x + w - 8 - uw;
    (void)ui_draw_text(fb, ux, ty, un, UI_OSC_COLOR_BG, 1);          /* 单位: 黑字 */

    ui_numkbd_text(k, val, (int)sizeof(val));
    vw = (int)ui_text_size(val, 1);
    vx = ux - 12 - vw;
    if (vx < (x + 16 + hw)) {                   /* 别压到左边提示 */
        vx = x + 16 + hw;
    }
    (void)ui_draw_text(fb, vx, ty, val, UI_OSC_COLOR_BG, 1);         /* 数值: 黑字 */
}

void ui_numkbd_draw(u32 fb, const ui_numkbd_t *k)
{
    int id;
    int pressed = (k->press != UI_NUMKBD_KEY_NONE) ? (int)k->press : -1;

    /* 面板底 = 灰白(按键/输入框是纯白, 靠黑色描边分开); 外框仍是白 1px */
    lcd_fill_rect(fb, UI_KBD_X, UI_KBD_Y, UI_KBD_W, UI_KBD_H, UI_OSC_COLOR_KBD_BG);
    lcd_rect(fb, UI_KBD_X, UI_KBD_Y, UI_KBD_W, UI_KBD_H, UI_OSC_COLOR_FRAME);

    /* 回显行底部一条分隔线: 浅底上不能用深灰网格色, 用中性灰 */
    lcd_hline(fb, UI_KBD_X + 1, UI_KBD_X + UI_KBD_W - 2,
              UI_KBD_ECHO_Y + UI_KBD_ECHO_H, UI_OSC_COLOR_MID);

    kbd_draw_echo(fb, k);
    for (id = 0; id < (int)UI_NUMKBD_KEY_CNT; id++) {
        kbd_draw_key(fb, id, (id == pressed) ? 1 : 0);
    }

    /* ★ 画完必须刷 D-Cache(面板整块): 板子上漏刷 = 花屏 */
    ui_numkbd_flush(fb, UI_KBD_Y, UI_KBD_H);
}

/* ---------------- 触摸 ---------------- */
void ui_numkbd_open(ui_numkbd_t *k, u8 target, u8 unit, const char *init,
                    u8 dec_max, u8 len_max)
{
    int n = 0;

    k->open    = 1u;
    k->target  = target;
    k->unit    = unit;
    k->dec_max = (dec_max > 3u) ? 3u : dec_max;      /* 定点值固定 3 位小数(kbd_parse 的 mul 只有 100/10/1) */
    if (len_max < 1u) {
        len_max = 1u;
    }
    if (len_max > (u8)(sizeof(k->buf) - 1u)) {
        len_max = (u8)(sizeof(k->buf) - 1u);
    }
    k->len_max = len_max;
    k->len     = 0u;
    k->dot     = 0u;
    k->press   = UI_NUMKBD_KEY_NONE;
    k->ok      = 0u;
    k->buf[0]  = '\0';
    /* 初值串: 只收数字与一个小数点, 其它字符丢掉(调用方给的是显示串) */
    while ((init != 0) && (init[n] != '\0') && (n < (int)len_max)) {
        char c = init[n];
        ++n;
        if (((c >= '0') && (c <= '9')) || (c == '.')) {
            kbd_push_char(k, c);
        }
    }
}

/* 按下某个键: 执行它的功能。返回 1 = 键盘仍开着, 0 = 已收键盘 */
static int kbd_apply_key(u32 fb, ui_numkbd_t *k, int id, int *dirty_y, int *dirty_h)
{
    (void)fb;                           /* 回显行由调用方重画(要连键的还原一起刷) */

    if ((id >= 0) && (id <= 9)) {
        kbd_push_char(k, (char)('0' + id));
        *dirty_y = UI_KBD_ECHO_Y;
        *dirty_h = UI_KBD_ECHO_H;
        return 1;
    }
    if (id == (int)UI_NUMKBD_KEY_DOT) {
        kbd_push_char(k, '.');
        *dirty_y = UI_KBD_ECHO_Y;
        *dirty_h = UI_KBD_ECHO_H;
        return 1;
    }
    if (id == (int)UI_NUMKBD_KEY_BS) {
        kbd_backspace(k);
        *dirty_y = UI_KBD_ECHO_Y;
        *dirty_h = UI_KBD_ECHO_H;
        return 1;
    }
    if (id == (int)UI_NUMKBD_KEY_ESC) {
        k->open = 0u;                   /* 取消: value 不动 */
        k->ok   = 0u;
        return 0;
    }
    if (id == (int)UI_NUMKBD_KEY_OK) {
        k->value = kbd_parse(k);        /* 回车: 写定点值 */
        k->open  = 0u;
        k->ok    = 1u;
        return 0;
    }
    return 1;
}

/* 画某个键的按下/普通态, 并只刷它占的那几行(整行刷, 32B 对齐) */
static void kbd_paint_key(u32 fb, int id, int pressed)
{
    int kx = 0, ky = 0, kw = 0, kh = 0;

    if (ui_numkbd_key_rect(id, &kx, &ky, &kw, &kh) == 0) {
        return;
    }
    kbd_draw_key(fb, id, pressed);
    ui_numkbd_flush(fb, ky, kh);
}

int ui_numkbd_touch(u32 fb, ui_numkbd_t *k, int x, int y, int ev)
{
    int id = ui_numkbd_key_hit(x, y);
    int want = (id >= 0) ? id : (int)UI_NUMKBD_KEY_NONE;

    if (ev == TOUCH_EV_DOWN) {
        k->press = (u8)want;                /* 面板外按下: press = NONE, 不高亮 */
        if (id >= 0) {
            kbd_paint_key(fb, id, 1);
        }
        return 1;
    }

    if (ev == TOUCH_EV_MOVE) {
        if ((int)k->press != want) {
            if (k->press != UI_NUMKBD_KEY_NONE) {
                kbd_paint_key(fb, (int)k->press, 0);
            }
            k->press = (u8)want;
            if (id >= 0) {
                kbd_paint_key(fb, id, 1);
            }
        }
        return 1;
    }

    if (ev == TOUCH_EV_UP) {
        int was = (k->press != UI_NUMKBD_KEY_NONE) ? (int)k->press : -1;

        k->press = UI_NUMKBD_KEY_NONE;
        if (was >= 0) {                          /* 先把按下态还原 */
            kbd_paint_key(fb, was, 0);
        }
        if ((id >= 0) && (id == was)) {           /* 在同一键上抬起 -> 执行 */
            int dy = 0, dh = 0;
            int keep = kbd_apply_key(fb, k, id, &dy, &dh);
            if (dh > 0) {                         /* 回显行变了: 重画 + 刷 */
                kbd_draw_echo(fb, k);
                ui_numkbd_flush(fb, dy, dh);
            }
            return keep;
        }
        if (ui_numkbd_win_hit(x, y) == 0) {       /* 点面板外: 取消(与"点窗口外关窗"一致) */
            k->open = 0u;
            k->ok   = 0u;
            return 0;
        }
        return 1;
    }

    /* TOUCH_EV_HOLD: 不动 */
    return (k->open != 0u) ? 1 : 0;
}

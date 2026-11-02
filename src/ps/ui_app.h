/******************************************************************************
 * ui_app.h - 界面管理: 当前界面 / 上拉窗口(界面切换) / 按钮分发
 *
 * 界面清单(以后加界面只需在这里加一行 + 写对应的 draw/btn 分支):
 *   0 示波器       (ui_osc_*)
 *   1 信号发生器   (ui_awg_*)
 *   2 频谱仪       (ui_spec_*)   单通道, CH1/CH2 可切换
 *
 * 上拉窗口: 按"界面"按钮从按钮栏上方拉起一个小窗, 列出所有界面;
 *           点某一项切换过去, 点空白处关掉。
 *
 * 频谱仪"设置"子窗口: 点频谱仪顶栏右上角的齿轮按钮, 在屏幕正中弹出一个子窗口,
 *           里面是相关可设置选项(名字 + 当前值); 选项的点击功能暂未实现(点行只高亮),
 *           点窗口外或右上角"关"按钮关掉。
 *           ★ 第 1 行是"参考电压": 行名 + 数值框 + 单位切换按钮(V/mV), 点单位按钮切换显示单位
 *             (内部恒为整数 mV, 默认 3300 = 3.300V); 点数值框弹出九宫格数字键盘
 *             (可复用模块 ui_numkbd.c), 回车写入参考电压, 取消/点面板外丢弃草稿。
 *             ★ 参考电压接进频谱仪"线性"纵轴的上端(见 ui_spec_lin_y), dB 模式不受影响。
 *           ★ 它和上面的"界面"菜单共用同一套"存底板/贴底板"机制(ui_app_win_*),
 *             不另写一套窗口保存/恢复。
 *
 * 整屏重画请求: 界面切换时置标志, 由主循环在"帧边界"执行整屏重画,
 *               这样不会出现写到一半被液晶扫到的撕裂。
 ******************************************************************************/
#ifndef UI_APP_H
#define UI_APP_H

#include "xil_types.h"
#include "ui_screens.h"
#include "ui_awg.h"
#include "ui_spec.h"
#include "ui_numkbd.h"
#include "ui_fra.h"
#include "ui_filter.h"
#include "touch.h"

/* 界面 ID */
#define UI_SCREEN_SCOPE 0
#define UI_SCREEN_AWG   1
#define UI_SCREEN_SPEC  2
#define UI_SCREEN_FRA   3     /* 第四界面: 频率特性分析仪 */
#define UI_SCREEN_FLT   4     /* 第五界面: 数字滤波器 */
#define UI_SCREEN_CNT   5

/* 底部按钮栏: 10 个按钮, 前 9 个含义由当前界面决定, 最后一个固定是"界面" */
#define UI_BTN_COUNT      10
#define UI_BTN_COUNT_AWG   7     /* 信号发生器: 波形/通道/输出/频率/幅度/相位/界面 */      /* 示波器 / 信号发生器的按钮数 */
#define UI_BTN_COUNT_FRA   8     /* 频率特性分析仪: 运行/阶数/平均/曲线/设置/<-/->/界面 */
#define UI_BTN_COUNT_SPEC 11      /* 频谱仪多一个"标峰" */
#define UI_BTN_COUNT_FLT   4      /* 数字滤波器: 应用/输出/采样率/界面(采样率键必须排在最后那个"界面"键之前) */
#define UI_BTN_MENU       (UI_BTN_COUNT - 1)   /* 打开/关闭"界面"窗口 */
#define UI_BTN_NONE     (-1)

/* 上拉窗口几何 */
#define UI_MENU_X      190
#define UI_MENU_W      420
#define UI_MENU_ROW_H  44
#define UI_MENU_PAD    6

/* "波形"选择子窗口几何(DAC1 最多 12 项 -> 2 列 x 6 行; 必须 <= ui_win_save 容量 480x300) */
#define UI_WAVE_X        171
#define UI_WAVE_W        458
#define UI_WAVE_TITLE_H  26
#define UI_WAVE_CELL_W   220
#define UI_WAVE_CELL_H   36
#define UI_WAVE_GAP      6
#define UI_WAVE_COLS     2
/* "波形"选择子窗口几何(DAC1 最多 12 项 -> 2 列 x 6 行; 必须 <= ui_win_save 容量 480x300) */
#define UI_WAVE_X        171
#define UI_WAVE_W        458
#define UI_WAVE_TITLE_H  26
#define UI_WAVE_CELL_W   220
#define UI_WAVE_CELL_H   36
#define UI_WAVE_GAP      6
#define UI_WAVE_COLS     2

/* 频谱仪"设置"子窗口几何: 屏幕正中
   x=(800-480)/2=160, y=(480-248)/2=116; 行高 28, 共 6 行(通道/纵轴/跨度/采样/分辨/峰值) */
#define UI_SET_WIN_X    160
#define UI_SET_WIN_Y    90                                     /* 只剩四项(4 x 两行 = 224px): 窗口回归小尺寸并居中 */
#define UI_SET_WIN_W    480
#define UI_SET_WIN_H    356                                    /* ★ 原 300 放不下第 5 行"窗": 5 行 x 56 = 280, +标题44+边距 = 356(底边 446, 仍在屏内) */
#define UI_SET_TITLE_H  44                                    /* 标题栏高度 */
#define UI_SET_CLOSE_X  (UI_SET_WIN_X + UI_SET_WIN_W - 40)     /* 关闭按钮: 600 */
#define UI_SET_CLOSE_Y  (UI_SET_WIN_Y + 9)                     /* 125 */
#define UI_SET_CLOSE_W  32
#define UI_SET_CLOSE_H  24
#define UI_SET_ROW_Y    (UI_SET_WIN_Y + UI_SET_TITLE_H + 8)    /* 第一行顶部 = 168 */
#define UI_SET_ROW_H    28
#define UI_SET_ROW0_H   (2 * UI_SET_ROW_H)                     /* 第 1 项(参考电压)占两行 = 56 */
#define UI_SET_ROW_N    5      /* ★ 参考电压 / 采样率 / 频率起始 / 频率终止 / 窗(每行占两格) */

/* ---- 设置窗第 1 行: "参考电压" 这一项 ----
   第 1 行的行矩形 = ui_set_row_rect(0) = x[170..629] y[171..192], 高 UI_SET_ROW_H-6 = 22。
   行内从左到右三块: 行名"参考电压"(16px x 4 字 = 64px, 画在 x+12=182 起) | 数值框 | 单位按钮。
   ★ 数值框/单位按钮的高度与行矩形一致(22), 上下边框和行边框重合 -> 一行看起来分成三格。
   ★ 这里给的是绝对坐标(与 ui_set_row_rect 同源), 改窗口几何时这三个宏要一起跟着调。 */
#define UI_SET_REF_ROW_H   (UI_SET_ROW0_H - 6)                             /* 50 = 两行高 - 6 */
#define UI_SET_REF_Y       (UI_SET_ROW_Y + 3)                              /* 171 */
#define UI_SET_REF_UNIT_W  72                                              /* 单位按钮宽(放大: 字号 x2) */
#define UI_SET_REF_UNIT_X  (UI_SET_WIN_X + UI_SET_WIN_W - 10 - 2 - UI_SET_REF_UNIT_W)  /* 584 */
#define UI_SET_REF_UNIT_H  UI_SET_REF_ROW_H
#define UI_SET_REF_BOX_W   150                                             /* 数值框宽(放大: 字号 x2 放得下 "5.500") */
#define UI_SET_REF_BOX_X   (UI_SET_REF_UNIT_X - 6 - UI_SET_REF_BOX_W)      /* 490 */
#define UI_SET_REF_BOX_H   UI_SET_REF_ROW_H

/* ---- 设置窗第 2 行: "采样率" 这一项 ----
   行矩形 = ui_set_row_rect(1) = x[170..629] y[227..276](占两行高, 与参考电压一样)。
   行内从左到右: 行名"采样率" | 数值框(与参考电压的数值框对齐) | 最右侧的上下箭头按钮
   (上箭头 = 升一档/频率变大, 下箭头 = 降一档; 到头夹住, 不循环)。 */
#define UI_SET_FS_ROW_H    UI_SET_REF_ROW_H                              /* 50 */
#define UI_SET_FS_Y        (UI_SET_ROW_Y + UI_SET_ROW0_H + 3)            /* 227 */
#define UI_SET_FS_BOX_X    UI_SET_REF_BOX_X                              /* 与参考电压数值框对齐 */
#define UI_SET_FS_BOX_W    UI_SET_REF_BOX_W
#define UI_SET_FS_ARW_W    34                                            /* 单个箭头按钮宽 */
#define UI_SET_FS_ARW_H    UI_SET_FS_ROW_H
#define UI_SET_FS_UP_X     UI_SET_REF_UNIT_X                             /* 上箭头(单位按钮左半) */
#define UI_SET_FS_DN_X     (UI_SET_REF_UNIT_X + 4 + UI_SET_FS_ARW_W)      /* 下箭头(右缘与单位按钮对齐) */

/* 设置窗命中值: >= 0 = 选项行号(0..UI_SET_ROW_N-1);
   下面两个负值表示"落在第 1 行的行内控件上", 和 UI_SET_HIT_NONE(-1, 什么都不沾)区分开 */
#define UI_SET_HIT_NONE    (-1)     /* 没命中任何行 / 控件 */
#define UI_SET_HIT_REFBOX  (-2)     /* 第 1 行"参考电压"的数值框 */
#define UI_SET_HIT_REFUNIT (-3)     /* 第 1 行"参考电压"的单位切换按钮 */
#define UI_SET_HIT_FSUP    (-4)     /* 第 2 行"采样率"的上箭头(升一档) */
#define UI_SET_HIT_FSDN    (-5)     /* 第 2 行"采样率"的下箭头(降一档) */
#define UI_SET_HIT_FSTBOX  (-6)     /* 第 3 行"频率起始"的数值框 */
#define UI_SET_HIT_FSTUNIT (-7)     /* 第 3 行"频率起始"的单位按钮(kHz/MHz) */
#define UI_SET_HIT_FSPBOX  (-8)     /* 第 4 行"频率终止"的数值框 */
#define UI_SET_HIT_FSPUNIT (-9)     /* 第 4 行"频率终止"的单位按钮(kHz/MHz) */

/* 应用总状态 */
typedef struct {
    u8       screen;      /* 当前界面 UI_SCREEN_xxx */
    u8       menu_open;   /* 1 = "界面"上拉窗口打开 */
    u8       set_open;    /* 1 = 频谱仪"设置"子窗口打开 */
    u8       wave_open;   /* 1 = "波形"选择子窗口打开(信号发生器) */
    u8       set_row_dirty; /* 键盘关掉后要重画设置窗的哪一行(1 = 第 0 行, 0 = 不用);
                               频率起始/终止的行被键盘盖住, 底板贴回后必须补画一次 */
    ui_osc_t osc;         /* 示波器状态 */
    ui_awg_t awg;         /* 信号发生器状态 */
    ui_spec_t spec;       /* 频谱仪状态 */
    ui_fra_t fra;        /* 频率特性分析仪状态 */
    ui_filter_t flt;     /* 数字滤波器状态 */
    ui_numkbd_t kbd;      /* ★ 数字键盘(第二步): 可复用模块 ui_numkbd.c 的状态 */
    u8 awg_kbd_id;        /* 键盘正在编辑的 AWG 参数 id(UI_AWG_P_xxx) */
} ui_app_t;

int  ui_fra_set_touch(u32 fb, ui_app_t *a, int ev, int x, int y);       /* FRA 设置页触摸 */
int  ui_fra_confirm_touch(u32 fb, ui_app_t *a, int ev, int x, int y);   /* FRA 用时预警确认弹窗 */
int  ui_fra_pick_touch(u32 fb, ui_app_t *a, int ev, int x, int y);   /* 曲线选择弹窗(模态) */
void ui_app_redraw_req(void);                                            /* 请求帧边界整屏重画 */

void ui_app_default(ui_app_t *a);

/* 整屏绘制当前界面(需要时叠加"界面"窗口); 不含底部按钮栏 */
void ui_app_draw(u32 fb, ui_app_t *a);
/* 当前界面的原地刷新(不含按钮栏); 窗口打开时不要调用 */
void ui_app_draw_live(u32 fb, ui_app_t *a);
void ui_app_flush_live(u32 fb, const ui_app_t *a);
/* 按钮动作后的局部刷新(顶栏/信息行/参数区; AWG 还会重画预览波形) */
void ui_app_after_button(u32 fb, ui_app_t *a);


/* 界面清单 */
int  ui_app_screen_count(void);
const char *ui_app_screen_name(int i);

/* 底部按钮: 标签(带当前参数) / 动作(按当前界面分发) */
/* 当前界面的按钮个数 / 最后一个"界面"按钮的下标(频谱仪比别的界面多一个) */
int  ui_app_btn_count(const ui_app_t *a);
int  ui_app_btn_menu(const ui_app_t *a);
void ui_app_btn_label(const ui_app_t *a, int btn, char *buf);
void ui_app_btn_action(ui_app_t *a, int btn);

/* ---- 示波器通道条上的通道名(CH1/CH2): 点一下开/关该通道 ---- */
int  ui_app_tab_hit(const ui_app_t *a, int x, int y);      /* 命中通道, -1=没命中 */
void ui_app_channel_toggle(u32 fb, ui_app_t *a, int ch);   /* 翻转并局部重画 */

/* ---- "界面"上拉窗口的几何/绘制/命中 ---- */
int  ui_app_menu_y(void);                 /* 窗口顶部 y */
int  ui_app_menu_h(void);                 /* 窗口高度 */
int  ui_app_menu_hit(int x, int y);       /* 命中界面序号, -1 = 没命中 */
void ui_app_menu_draw(u32 fb, const ui_app_t *a, int pressed);
void ui_app_menu_draw_row(u32 fb, const ui_app_t *a, int i, int pressed);
void ui_app_menu_flush(u32 fb);

/* ---- "波形"选择子窗口(信号发生器): 只列当前通道能产生的波形 ---- */
int  ui_wave_count(const ui_app_t *a);
int  ui_wave_y(const ui_app_t *a);
int  ui_wave_h(const ui_app_t *a);
int  ui_wave_hit(const ui_app_t *a, int x, int y);      /* 命中项序号, -1 = 没命中 */
void ui_wave_draw(u32 fb, const ui_app_t *a, int pressed);
void ui_wave_draw_row(u32 fb, const ui_app_t *a, int i, int pressed);
void ui_wave_flush(u32 fb, const ui_app_t *a);
void ui_wave_open(ui_app_t *a);
void ui_wave_close(u32 fb, ui_app_t *a);

/* ---- 弹窗层("界面"菜单 + 频谱仪"设置"子窗口) ----
   ★ 两个窗口共用同一套"存底板/贴底板"机制(实现在 ui_app.c 的 ui_win_*),
     不要为设置窗另写一套保存/恢复: 窗口打开期间主循环不刷新底下内容, 所以存下来的
     底板在关窗时能逐像素贴回(验证台断言"关窗后与开窗前差异 0 像素")。
   ★ refresh 只允许主循环在帧边界(wait_vdma_frame() 之后)调用, 触摸处理里只能置标志。 */
int  ui_app_win_open(const ui_app_t *a);          /* 1 = 有弹窗开着(主循环据此暂停原地刷新) */
int  ui_app_win_need_redraw(void);                /* 1 = 有开/关窗口的重画请求 */
void ui_app_win_clear_redraw(void);
void ui_app_win_req(void);   /* ★ 请求窗口层在帧边界重画(Chirp-Z 子窗口开/关用) */
void ui_app_win_refresh(u32 fb, ui_app_t *a);     /* 帧边界: 开窗(存底板+画) / 关窗(贴回底板) */

void ui_app_menu_open(ui_app_t *a);               /* 打标志, 窗口由主循环在帧边界画 */
void ui_app_menu_close(u32 fb, ui_app_t *a);      /* 打标志, 底板由主循环在帧边界贴回 */
/* 兼容旧名字(验证台 host_main.c 与老调用点仍在使用), 语义与 ui_app_win_* 相同 */
void ui_app_menu_refresh(u32 fb, ui_app_t *a);
int  ui_app_menu_need_redraw(void);
void ui_app_menu_clear_redraw(void);

/* ---- 频谱仪"设置"子窗口: 开/关 + 绘制 + 命中 ---- */
void ui_app_set_open(ui_app_t *a);                /* 只在频谱仪界面且无其它窗口时生效 */
void ui_app_set_close(u32 fb, ui_app_t *a);
void ui_app_set_draw(u32 fb, const ui_spec_t *s);
void ui_app_set_draw_row(u32 fb, const ui_spec_t *s, int i, int pressed);
/* 第 1 行"参考电压": 行名 + 数值框 + 单位按钮。
   row_pressed  = 行名区(整行)按下 -> 整行反色(与其它行一致);
   box_pressed  = 数值框单独按下   -> 只反色数值框(本轮点了不弹数字键盘, 只高亮);
   unit_pressed = 单位按钮单独按下 -> 只反色单位按钮。
   数值框里画的是 ui_spec_fmt_ref_mv(s->ref_mv, s->ref_unit), 所以切单位后重画这一行即可。 */
void ui_app_set_draw_ref_row(u32 fb, const ui_spec_t *s, int row_pressed,
                             int box_pressed, int unit_pressed);
/* 第 2 行"采样率": 行名 + 数值框 + 最右侧上下箭头。
   row_pressed = 整行按下 -> 整行(含框/箭头)反色;
   up_pressed / dn_pressed = 单独按下某个箭头 -> 只反色那个箭头。
   数值框显示当前档(ui_spec_fs_text), 箭头按下只置 ui_spec_t.fs_req, 由宿主去写时钟。 */
void ui_app_set_draw_fs_row(u32 fb, const ui_spec_t *s, int row_pressed,
                            int up_pressed, int dn_pressed);
/* 第 3/4 行"频率起始 / 频率终止": 行名 + 数值框 + 单位按钮(kHz/MHz),
   与第 1 行"参考电压"同一套排法(i = 2 起始, i = 3 终止) */
void ui_app_set_draw_freq_row(u32 fb, const ui_spec_t *s, int i, int row_pressed,
                              int box_pressed, int unit_pressed);
const char *ui_app_set_row_name(int i);           /* 选项行名(验证台/调试用) */
void ui_app_set_flush(u32 fb);                    /* 设置窗区域 D-Cache */
int  ui_app_set_win_hit(int x, int y);            /* 1 = 落在窗口矩形内 */
int  ui_app_set_close_hit(int x, int y);          /* 1 = 落在关闭按钮上 */
int  ui_app_set_ref_box_hit(int x, int y);        /* 1 = 落在第 1 行数值框上 */
int  ui_app_set_ref_unit_hit(int x, int y);       /* 1 = 落在第 1 行单位按钮上 */
int  ui_app_set_fs_up_hit(int x, int y);          /* 1 = 落在第 2 行上箭头上 */
int  ui_app_set_fs_dn_hit(int x, int y);          /* 1 = 落在第 2 行下箭头上 */
int  ui_app_set_freq_box_hit(int i, int x, int y);  /* i=2/3: 1 = 落在该行数值框上 */
int  ui_app_set_freq_unit_hit(int i, int x, int y); /* i=2/3: 1 = 落在该行单位按钮上 */
int  ui_app_set_hit(int x, int y);                /* 行号(>=0) / UI_SET_HIT_* / -1 = 不是行 */

/* ---- 数字键盘(可复用模块 ui_numkbd.c)的窗口层(第二步) ----
   键盘从屏幕下半弹出来, 盖在设置窗"之上": 设置窗第 1 项的数值框(y171..220)仍然看得见,
   所以用户输入时能看到框里原来的值, 回车后第 1 行立刻显示新值。
   ★ 键盘用自己的一块底板缓存(ui_numkbd_save_under/restore_under), 不能和设置窗共用
     ui_win_save: 共用会把设置窗的底板冲掉, 之后关设置窗就贴不回原样了;
   ★ 键盘开着时也算"有窗口"(ui_app_win_open 返回 1), 主循环据此暂停原地刷新;
   ★ 打开/关闭都只打标志, 真正画/贴由主循环在帧边界调 ui_app_win_refresh()。 */
#define UI_APP_KBD_TGT_REF    0u    /* 键盘目标: 参考电压(频谱仪"设置"窗第 1 项) */
#define UI_APP_KBD_TGT_FSTART 1u    /* 键盘目标: 频率起始(第 3 项) */
#define UI_APP_KBD_TGT_FSTOP  2u    /* 键盘目标: 频率终止(第 4 项) */
#define UI_APP_KBD_TGT_AWG    3u    /* 键盘目标: 信号发生器参数区(具体哪一行看 awg_kbd_id) */
#define UI_APP_KBD_TGT_FRA_START 6u /* ★ FRA 设置页: 起始频率(必须与频谱仪的 1/2 分开, 否则会写错对象) */
#define UI_APP_KBD_TGT_FRA_STOP  7u
#define UI_APP_KBD_TGT_CZT_FSTART 8u  /* Chirp-Z 模式2: 起始频率 */
#define UI_APP_KBD_TGT_CZT_FSTOP  9u  /* Chirp-Z 模式2: 终止频率 */
#define UI_APP_KBD_TGT_CZT_M     10u  /* Chirp-Z 模式2: 点数 M */
#define UI_APP_KBD_TGT_FLT_FC   11u  /* ★ 滤波器: 截止 fc(低通/高通) 或 f1(带通/带阻) */ /* ★ FRA 设置页: 终止频率 */
/* 频率这两项的单位只占键盘单位表的两个空槽(4/5): 0..3 已经给参考电压的 V/mV/Vpp/mVpp */
#define UI_APP_KBD_UNIT_KHZ   4u
#define UI_APP_KBD_UNIT_MHZ   5u

void ui_app_kbd_open_refbox(ui_app_t *a);
/* 设置窗第 row 行的数值框弹出数字键盘(row: 0 参考电压 / 2 频率起始 / 3 频率终止);
   只有设置窗开着时才生效。键盘只在帧边界画/收(见 ui_app_win_refresh)。 */
void ui_app_kbd_open_setrow(ui_app_t *a, int row);     /* 点数值框抬起: 弹键盘(初值 = 当前显示串) */
void ui_app_kbd_open_awg(ui_app_t *a, u8 id);        /* 点 AWG 参数区数值框: 弹键盘 */
void ui_app_kbd_open_fra(ui_app_t *a, int row, const char *init);
void ui_app_kbd_open_czt(ui_app_t *a, int row);   /* Chirp-Z mode2: 0=f_start 1=f_end 2=M */
#define UI_APP_KBD_TGT_FLT_F2   12u  /* ★ 滤波器: 带通/带阻的上边界 f2 */
void ui_app_kbd_open_flt(ui_app_t *a, int which);/* ★ 滤波器: 0=fc或f1, 1=f2 */   /* FRA 设置页: 起始/终止频率 */
void ui_fra_redraw_settings(u32 fb, ui_fra_t *a);          /* 键盘收起后重画 FRA 设置页 */
int  ui_app_kbd_touch(u32 fb, ui_app_t *a, int ev, int x, int y);
     /* 键盘开着时的触摸: 返回 1 = 本次有动作(收键盘/改回显都算) */
void ui_app_kbd_commit(u32 fb, ui_app_t *a);  /* 回车确认: kbd.value -> 目标值 + 重画受影响区域 */
void ui_app_kbd_close(ui_app_t *a);           /* 收键盘(只打标志, 底板由帧边界贴回) */
int  ui_app_kbd_is_open(const ui_app_t *a);   /* 1 = 键盘开着 */

/* 切换界面(会请求整屏重画) */
void ui_app_switch(ui_app_t *a, int screen);

/* "需要整屏重画"请求(主循环在帧边界执行) */
int  ui_app_need_redraw(void);
void ui_app_clear_redraw(void);

#endif /* UI_APP_H */

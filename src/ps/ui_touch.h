/******************************************************************************
 * ui_touch.h - 屏幕触摸按钮栏(10 个按钮) + "界面"上拉窗口
 *
 *   按钮栏: 屏幕最底部 40px(y=440..479), 横向 10 等分(每格 80px)
 *   最后一个是固定的"界面"按钮, 按下从按钮栏上方拉起一个小窗列出所有界面;
 *   前 9 个按钮的含义由当前界面决定(ui_app.c 里的 ui_app_btn_label/action):
 *     示波器:     自动设置 触发+ 触发- 运行停止 时基± 档位± CH1/CH2
 *     信号发生器: 通道 波形 频率- 频率+ 频率/10 频率x10 幅度- 幅度+ 输出
 *
 * 交互: 按下 -> 按钮反色; 在同一按钮上抬起 -> 执行动作; 滑出 -> 取消
 *       动作后只刷新受影响的区域(顶栏/信息行/参数区/按钮文字), 不整屏重画
 *
 * 依赖: touch.c(触摸驱动) + ui_app.c(界面管理)
 *
 * 主循环用法:
 *     ui_app_default(&app);  ... 采数据 ...
 *     ui_app_draw(FB, &app);                      // 整屏
 *     ui_touch_draw_bar(FB, &app, UI_BTN_NONE);   // 按钮栏
 *     while (1) {
 *         ... 采集一帧, 等帧边界 ...
 *         if (ui_app_need_redraw()) { ui_app_draw(FB,&app); ui_touch_draw_bar(...); lcd_flush(FB); ui_app_clear_redraw(); }
 *         else if (!app.menu_open) { ui_app_draw_live(FB,&app); ui_app_flush_live(FB,&app); }
 *         (void)ui_touch_poll(FB, &app);
 *     }
 ******************************************************************************/
#ifndef UI_TOUCH_H
#define UI_TOUCH_H

#include "xil_types.h"
#include "lcd_gfx.h"
#include "ui_app.h"

/* 每个按钮的横坐标与宽度(n 等分, 处理好取整; n 由 ui_app_btn_count() 给) */
#define UI_BTN_X(i, n)     ((i) * LCD_GFX_WIDTH / (n))
#define UI_BTN_WID(i, n)   (((((i) + 1) * LCD_GFX_WIDTH) / (n)) - \
                            (((i) * LCD_GFX_WIDTH) / (n)))

/* 画整个按钮栏; pressed = 当前被按下的按钮 ID(UI_BTN_NONE = 无) */
void ui_touch_draw_bar(u32 fb, const ui_app_t *a, int pressed);

/* 按钮栏区域的 D-Cache 刷新(缓存可写时必须调用) */
void ui_touch_flush_bar(u32 fb);

/* 处理一次触摸(按钮栏 + "界面"窗口). 返回 1 = 本次有动作执行 */
int  ui_touch_poll(u32 fb, ui_app_t *a);

#endif /* UI_TOUCH_H */

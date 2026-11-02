/******************************************************************************
 * touch.h - 电容触摸屏驱动(PS I2C0 经 EMIO, 引脚 SCL=V6 / SDA=W6)
 *
 * 自动探测控制器:
 *   - GT911 / GT9147 (Goodix): I2C 地址 0x5D 或 0x14, 16 位寄存器
 *   - FT5x06 / FT5426 / FT6236 (FocalTech): I2C 地址 0x38, 8 位寄存器
 * 只使用 I2C 轮询(INT/RST 没有接到 FPGA, 也不需要)。
 *
 * 用法(先验证硬件, 再接界面):
 *     touch_init();                 // 初始化 I2C + 探测控制器, 串口会打印探测结果
 *     touch_test_loop();            // 触摸屏幕 -> 串口打印坐标(验证 + 校准用)
 *     touch_test_raw_loop();        // 只打印控制器原始坐标(标定分辨率用)
 *
 * 验证通过后, 在刷新循环里:
 *     touch_point_t p;
 *     if (touch_read(&p)) { ... }             // p.x / p.y 已是 LCD 像素坐标(0..799 / 0..479)
 *  或带事件(推荐做按钮用):
 *     int ev = touch_poll_event(&p);          // TOUCH_EV_DOWN / MOVE / HOLD / UP / NONE
 ******************************************************************************/
#ifndef TOUCH_H
#define TOUCH_H

#include "xil_types.h"

/* LCD 分辨率(触摸坐标会映射到这个范围) */
#ifndef TOUCH_LCD_W
#define TOUCH_LCD_W   800
#endif
#ifndef TOUCH_LCD_H
#define TOUCH_LCD_H   480
#endif

/* 探测到的控制器类型 */
#define TOUCH_IC_NONE    0
#define TOUCH_IC_GT911   1     /* 汇顶 GT911/GT9147 */
#define TOUCH_IC_FT5X06  2     /* 敦泰 FT5x06/FT5426/FT6236 */

/* 触摸事件(touch_poll_event 返回值) */
#define TOUCH_EV_NONE  0
#define TOUCH_EV_DOWN  1       /* 本次刷新新按下 */
#define TOUCH_EV_MOVE  2       /* 按下并移动 */
#define TOUCH_EV_HOLD  3       /* 按下不动 */
#define TOUCH_EV_UP    4       /* 本次刷新抬起(坐标为最后按下位置) */

/* 坐标标定: 控制器原始坐标 -> LCD 像素坐标 */
typedef struct {
    u16 raw_w;      /* 控制器 X 轴满量程(默认 800; 用 touch_test_raw_loop 测出来) */
    u16 raw_h;      /* 控制器 Y 轴满量程(默认 480) */
    u8  swap_xy;    /* 1 = 交换 X/Y(横竖屏方向不对时置 1) */
    u8  inv_x;      /* 1 = X 轴反向 */
    u8  inv_y;      /* 1 = Y 轴反向 */
} touch_calib_t;

typedef struct {
    u16 x;          /* LCD 坐标 x: 0..TOUCH_LCD_W-1 */
    u16 y;          /* LCD 坐标 y: 0..TOUCH_LCD_H-1 */
    u8  id;         /* 触摸点编号(目前取第一个点) */
    u8  pressed;    /* 1 = 有触摸 */
} touch_point_t;

/* ---------------- 接口 ---------------- */

/* 初始化 I2C(100kHz) + 探测控制器; 返回 0 成功, -1 失败 */
int  touch_init(void);

/* 探测控制器(会打印型号与地址); 返回 TOUCH_IC_xxx */
int  touch_detect(void);

/* 扫描常见 I2C 地址, 打印有应答的地址(调试用) */
void touch_scan_bus(void);

/* 已探测到的控制器类型 */
int  touch_get_ic(void);

/* 读原始坐标: 返回 1 = 有触摸 */
int  touch_read_raw(u16 *x, u16 *y, u8 *id);

/* 读映射后的 LCD 坐标: 返回 1 = 有触摸 */
int  touch_read(touch_point_t *pt);

/* 带事件的读取: 返回 TOUCH_EV_xxx(内部记录上次状态, 做按钮最方便) */
int  touch_poll_event(touch_point_t *pt);

/* 标定参数读写 */
void touch_get_calib(touch_calib_t *c);
void touch_set_calib(const touch_calib_t *c);

/* 自测循环(死循环, 串口打印) */
void touch_test_loop(void);
void touch_test_raw_loop(void);

#endif /* TOUCH_H */

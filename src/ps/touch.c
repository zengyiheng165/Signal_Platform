/******************************************************************************
 * touch.c - 电容触摸屏驱动(PS I2C0 经 EMIO, SCL=V6 / SDA=W6)
 *
 * 说明:
 *   - 只用 I2C 轮询, 不需要 INT/RST 引脚(工程里没接)。
 *   - 自动探测 GT911/GT9147(0x5D/0x14) 与 FT5x06/FT5426(0x38)。
 *   - 本 BSP 是 SDT 流程(-DSDT), 所以用基地址查配置。
 ******************************************************************************/
#include "touch.h"
#include "xiicps.h"
#include "xparameters.h"
#include "xil_printf.h"
#include "sleep.h"
#include <string.h>

#define TOUCH_I2C_SCLK_HZ   100000u   /* 先用 100kHz, 稳定后可试 400kHz */

#define GT911_ADDR_A        0x5Du     /* GT911 地址(取决于复位时 INT 电平) */
#define GT911_ADDR_B        0x14u
#define FT_ADDR             0x38u     /* FT5x06/FT5426 */

/* GT911 寄存器(16 位地址) */
#define GT_REG_PRODUCT_ID   0x8140u   /* 4 字节 ASCII 产品 ID, 如 "911" */
#define GT_REG_STATUS       0x814Eu   /* bit7=数据就绪, bit3:0=触点数 */
#define GT_REG_POINT1       0x8150u   /* 每个点 8 字节: xl,xh,yl,yh,sl,sh,id,res */

/* FT5x06 寄存器(8 位地址) */
#define FT_REG_DEV_MODE     0x00u     /* 0x00=正常工作 */
#define FT_REG_TD_STATUS    0x02u     /* bit3:0=触点数 */
#define FT_REG_POINT1       0x03u     /* 每个点 4 字节: xh,xl,yh,yl */
#define FT_REG_CHIP_ID      0xA3u
#define FT_REG_FW_ID        0xA6u

static XIicPs        touch_iic;
static u8            touch_ready;                  /* 1 = 初始化完成可用 */
static u8            touch_ic = TOUCH_IC_NONE;
static u8            touch_addr;                   /* 实际 I2C 地址 */
static touch_calib_t touch_cal = { TOUCH_LCD_W, TOUCH_LCD_H, 0, 0, 0 };
static u8            touch_down;                   /* 上次是否按下(事件用) */
static touch_point_t touch_last;

/* ================= I2C 基础读写 ================= */
static int i2c_write_bytes(u8 addr, const u8 *buf, u32 len)
{
    s32 st;

    st = XIicPs_MasterSendPolled(&touch_iic, (u8 *)buf, (s32)len, (u16)addr);
    {
        /* ★ 有界等待(同 i2c_read_bytes): 详见那里的说明 */
        volatile int tw = 20000;

        while ((XIicPs_BusIsBusy(&touch_iic) != 0) && (tw > 0)) { tw--; }
    }
    return (st == XST_SUCCESS) ? 0 : -1;
}

static int i2c_read_bytes(u8 addr, u8 *buf, u32 len)
{
    s32 st;

    st = XIicPs_MasterRecvPolled(&touch_iic, buf, (s32)len, (u16)addr);
    {
        /* ★ 有界等待: 原来是 while (XIicPs_BusIsBusy(...)) { ; } 无界忙等 ——
           一旦触摸 I2C 控制器异常(面板掉线/EMI/BA 位卡住), 这里永不退出,
           ui_touch_poll 每帧都被调 -> 整机永久冻结, 只能断电。
           现在最多等约 20ms, 超时就返回失败(调用方按 -1 处理)。 */
        volatile int tw = 20000;

        while ((XIicPs_BusIsBusy(&touch_iic) != 0) && (tw > 0)) { tw--; }
    }
    return (st == XST_SUCCESS) ? 0 : -1;
}

/* reg_len: 1 = 8 位寄存器地址(FT5x06), 2 = 16 位地址(GT911) */
static int touch_reg_write(u8 addr, u32 reg, int reg_len, const u8 *data, u32 len)
{
    u8  tmp[24];
    int i;

    if ((len + (u32)reg_len) > (u32)sizeof(tmp)) {
        return -1;
    }
    for (i = 0; i < reg_len; i++) {
        tmp[i] = (u8)(reg >> (8 * (reg_len - 1 - i)));
    }
    for (i = 0; i < (int)len; i++) {
        tmp[reg_len + i] = data[i];
    }
    return i2c_write_bytes(addr, tmp, len + (u32)reg_len);
}

static int touch_reg_read(u8 addr, u32 reg, int reg_len, u8 *buf, u32 len)
{
    u8  tmp[4];
    int i;

    for (i = 0; i < reg_len; i++) {
        tmp[i] = (u8)(reg >> (8 * (reg_len - 1 - i)));
    }
    if (i2c_write_bytes(addr, tmp, (u32)reg_len) != 0) {   /* 先写寄存器地址 */
        return -1;
    }
    return i2c_read_bytes(addr, buf, len);                 /* 再读数据 */
}

/* ================= 调试: 扫描 I2C 地址 ================= */
void touch_scan_bus(void)
{
    static const u8 cand[] = {
        0x14u, 0x38u, 0x5Du, 0x70u, 0x2Cu, 0x2Eu, 0x48u, 0xBAu
    };
    u8  v = 0;
    u32 i;
    int found = 0;

    xil_printf("[TOUCH] I2C addr scan:");
    for (i = 0; i < (u32)(sizeof(cand) / sizeof(cand[0])); i++) {
        if (i2c_read_bytes(cand[i], &v, 1) == 0) {         /* 有 ACK */
            xil_printf(" 0x%02X", cand[i]);
            found++;
        }
    }
    if (found == 0) {
        xil_printf(" (none)");
    }
    xil_printf("\r\n");
}

/* ================= 控制器探测 ================= */
int touch_detect(void)
{
    static const u8 gt_addrs[2] = { GT911_ADDR_A, GT911_ADDR_B };
    u8  id[4];
    u8  v = 0;
    int i;

    touch_ic = TOUCH_IC_NONE;
    touch_addr = 0;

    /* GT911/GT9147: 读 0x8140 的产品 ID(ASCII) */
    for (i = 0; i < 2; i++) {
        (void)memset(id, 0, sizeof(id));
        if (touch_reg_read(gt_addrs[i], GT_REG_PRODUCT_ID, 2, id, 4) == 0) {
            if ((id[0] >= (u8)'0') && (id[0] <= (u8)'9')) {
                touch_ic = TOUCH_IC_GT911;
                touch_addr = gt_addrs[i];
                xil_printf("[TOUCH] GT9xx @0x%02X  ID=%c%c%c%c\r\n",
                           gt_addrs[i], id[0], id[1], id[2], id[3]);
                return (int)touch_ic;
            }
        }
    }

    /* FT5x06/FT5426: 读 0xA3 芯片 ID */
    if (touch_reg_read(FT_ADDR, FT_REG_CHIP_ID, 1, &v, 1) == 0) {
        u8 fw = 0;
        (void)touch_reg_read(FT_ADDR, FT_REG_FW_ID, 1, &fw, 1);
        touch_ic = TOUCH_IC_FT5X06;
        touch_addr = FT_ADDR;
        xil_printf("[TOUCH] FT5x06 @0x%02X  ChipID=0x%02X FwID=0x%02X\r\n",
                   FT_ADDR, v, fw);
        return (int)touch_ic;
    }

    xil_printf("[TOUCH] no controller detected\r\n");
    return (int)touch_ic;
}

/* ================= 初始化 ================= */
int touch_init(void)
{
    XIicPs_Config *cfg;
    s32 st;

#ifdef SDT
    cfg = XIicPs_LookupConfig((UINTPTR)XPAR_XIICPS_0_BASEADDR);
#else
    cfg = XIicPs_LookupConfig((u16)XPAR_XIICPS_0_DEVICE_ID);
#endif
    if (cfg == 0) {
        xil_printf("[TOUCH] IIC config not found\r\n");
        return -1;
    }

    st = XIicPs_CfgInitialize(&touch_iic, cfg, cfg->BaseAddress);
    if (st != XST_SUCCESS) {
        xil_printf("[TOUCH] IIC init failed (%d)\r\n", (int)st);
        return -1;
    }
    (void)XIicPs_SetSClk(&touch_iic, TOUCH_I2C_SCLK_HZ);

    touch_ready = 0;
    touch_down = 0;
    (void)memset(&touch_last, 0, sizeof(touch_last));

    if (touch_detect() == TOUCH_IC_NONE) {
        touch_scan_bus();
        return -1;
    }

    /* FT 系列: 确认处于正常工作模式(0x00) */
    if (touch_ic == TOUCH_IC_FT5X06) {
        u8 m = 0;
        if (touch_reg_read(touch_addr, FT_REG_DEV_MODE, 1, &m, 1) == 0) {
            xil_printf("[TOUCH] FT device mode = 0x%02X\r\n", m);
            if (m != 0u) {
                u8 z = 0u;
                (void)touch_reg_write(touch_addr, FT_REG_DEV_MODE, 1, &z, 1);
            }
        }
    }

    touch_ready = 1;
    xil_printf("[TOUCH] init OK (I2C 100kHz)\r\n");
    return 0;
}

int touch_get_ic(void)
{
    return (int)touch_ic;
}

/* ================= 读触摸点 ================= */
int touch_read_raw(u16 *x, u16 *y, u8 *id)
{
    u8 buf[8];
    u8 st;
    u8 n;

    if ((touch_ready == 0) || (touch_ic == TOUCH_IC_NONE)) {
        return 0;
    }

    if (touch_ic == TOUCH_IC_GT911) {
        if (touch_reg_read(touch_addr, GT_REG_STATUS, 2, &st, 1) != 0) {
            return 0;
        }
        if ((st & 0x80u) == 0u) {
            return 0;                              /* 数据未就绪 */
        }
        if ((st & 0x0Fu) == 0u) {                  /* 无触摸点 */
            u8 z = 0u;
            (void)touch_reg_write(touch_addr, GT_REG_STATUS, 2, &z, 1);
            return 0;
        }
        if (touch_reg_read(touch_addr, GT_REG_POINT1, 2, buf, 8) != 0) {
            return 0;
        }
        {
            u8 z = 0u;                             /* 必须清标志, 否则不再更新 */
            (void)touch_reg_write(touch_addr, GT_REG_STATUS, 2, &z, 1);
        }
        *x = (u16)(((u16)buf[1] << 8) | (u16)buf[0]);
        *y = (u16)(((u16)buf[3] << 8) | (u16)buf[2]);
        *id = buf[6];
        return 1;
    }

    if (touch_ic == TOUCH_IC_FT5X06) {
        if (touch_reg_read(touch_addr, FT_REG_TD_STATUS, 1, &n, 1) != 0) {
            return 0;
        }
        n &= 0x0Fu;
        if ((n == 0u) || (n > 5u)) {
            return 0;                              /* 0 = 无触摸 */
        }
        if (touch_reg_read(touch_addr, FT_REG_POINT1, 1, buf, 4) != 0) {
            return 0;
        }
        *x = (u16)(((u16)(buf[0] & 0x0Fu) << 8) | (u16)buf[1]);
        *y = (u16)(((u16)(buf[2] & 0x0Fu) << 8) | (u16)buf[3]);
        *id = 0;
        return 1;
    }

    return 0;
}

int touch_read(touch_point_t *pt)
{
    u16 rx = 0, ry = 0;
    u8  id = 0;
    s32 x, y, mx, my;

    if (pt == 0) {
        return 0;
    }
    (void)memset(pt, 0, sizeof(*pt));
    if (touch_read_raw(&rx, &ry, &id) == 0) {
        return 0;
    }

    x = (s32)rx;
    y = (s32)ry;
    mx = (s32)touch_cal.raw_w;
    my = (s32)touch_cal.raw_h;
    if (mx <= 0) {
        mx = 1;
    }
    if (my <= 0) {
        my = 1;
    }

    if (touch_cal.swap_xy != 0u) {                 /* 交换 X/Y */
        s32 t = x;
        x = y;
        y = t;
        t = mx;
        mx = my;
        my = t;
    }
    if (touch_cal.inv_x != 0u) {
        x = mx - 1 - x;
    }
    if (touch_cal.inv_y != 0u) {
        y = my - 1 - y;
    }
    if (x < 0) {
        x = 0;
    }
    if (x >= mx) {
        x = mx - 1;
    }
    if (y < 0) {
        y = 0;
    }
    if (y >= my) {
        y = my - 1;
    }

    pt->x = (u16)(((s64)x * (s64)TOUCH_LCD_W) / (s64)mx);   /* 缩放 + 限幅 */
    pt->y = (u16)(((s64)y * (s64)TOUCH_LCD_H) / (s64)my);
    pt->id = id;
    pt->pressed = 1;
    return 1;
}

int touch_poll_event(touch_point_t *pt)
{
    touch_point_t cur;
    int ev = TOUCH_EV_NONE;

    (void)memset(&cur, 0, sizeof(cur));
    if (touch_read(&cur) != 0) {
        cur.pressed = 1;
    }

    if (cur.pressed != 0u) {
        if (touch_down == 0u) {
            ev = TOUCH_EV_DOWN;
        } else if ((cur.x != touch_last.x) || (cur.y != touch_last.y)) {
            ev = TOUCH_EV_MOVE;
        } else {
            ev = TOUCH_EV_HOLD;
        }
        touch_down = 1;
        touch_last = cur;
    } else {
        if (touch_down != 0u) {
            ev = TOUCH_EV_UP;                      /* 抬起时坐标保留最后按下位置 */
        }
        touch_down = 0;
    }

    if (pt != 0) {
        *pt = touch_last;
    }
    return ev;
}

void touch_get_calib(touch_calib_t *c)
{
    if (c != 0) {
        *c = touch_cal;
    }
}

void touch_set_calib(const touch_calib_t *c)
{
    if (c != 0) {
        touch_cal = *c;
    }
}

/* ================= 自测循环 ================= */
void touch_test_raw_loop(void)
{
    u16 x = 0, y = 0;
    u8  id = 0;

    xil_printf("[TOUCH] raw loop: 触摸屏幕四角, 记下打印的原始值用于标定\r\n");
    while (1) {
        if (touch_read_raw(&x, &y, &id) != 0) {
            xil_printf("raw x=%4d y=%4d id=%d\r\n", (int)x, (int)y, (int)id);
        }
        usleep(50000);
    }
}

void touch_test_loop(void)
{
    touch_point_t pt;
    int ev;

    xil_printf("[TOUCH] test loop: 触摸屏幕, 串口打印 LCD 坐标\r\n");
    while (1) {
        ev = touch_poll_event(&pt);
        if (ev == TOUCH_EV_DOWN) {
            xil_printf("DOWN  x=%3d y=%3d\r\n", (int)pt.x, (int)pt.y);
        } else if (ev == TOUCH_EV_MOVE) {
            xil_printf("MOVE  x=%3d y=%3d\r\n", (int)pt.x, (int)pt.y);
        } else if (ev == TOUCH_EV_UP) {
            xil_printf("UP    x=%3d y=%3d\r\n", (int)pt.x, (int)pt.y);
        }
        usleep(20000);
    }
}

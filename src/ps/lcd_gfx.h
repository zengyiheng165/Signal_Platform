/******************************************************************************
 * lcd_gfx.h - 800x480 RGB565 TFT-LCD 绘图库接口
 *
 * 适用工程: vDMA_app (Zynq-7000 + AXI VDMA 24bit 流 + rgb2lcd RGB565 屏)
 *
 * 帧缓冲像素布局(与现有 helloworld/双缓冲一致):
 *   每像素 3 字节 { byte0(565低8位), byte1(565高8位), 0x00 },
 *   VDMA 流宽 24bit, rgb2lcd 固定取低16位为 RGB565。
 *
 * 用法(配合你现有的双缓冲切换):
 *   1) 先在"空闲缓冲"上作画:
 *        lcd_clear(nxt_fb, LCD_COLOR_BLACK);
 *        lcd_fill_rect(nxt_fb, 10, 10, 100, 50, LCD_COLOR_RED);
 *        lcd_text(nxt_fb, 20, 300, "Hello LCD!", LCD_COLOR_WHITE, 2);
 *   2) 画完后刷新 D-Cache(一次即可):
 *        lcd_flush(nxt_fb);
 *   3) 再调用你自己的 switch_frame() 切到屏幕。
 *   注意: 绘图函数本身不刷 cache、不切屏, 只是写帧缓冲。
 ******************************************************************************/
#ifndef LCD_GFX_H
#define LCD_GFX_H

#include "xil_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- 分辨率(可通过编译宏覆盖) ---------------- */
#ifndef LCD_GFX_WIDTH
#define LCD_GFX_WIDTH  800
#endif
#ifndef LCD_GFX_HEIGHT
#define LCD_GFX_HEIGHT 480
#endif
#define LCD_GFX_FRAME_BYTES  ((u32)(LCD_GFX_WIDTH) * (u32)(LCD_GFX_HEIGHT) * 3u)

/* ---------------- 常用颜色(RGB565) ---------------- */
#define LCD_COLOR_BLACK     0x0000
#define LCD_COLOR_WHITE     0xFFFF
#define LCD_COLOR_RED       0xF800
#define LCD_COLOR_GREEN     0x07E0
#define LCD_COLOR_BLUE      0x001F
#define LCD_COLOR_YELLOW    0xFFE0
#define LCD_COLOR_CYAN      0x07FF
#define LCD_COLOR_MAGENTA   0xF81F
#define LCD_COLOR_ORANGE    0xFD20
#define LCD_COLOR_GRAY      0x8410
#define LCD_COLOR_LGRAY     0xBDF7
#define LCD_COLOR_DGRAY     0x4208

/* 888 -> 565 换算 */
u16 lcd_rgb565(u8 r, u8 g, u8 b);

/* ---------------- 帧缓冲级 ---------------- */
void lcd_clear(u32 fb, u16 color);   /* 整帧填充(不刷cache) */
void lcd_flush(u32 fb);              /* 整帧刷新 D-Cache(切屏前调用) */
void lcd_pixel(u32 fb, int x, int y, u16 color);

/* 保护色: 设为非 0 后, 后续绘图不会覆盖"已经是该颜色"的像素(传 0 取消)。
   示波器用它让 CH2 不压掉 CH1 —— 两路波形交叉处否则会被割出 1 像素缺口,
   看起来像波形错开/断开。 */
void lcd_set_protect(u16 color);

/* ---------------- 基本图元(坐标越界自动裁剪) ---------------- */
void lcd_hline(u32 fb, int x0, int x1, int y, u16 color);
void lcd_vline(u32 fb, int x, int y0, int y1, u16 color);
void lcd_line(u32 fb, int x0, int y0, int x1, int y1, u16 color);
void lcd_rect(u32 fb, int x, int y, int w, int h, u16 color);      /* 空心矩形, w/h为宽高(px) */
void lcd_fill_rect(u32 fb, int x, int y, int w, int h, u16 color); /* 实心矩形 */
void lcd_circle(u32 fb, int cx, int cy, int r, u16 color);
void lcd_fill_circle(u32 fb, int cx, int cy, int r, u16 color);
void lcd_ellipse(u32 fb, int cx, int cy, int rx, int ry, u16 color);
void lcd_fill_ellipse(u32 fb, int cx, int cy, int rx, int ry, u16 color);

/* 1bpp 掩码位图: 只画掩码里为 1 的像素(为 0 的像素不动 -> 底下的背景/边框原样保留)。
   掩码按行存放, 每行 (w+7)/8 字节, 每字节 MSB 在最左边。
   用途: 细描边图标(如频谱仪顶栏的"设置"齿轮), 形状精确且不需要外部图片资源。 */
void lcd_blit_mask(u32 fb, int x, int y, int w, int h, const u8 *mask, u16 color);

/* ---------------- 折线 / 曲线 ---------------- */
/* pts: {x0,y0, x1,y1, ...} 连续点坐标,npts: 点数(>=2) */
void lcd_polyline(u32 fb, const int *pts, int npts, u16 color);

/* 函数曲线: 屏上像素 y = y_center - fn(x)*yscale, x 取 x0..x1(逐像素) */
typedef s32 (*lcd_fn_t)(s32 x, void *ctx);
void lcd_plot(u32 fb, int x0, int x1, int y_center, s32 yscale,
              lcd_fn_t fn, void *ctx, u16 color);

/* 二次贝塞尔曲线(P0,P1控制,P2终点), steps 采样段数 */
void lcd_bezier2(u32 fb, int x0, int y0, int x1, int y1, int x2, int y2,
                 u16 color, int steps);

/* ---------------- 文本(内置 8x8 ASCII 字体, 0x20~0x7E) ----------------
 * scale: 1=8x8, 2=16x16 ...  返回前进宽度(px)
 * '\r' '\n' 不绘制也不计宽, 由调用方换行。
 * 注意: 内置字库只有 ASCII, 中文需自行扩展点阵(见文件内注释)。
 */
int  lcd_char(u32 fb, int x, int y, char ch, u16 color, u8 scale);
int  lcd_text(u32 fb, int x, int y, const char *s, u16 color, u8 scale);
/* 带背景: 每字符先填 bg 色块再画前景色(适合做 UI 文字) */
int  lcd_text_bg(u32 fb, int x, int y, const char *s,
                 u16 color, u16 bg, u8 scale);
u32  lcd_text_width(const char *s, u8 scale);

/* ---------------- 数字(可选) ---------------- */
int  lcd_int(u32 fb, int x, int y, s32 value, u16 color, u8 scale);
int  lcd_uint(u32 fb, int x, int y, u32 value, u16 color, u8 scale);
int  lcd_hex(u32 fb, int x, int y, u32 value, u16 color, u8 scale);

#ifdef __cplusplus
}
#endif

#endif /* LCD_GFX_H */

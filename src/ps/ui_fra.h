/******************************************************************************
 * ui_fra.h - 第四界面: 频率特性分析仪(先做界面, 用演示曲线; 采集/计算后续接)
 * 扫频参数: 500Hz~1MHz, 步长 500Hz(1x分辨率) / 1KHz(2x分辨率), fs=8.192MHz, N=16384
 * 显示: 左上 幅频(dB) / 右上 相频(度) / 左下 冲激响应 / 右下 阶跃响应
 ******************************************************************************/
#ifndef UI_FRA_H
#define UI_FRA_H

#include "xil_types.h"

#define UI_FRA_MAX_PTS  2048      /* 最大扫频点数(500Hz 步长最多 2000 点) */
/* ★ 冲激响应点数必须 = PL FFT IP 的固定点数 16384!
   fft_pl_accelerate/ifft_pl_accelerate 是按 fft_len 算 DMA 长度的(4/8 字节每点),
   若这里填 4096 而 IP 实际做 16384 点, RX 会多写 3 倍数据 -> 踩内存。
   16384 点正好用满分配的缓冲: TX 16384*4=64KB=MAX_PKT_LEN, RX 16384*8=128KB=MAX_PKT_LEN*2。
   时基: 栅格 Δf=500Hz -> f_max=8192*500=4.096MHz, dt=1/(2*f_max)=122ns, 记录 2ms。 */
#define UI_FRA_IFFT_N   16384     /* 必须与 fra_core.h 的 FRA_IFFT_N 一致, 且 = FFT_N */

typedef struct {
    s32  f_start_hz;    /* 起始频率(下限 1kHz, 加减档 10kHz) */
    s32  f_stop_hz;     /* 终止频率(同上) */
    s32  step_hz;       /* 步长: 500 或 1000(有效), 1/10/100 仅显示不可选 */
    s32  vpp_mv;        /* 激励幅度 mVpp */
    u8   avg;           /* 平均次数 1/2/4/8 */
    u8   order;         /* 拟合阶数 1..6 */
    u8   y_db;          /* 1 = 幅频用 dB, 0 = 线性 */
    u8   running;       /* 1 = 正在扫频(界面先留着) */
    s32  warn_s;        /* 用时预警阈值(秒), 0 = 不预警 */
    u8   set_open;      /* 1 = 正在显示"设置"整页 */
    u8   confirm_open;  /* 1 = 正在显示"预计用时"确认弹窗 */

    /* ---- 实测数据(扫频结果, 由 helloworld.c 的 fra_sweep_step() 填) ---- */
    float habs[UI_FRA_MAX_PTS];   /* |H| = |被测|/|参考| */
    float hpha[UI_FRA_MAX_PTS];   /* 相位(度), 已解卷绕 */
    u16   npt;                    /* 已测点数 */
    s32   f_meas_hz;              /* 当前正在测的频率(进度显示) */
    u8    redraw_curve;           /* 1 = 曲线有新数据, 请在帧边界重画 */
    u8    view;                   /* 当前显示的响应曲线: 0=幅频 1=相频 2=冲激 3=阶跃 */
    u8    pick_open;              /* 1 = 正在显示"选择曲线"弹窗 */
    u8    draw_popup;             /* 1 = 请在帧边界把弹窗画到曲线上(不整屏重画) */
    u8    draw_confirm;           /* 1 = 请在帧边界把"预计用时"确认窗画到曲线上 */
    u8    draw_set;               /* 1 = 请在帧边界把"设置"页画到曲线区上 */

    /* ---- 拟合 + 时域响应(由 helloworld.c 的 fra_solve() 填) ---- */
    float imp[UI_FRA_IFFT_N];     /* 冲激响应 */
    float stp[UI_FRA_IFFT_N];     /* 阶跃响应 */
    /* ★ PL 二阶模型算出的时域响应(与实测 imp/stp 同长同算法, 用于叠加显示拟合曲线) */
    float imp2[UI_FRA_IFFT_N];    /* 模型冲激响应 */
    float stp2[UI_FRA_IFFT_N];    /* 模型阶跃响应 */
    float imp2_lo, imp2_hi;       /* 模型冲激的自动量程 */
    float stp2_lo, stp2_hi;       /* 模型阶跃的自动量程 */
    u8    fit_time_ok;            /* 1 = 模型时域响应有效 */
    float fit_mag[UI_FRA_MAX_PTS];/* 拟合曲线: 幅频(与实测同点) */
    float fit_pha[UI_FRA_MAX_PTS];/* 拟合曲线: 相频(已对到实测那一支) */
    s32   imp_fmax_hz;            /* 逆变换用的 f_max: 时基 dt = 1/(2*f_max) */
    s32   imp_shift_us;           /* 冲激响应时间原点对齐移了多少(us) */
    float fit_res;                /* 拟合相对残差 */
    u8    fit_ok;                 /* 1 = 最小二乘拟合成功(只用于幅频/相频的白色叠加线) */
    /* ★ 拟合出来的传递函数 H(s) = (b0 + b1*s) / (s^2 + a1*s + a0) 的系数(幅频/相频右上角显示框用) */
    float tf_b0, tf_b1, tf_a0, tf_a1;
    u8    tf_ok;
    /* ★ 由系数算出的极点(实部/虚部, 两个共轭极点; 上面显示框第二三行用) */
    float tf_pr, tf_pi;
    u8    ifft_ok;                /* 1 = 实测频率响应的逆 FFT 成功(冲激/阶跃可用) */
    s32   fc_hz;                  /* 截止频率(-3dB 点, 由幅频实测数据求出) */
    float fc_lvl;                 /* 截止电平(线性) = 截止那个网格点的实测电平(≈0.7071); 兜底路径 = 0.7071*实测峰值 */
    u8    fc_ok;                  /* 1 = 找到截止频率 */
    u8    run_win;                /* 0=无窗口 1=待画"正在运行" 2=待画"运行结束" 3=已画过 */
    u16   cur_idx;               /* ★ 光标所在实测点下标(频率视图) */
    u8    cur_on;                /* 1 = 显示光标 */
    u8    cur_draw;              /* 1 = 只重画光标(不重画曲线区 -> 不闪) */
    /* ★ 冲激/阶跃的自动量程(只在计算完成时定一次, 显示期间不变) */
    float imp_lo;                 /* 冲激响应显示的纵轴下界(V) */
    float imp_hi;                 /* 上界(V) */
    float stp_lo;                 /* 阶跃响应下界(V) */
    float stp_hi;                 /* 上界(V) */
    u8    fit_order;              /* 实际用上的阶数 */
    float dbg_h0;                 /* 自检: h[0] */
    float dbg_sumh;               /* 自检: 阶跃终值(=Σh, 应等于直流增益) */
    float dbg_h0dc;               /* 自检: 模型直流增益 |H(0)| */
    s32   dbg_peak_n;             /* 自检: |h| 峰值下标 */
    u8    redraw_pane;            /* 1 = 请在帧边界只重画曲线区(关闭弹窗用) */
    u8    redraw_bar;             /* 1 = 请在帧边界重画按钮栏(曲线名/参数变了, 否则标签慢一拍) */
} ui_fra_t;

void ui_fra_default(ui_fra_t *a);
int  ui_fra_est_sec(const ui_fra_t *a);   /* 预计用时(秒): 点数 x 平均 x 8ms */
const char *ui_fra_view_short(u8 v);              /* 曲线名(按钮栏用): 幅频/相频/冲激/阶跃 */
void ui_fra_draw(u32 fb, ui_fra_t *a);
void ui_fra_draw_chrome(u32 fb, ui_fra_t *a);   /* 只重画顶栏+信息行(按钮动作后调用, 避免整屏重画) */
void ui_fra_draw_live(u32 fb, ui_fra_t *a);
void ui_fra_flush(u32 fb);
void ui_fra_cursor_refresh(u32 fb, ui_fra_t *a);   /* 只擦旧光标+画新光标 */
void ui_fra_run_win_draw(u32 fb, const ui_fra_t *a, int finished);   /* 扫频子窗口 */
void ui_fra_flush_chrome(u32 fb);


#endif /* UI_FRA_H */
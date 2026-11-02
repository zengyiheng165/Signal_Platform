#ifndef  __CZT_H__
#define  __CZT_H__

#include "math.h"
#include "plfft.h"

#ifndef PI
#define PI 3.14159265358979323846
#endif

#define TWO_PI (2.0 * PI)   /* 2*pi，仅本文件内部使用 */

typedef struct {
    double re;
    double im;
} czt_complex_t;


typedef struct {
    double fs;        /* 采样率 (Hz)*/
    double f_start;   /* 起始频率 */
    double f_end;     /* 结束频率 */
    int n;            /* 采样点数 */
    int m;            /* 输出点数 */     
} czt_plan_t;


int czt_max_index(double *yout, int m);

/**
* @brief  求CZT变换。
* @param  plan CZT变换参数，具体可以查看头文件。
* @param  xin 实数输入,长度为N
* @param  yout 幅度输出（缩放到真实幅度），长度为M
* @param  AxiDman 选择硬件加速的 DMA 节点
* @return 最大值所在的频率
* @note   使用该函数时，必须确保log2（plan.n+plan.m-1）的向上取整小于16384个点
*/
int czt(czt_plan_t plan, int16_t *xin , double*yout,XAxiDma AxiDman);

/**
* @brief  预计算CZT的三组旋转因子(啁啾)，只为 czt_cached() 服务。
* @param  plan CZT变换参数，含义与 czt() 完全相同。
* @return 0 成功；-1 参数非法(点数超过 CZT_MAX_L 等)。
* @note   三组因子只与 fs/f_start/f_end/n/m 有关，与输入数据无关：
*             gn[i] = exp(j*Ax*(-i)) * exp(j*Wx*i*i/2)                  （g 的调制因子，N 个）
*             h[p]  = p<M-1 ? exp(-j*Wx*p*p/2) : exp(-j*Wx*(L-p)^2/2)   （h 啁啾，L 个）
*             wn[i] = exp(j*Wx*i*i/2)                                    （输出因子，M 个）
*         所以只在"频段/点数改变"时调用一次即可；参数不变时 czt_cached() 内部会跳过重建。
*         czt() 每调用一次都要现算这三组(约 2.4 万次 sin/cos，25~50ms)，
*         需要"每帧都算"时必须改用 czt_cached()。
*/
int czt_prepare(czt_plan_t plan);

/**
* @brief  用预计算好的啁啾做CZT变换，数学上与 czt() 完全等价。
* @param  plan CZT变换参数；与上次不同时会自动调用 czt_prepare() 重建缓存。
* @param  xin 实数输入,长度为N
* @param  yout 幅度输出（缩放到真实幅度），长度为M
* @param  AxiDman 选择硬件加速的 DMA 节点（与 czt() 相同）
* @return 0 成功；-1 参数非法或 PL FFT 失败
* @note   稳定后每帧只需 N 次复数乘 + 3 次 PL FFT(约 0.5~1ms)，可用于"每帧精测"。
*         与 czt() 的唯一差别：h 啁啾来自缓存，而 czt_fft() 是就地变换，
*         所以算 fft(h) 之前必须先把缓存拷到 s_czt_h，否则会把缓存算坏。
*/
int czt_cached(czt_plan_t plan, int16_t *xin, double *yout, XAxiDma AxiDman);

/**
* @brief  取上一次 czt_prepare() 算出的变换长度 L。
* @return L = next_pow2(n+m-1)；0 表示还没有有效缓存。
* @note   供调用方核对"L 是否等于 PL 的 FFT 点数设置"。
*/
int czt_cached_L(void);

#endif
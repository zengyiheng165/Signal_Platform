/* fra_core.h - 频率特性分析仪数学核心(纯计算, 只依赖 math.h)
 * 约定: FFT 长度 N, 采样率 fs, 分辨率 df = fs/N; 扫频点都落在 df 的整数倍上(相干)。
 *       H(s) = B(s)/A(s), A(s) = 1 + a1 s + ... + an s^n, B(s) = b0 + b1 s + ... + bm s^m
 */
#ifndef FRA_CORE_H
#define FRA_CORE_H

#define FRA_MAX_ORDER   6        /* 分子/分母最高阶 */
#define FRA_IFFT_N     16384     /* 冲激响应 IFFT 点数(2 的幂); 必须 = PL FFT IP 的固定点数 16384 */

/* 频率 -> bin 号(f 应为 fs/N 的整数倍); 非法返回 -1 */

/* PS 端单点 DFT: 对 x[0..n-1] 求第 k 个 bin 的复数(去直流, 递推相位因子, 约 0.15ms@16384) */
/* 取第 k 个 bin 的复数(FFT 输出已含实/虚部) */

/* 复数相除 H = A / B(直通校准: H_dut = H_meas / H_cal) */

/* 最小二乘拟合(Levy + Sanathanan-Koerner 迭代): 输入 np 个频点与复数响应,
 * 输出 b[0..m] 与 a[1..n](a[0] 隐含 1); 返回相对残差 RMS(<0 = 失败) */
double fra_fit(const double *f_hz, const double *h_re, const double *h_im, int np,
               int m, int n, double *b, double *a);

/* 用拟合系数求 H(jw) */
void   fra_eval(const double *b, int m, const double *a, int n, double f_hz,
                double *o_re, double *o_im);

/* 冲激响应: 输入正频率响应 H(0..f_max), 共 nf = FRA_IFFT_N/2+1 点(可由 fra_eval 在
 * 均匀栅格上求值, 含模型外推), 输出 n 点实序列(时基 dt = 1/(2*f_max)) */

/* 阶跃响应 = 冲激响应的累加(不含 dt); 收官值应等于直流增益 |H(0)| */
void   fra_step(const float *h, int n, float *s);

#endif /* FRA_CORE_H */

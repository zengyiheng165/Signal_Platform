#ifndef  _IIR_FILTER_DESIGN_H__
#define  _IIR_FILTER_DESIGN_H__

/**
* @brief  设计任意阶数低通 IIR 滤波器（Butterworth 最大平坦型）
* @param  fs     采样频率，单位 Hz，要求 fs > 0 且为有限值
* @param  fc     截止频率（-3dB 点），单位 Hz，要求 0 < fc < fs/2
* @param  order  滤波器阶数，允许范围 1 ~ 20（更高阶直接型系数已病态）
* @param  b      输出：x 的系数数组，长度至少 order+1，不能为 NULL
* @param  a      输出：y 的系数数组，长度至少 order+1，不能为 NULL
* @return 1：设计成功，b[]、a[] 有效
*         0：参数不合格（指针为空、阶数越界、fs/fc 非法）或内存分配失败
* @note   1) 返回 0 时 b[]、a[] 不会被写入，请勿使用；
*            返回 0 时也不会残留已分配的内存
*         2) 两数组均按 z^-1 升幂排列，且 a[0] 恒为 1、直流增益恒为 1
*         3) 直接 I 型差分方程：
*            y[n] = b[0]*x[n] + b[1]*x[n-1] + ... + b[N]*x[n-N]
*                             - a[1]*y[n-1] - ... - a[N]*y[n-N]
*         4) 阶数高且 fs/fc 很大时（N>=10 且 fs/fc>=40）直接型系数病态，
*            建议降低阶数或改用二阶节(biquad)级联
*         5) fc 越接近 fs/2，双线性变换越病态，实际使用建议 fc <= 0.45*fs
*/

int iir_lowpass(double fs, double fc, int order, double *b, double *a);
/**
* @brief  设计任意阶数高通 IIR 滤波器（Butterworth 最大平坦型）
* @param  fs     采样频率，单位 Hz，要求 fs > 0 且为有限值
* @param  fc     截止频率（-3dB 点），单位 Hz，要求 0 < fc < fs/2
* @param  order  滤波器阶数，允许范围 1 ~ 20（更高阶直接型系数已病态）
* @param  b      输出：x 的系数数组，长度至少 order+1，不能为 NULL
* @param  a      输出：y 的系数数组，长度至少 order+1，不能为 NULL
* @return 1：设计成功，b[]、a[] 有效
*         0：参数不合格（指针为空、阶数越界、fs/fc 非法）或内存分配失败
* @note   1) 返回 0 时 b[]、a[] 不会被写入，也不会残留已分配的内存
*         2) 两数组均按 z^-1 升幂排列，a[0] 恒为 1；全部零点位于 z = +1，
*            因此直流增益为 0、奈奎斯特频率处增益为 1
*         3) 直接 I 型差分方程：
*            y[n] = b[0]*x[n] + b[1]*x[n-1] + ... + b[N]*x[n-N]
*                             - a[1]*y[n-1] - ... - a[N]*y[n-N]
*         4) 阶数高且 fc 离奈奎斯特越近，直接型系数越病态
*            （N>=10 且 (fs/2)/fc 很大时），建议改用二阶节(biquad)级联
*/

int iir_highpass(double fs, double fc, int order, double *b, double *a);
/**
* @brief  设计任意阶数带通 IIR 滤波器（Butterworth 最大平坦型）
* @param  fs     采样频率，单位 Hz，要求 fs > 0 且为有限值
* @param  f1     下截止频率（-3dB 点），单位 Hz，要求 0 < f1 < f2 < fs/2
* @param  f2     上截止频率（-3dB 点），单位 Hz，要求 f1 < f2 < fs/2
* @param  order  低通原型阶数，允许范围 1 ~ 10（实际滤波器阶数为 2*order）
* @param  b      输出：x 的系数数组，长度至少 2*order+1，不能为 NULL
* @param  a      输出：y 的系数数组，长度至少 2*order+1，不能为 NULL
* @return 1：设计成功，b[]、a[] 有效
*         0：参数不合格（指针为空、阶数越界、fs/f1/f2 非法）或内存分配失败
* @note   1) 返回 0 时 b[]、a[] 不会被写入，也不会残留已分配的内存
*         2) 实际阶数 N = 2*order，两数组按 z^-1 升幂排列，a[0] 恒为 1
*         3) 全部零点位于 z = +1 与 z = -1（各 order 个），
*            因此直流与奈奎斯特频率处增益均为 0
*         4) 增益在中心频率 f0 = sqrt(f1*f2) 处归一化为 1，
*            在 f1 与 f2 处为 -3dB
*         5) 直接 I 型差分方程（N = 2*order）：
*            y[n] = b[0]*x[n] + b[1]*x[n-1] + ... + b[N]*x[n-N]
*                             - a[1]*y[n-1] - ... - a[N]*y[n-N]
*         6) 相对带宽越窄（(f2-f1)/f0 越小）、阶数越高，直接型系数越病态，
*            建议改用二阶节(biquad)级联
*/
int iir_bandpass(double fs, double f1, double f2, int order, double *b, double *a);

/**
* @brief  设计任意阶数带阻（陷波）IIR 滤波器（Butterworth 最大平坦型）
* @param  fs     采样频率，单位 Hz，要求 fs > 0 且为有限值
* @param  f1     阻带下边沿（-3dB 点），单位 Hz，要求 0 < f1 < f2 < fs/2
* @param  f2     阻带上边沿（-3dB 点），单位 Hz，要求 f1 < f2 < fs/2
* @param  order  低通原型阶数，允许范围 1 ~ 10（实际滤波器阶数为 2*order）
* @param  b      输出：x 的系数数组，长度至少 2*order+1，不能为 NULL
* @param  a      输出：y 的系数数组，长度至少 2*order+1，不能为 NULL
* @return 1：设计成功，b[]、a[] 有效
*         0：参数不合格（指针为空、阶数越界、fs/f1/f2 非法）或内存分配失败
* @note   1) 返回 0 时 b[]、a[] 不会被写入，也不会残留已分配的内存
*         2) 实际阶数 N = 2*order，两数组按 z^-1 升幂排列，a[0] 恒为 1
*         3) 零点全部位于单位圆上的阻带内（共轭成对），因此直流与奈奎斯特
*            频率处增益均为 1 —— 这是带阻与带通最重要的区别
*         4) f1 与 f2 处增益为 -3dB，阻带中心 f0 = sqrt(f1*f2) 附近被深度抑制
*         5) 直接 I 型差分方程（N = 2*order）：
*            y[n] = b[0]*x[n] + ... + b[N]*x[n-N] - a[1]*y[n-1] - ... - a[N]*y[n-N]
*         6) 阻带越窄（f2/f1 越接近 1）、阶数越高，直接型系数越病态，
*            建议改用二阶节(biquad)级联
*/
int iir_bandstop(double fs, double f1, double f2, int order, double *b, double *a);

/**
* @brief  把任意阶数 IIR 滤波器系数（b 与 a）量化成 12bit 有符号整数
* @param  b   输入：x 的系数（double），长度 n
* @param  a   输入：y 的系数（double），长度 n
* @param  n   系数个数（任意阶数，n = 实际阶数 + 1）
* @param  bq  输出：量化的 x 系数，取值 [-2048, 2047]，长度 n
* @param  aq  输出：量化的 y 系数，取值 [-2048, 2047]，长度 n
* @param  q   输出：公共定标指数，b[k] ≈ bq[k] / 2^q 且 a[k] ≈ aq[k] / 2^q
* @return n ：全部系数都在 12bit 内，量化干净完成
*         -n：有系数被限幅到 ±2048/±2047
*         0 ：参数非法（指针为空或 n < 1）
* @note   1) bq/aq 为 int 型，但数值严格限制在 12bit 有符号范围内
*            （[-2048, 2047]），可直接截断成 12bit 或写进 FPGA 系数寄存器。
*         2) b 与 a 共用同一定标 2^q，量化前后差分方程完全等价，无需增益换算：
*              y[n] = ( Σ bq[k]·X[n-k] ? Σ_{k>=1} aq[k]·Y[n-k] ) / 2^q
*         3) q 由两个数组的全局最大值 max(|b|,|a|) 决定：
*              q = floor( log2( 2047 / max ) )     保证 max·2^q <= 2047
*         4) 采用四舍五入，不是截断。
*         5) 【使用前必读】共用定标下精度分配固定，仅当 max|b| 与 max|a| 量级
*            接近时合用。先检查 d = |log2( max|a| / max|b| )|：
*              d <= 4 bit ：可用（实测时域误差 < 1%）
*              4 < d < 8  ：勉强可用（1% ~ 5%），建议改用二阶节
*              d >= 8 bit ：不可用，b 会被全部量化为 0（实测 d=13 时 9~17 个
*                           b 系数全变 0，滤波器输出恒为 0）
*            量级差过大时请先做整体预缩放，或改用二阶节级联逐节量化。
*         6) 累加位宽：n <= 8 时 2047*2047*8 ≈ 3.4e7，int32 安全；
*            更大 n 建议用 int64 累加。
*/
int iir_quantize_12bit(const double *b, const double *a, int n,
                       int *bq, int *aq, int *q);
#endif

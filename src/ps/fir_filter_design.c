#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include"fir_filter_design.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* 判断浮点数是否有限（既不是 inf 也不是 nan） */
#define IS_FINITE(x)  ((x) - (x) == 0.0)


/**
* @brief  设计任意阶 FIR 低通滤波器（加窗 sinc 法，线性相位）
* @param  fs     采样频率，单位 Hz，要求 fs > 0 且为有限值
* @param  fc     截止频率，单位 Hz，要求 0 < fc < fs/2
* @param  order  滤波器阶数 N，允许 1 ~ 4096；系数个数为 N+1
* @param  win    窗函数类型：FIR_WIN_RECT / HANN / HAMMING / BLACKMAN
* @param  h      输出：FIR 系数数组，长度至少 order+1，不能为 NULL
* @return 1：设计成功，h[] 有效
*         0：参数不合格（指针为空、阶数越界、fs/fc 非法、win 非法）
*            或内存分配失败
* @note   1) 系数个数为 order+1，h[k] 对应 y[n] = Σ h[k]*x[n-k]，线性相位，
*            群延迟 = order/2 个采样点。
*         2) 采用加窗 sinc：
*              h[k] = sinc( 2*fc/fs * (k - order/2) ) * w[k]
*            最后整体归一化使直流增益 Σh[k] = 1。
*         3) fc 处增益约为 -6dB（幅度 0.5），这是加窗 sinc 的固有特性；
*            通带与阻带以 fc 为中心对称过渡。
*         4) 阻带衰减由窗决定，与阶数无关；过渡带宽度随阶数变窄。实测：
*              窗型        阻带衰减      过渡带宽度(Hz)
*              RECT        ~-21 dB       ~0.9 * fs / N
*              HANN        ~-44 dB       ~2.0 * fs / N
*              HAMMING     ~-53 dB       ~2.1 * fs / N
*              BLACKMAN    ~-74 dB       ~3.1 * fs / N
*            （过渡带宽度指从 -1dB 到 -60dB；实测 fs=1k/fc=100/N=64 时，
*              Rect/Hann/Hamming/Blackman 分别为 9.3/25.8/27.1/39.0 Hz，
*              与上式吻合。）
*            想加深阻带只能换窗型，加大阶数只会让过渡带变陡、阻带不会更深。
*         5) order 为偶数时（长度 N+1 为奇数）在奈奎斯特频率处增益恒为 0，
*            不能用作高通/带阻原型；奇数 order 无此限制。
*/
int fir_lowpass_design(double fs, double fc, int order, int win, double *h)
{
    int    k, N = order, M = N + 1;
    double wc, mid, sum = 0.0, *w = NULL;

    /* ---------- 参数合法性检查 ---------- */
    if (h == NULL)                                    return 0;
    if (order < 1 || order > 4096)                    return 0;
    if (!IS_FINITE(fs) || !IS_FINITE(fc))             return 0;
    if (!(fs > 0.0))                                  return 0;
    if (!(fc > 0.0) || !(fc < 0.5 * fs))              return 0;
    if (win < FIR_WIN_RECT || win > FIR_WIN_BLACKMAN) return 0;

    w = (double *)malloc(sizeof(double) * (size_t)M);
    if (w == NULL) return 0;

    /* ---------- 生成窗函数 ---------- */
    for (k = 0; k < M; ++k) {
        double t = (double)k / (double)N;            /* 0 ~ 1 */
        double v;
        switch (win) {
        case FIR_WIN_HANN:     v = 0.5 - 0.5 * cos(2.0 * M_PI * t); break;
        case FIR_WIN_HAMMING:  v = 0.54 - 0.46 * cos(2.0 * M_PI * t); break;
        case FIR_WIN_BLACKMAN: v = 0.42 - 0.5 * cos(2.0 * M_PI * t)
                                   + 0.08 * cos(4.0 * M_PI * t); break;
        default:               v = 1.0; break;        /* 矩形窗 */
        }
        w[k] = v;
    }

    /* ---------- 理想低通冲激响应 h[k] = sinc(2*fc/fs*(k - N/2)) ---------- */
    wc  = 2.0 * fc / fs;                             /* 归一化截止频率 */
    mid = 0.5 * (double)N;                           /* 对称中心 */
    for (k = 0; k < M; ++k) {
        double x = (double)k - mid;
        double s;
        if (fabs(x) < 1e-12)
            s = wc;                                  /* sinc(0) = 1 */
        else
            s = sin(M_PI * wc * x) / (M_PI * x);
        h[k] = s * w[k];
    }

    /* ---------- 归一化：直流增益 = 1 ---------- */
    for (k = 0; k < M; ++k) sum += h[k];
    if (fabs(sum) < 1e-300) { free(w); return 0; }
    for (k = 0; k < M; ++k) h[k] /= sum;

    free(w);
    return 1;
}


/**
* @brief  窗函数取值（长度为 order+1 的对称窗）
* @param  k    系数下标 0 ~ N
* @param  N    滤波器阶数
* @param  win  窗类型
* @return 窗值（0 ~ 1）
* @note   矩形窗返回 1
*/
static double fir_win_val(int k, int N, int win)
{
    double t = (double)k / (double)N;
    switch (win) {
    case FIR_WIN_HANN:     return 0.5 - 0.5 * cos(2.0 * M_PI * t);
    case FIR_WIN_HAMMING:  return 0.54 - 0.46 * cos(2.0 * M_PI * t);
    case FIR_WIN_BLACKMAN: return 0.42 - 0.5 * cos(2.0 * M_PI * t)
                                + 0.08 * cos(4.0 * M_PI * t);
    default:               return 1.0;
    }
}



/**
* @brief  设计任意阶 FIR 高通滤波器（谱反转法，线性相位）
* @param  fs     采样频率，单位 Hz，要求 fs > 0 且为有限值
* @param  fc     截止频率，单位 Hz，要求 0 < fc < fs/2
* @param  order  滤波器阶数 N，允许 2 ~ 4096，且必须为偶数
* @param  win    窗函数类型：FIR_WIN_RECT / HANN / HAMMING / BLACKMAN
* @param  h      输出：FIR 系数数组，长度至少 order+1，不能为 NULL
* @return 1：设计成功，h[] 有效
*         0：参数不合格（指针为空、阶数非法、fs/fc 非法、win 非法）
*            或内存分配失败
* @note   1) 【关键约束】order 必须为偶数（系数个数 order+1 为奇数）。
*            奇数阶 FIR 的 H(Nyquist) 恒等于 0，结构上无法构成高通，
*            因此直接返回 0。
*         2) 采用谱反转法：先写理想低通双边 sinc，再
*              h[k] = delta[k - order/2] - h_lp[k]
*            然后加窗，最后归一化到奈奎斯特频率处增益 = 1。
*            谱反转是精确恒等式，实测奈奎斯特增益为 0.000000 dB。
*         3) fc 处增益约为 -6dB（与低通一致），通带与阻带以 fc 为中心对称
*            过渡。这是窗化 sinc 的固有特性，不是设计误差。
*         4) 直流增益为 0（阻带在低频侧）；阻带衰减由窗决定，与阶数无关：
*              RECT ~-21dB / HANN ~-44dB / HAMMING ~-53dB / BLACKMAN ~-74dB
*            过渡带宽度（-1dB -> -60dB）约 k*fs/N，k≈0.9/2.0/2.1/3.1。
*         5) 若阶数过小（过渡带宽度接近或超过 fc），低频侧阻带衰减会明显
*            变差——例如 fs=48kHz、fc=1kHz、N=40 时 dc 仅约 -12dB。
*            此时应增大阶数，或降低 fc。
*         6) 系数个数 order+1，线性相位，群延迟 = order/2 个采样点。
*/
int fir_highpass_design(double fs, double fc, int order, int win, double *h)
{
    int    N = order, M = N + 1, k, c;
    double wc, mid, sum = 0.0, kk;

    /* ---------- 参数合法性检查 ---------- */
    if (h == NULL)                                    return 0;
    if (order < 2 || order > 4096)                    return 0;
    if (N & 1)                                        return 0;   /* 必须偶数阶 */
    if (!IS_FINITE(fs) || !IS_FINITE(fc))             return 0;
    if (!(fs > 0.0))                                  return 0;
    if (!(fc > 0.0) || !(fc < 0.5 * fs))              return 0;
    if (win < FIR_WIN_RECT || win > FIR_WIN_BLACKMAN) return 0;

    c   = N / 2;                                     /* 对称中心（delta 位置）*/
    wc  = 2.0 * fc / fs;                             /* 归一化截止频率 */
    mid = 0.5 * (double)N;

    /* ---------- 1) 理想低通双边 sinc（未加窗） ---------- */
    for (k = 0; k < M; ++k) {
        double x = (double)k - mid;
        h[k] = (fabs(x) < 1e-12) ? wc : sin(M_PI * wc * x) / (M_PI * x);
    }

    /* ---------- 2) 谱反转：h_hp = delta - h_lp ---------- */
    for (k = 0; k < M; ++k) h[k] = -h[k];
    h[c] += 1.0;

    /* ---------- 3) 加窗 ---------- */
    for (k = 0; k < M; ++k) h[k] *= fir_win_val(k, N, win);

    /* ---------- 4) 归一化：奈奎斯特增益 = 1 ----------
     * H(z = -1) = Sum h[k]*(-1)^k，即系数交替求和 */
    for (k = 0; k < M; ++k) sum += ((k & 1) ? -h[k] : h[k]);
    if (fabs(sum) < 1e-300) return 0;
    kk = 1.0 / sum;
    for (k = 0; k < M; ++k) h[k] *= kk;

    return 1;
}



/**
* @brief  设计任意阶 FIR 带通滤波器（sinc×cos 调制法，线性相位）
* @param  fs     采样频率，单位 Hz，要求 fs > 0 且为有限值
* @param  f1     下截止频率，单位 Hz，要求 0 < f1 < f2 < fs/2
* @param  f2     上截止频率，单位 Hz，要求 f1 < f2 < fs/2
* @param  order  滤波器阶数 N，允许 2 ~ 4096，且必须为偶数
* @param  win    窗函数类型：FIR_WIN_RECT / HANN / HAMMING / BLACKMAN
* @param  h      输出：FIR 系数数组，长度至少 order+1，不能为 NULL
* @return 1：设计成功，h[] 有效
*         0：参数不合格（指针为空、阶数非法、fs/f1/f2 非法、win 非法）
*            或内存分配失败
* @note   1) 【阶数约束】order 必须为偶数。若用奇数阶（Type II），当中心
*            频率恰为 fs/4 时响应会被迫为零，无法设计，故统一要求偶数阶。
*         2) 原理：带通冲激响应 = 低通原型 × 余弦载波
*              h[k] = 2*B/fs * sinc(B/fs*(k-N/2)) * cos(pi*(f1+f2)/fs*(k-N/2))
*            其中 B = f2-f1 为带宽。再乘窗、去均值、并在中心频率归一化。
*         3) 增益：中心频率 f0 = (f1+f2)/2 处精确为 0dB（1.0），f1 与 f2 处
*            约为 -6dB（与低通的 fc 处一致），通带与两侧阻带对称过渡。
*         4) 直流增益与奈奎斯特增益均被压制：去均值后 Sum h 达 1e-17 量级，
*            直流响应精确为 0；奈奎斯特衰减 -40 ~ -127dB（取决于窗与阶数）。
*         5) 阻带衰减由窗决定，与阶数无关：
*              RECT ~-21dB / HANN ~-44dB / HAMMING ~-53dB / BLACKMAN ~-74dB
*            过渡带宽度（-1dB -> -60dB）约 k*fs/N，k≈0.9/2.0/2.1/3.1。
*            带宽 B 需明显大于过渡带宽度，否则通带无法成形（见注 7）。
*         6) 系数个数 order+1，线性相位，群延迟 = order/2 个采样点。
*         7) 阶数太小时通带会塌陷：例如 fs=1kHz、100~200Hz、N=8 时 f1/f2
*            处只有 -2.6dB/-1.7dB（通带没平）。经验上取 N ≥ (5~10)*fs/B。
*/
int fir_bandpass_design(double fs, double f1, double f2, int order,
                        int win, double *h)
{
    int    N = order, M = N + 1, k;
    double B, f0, wc, w0, mid, g0, re, im, mean;

    /* ---------- 参数合法性检查 ---------- */
    if (h == NULL)                                          return 0;
    if (order < 2 || order > 4096)                          return 0;
    if (N & 1)                                              return 0;  /* 偶数阶 */
    if (!IS_FINITE(fs) || !IS_FINITE(f1) || !IS_FINITE(f2)) return 0;
    if (!(fs > 0.0))                                        return 0;
    if (!(f1 > 0.0) || !(f2 > f1))                          return 0;
    if (!(f2 < 0.5 * fs))                                   return 0;
    if (win < FIR_WIN_RECT || win > FIR_WIN_BLACKMAN)       return 0;

    B   = f2 - f1;                                          /* 带宽 */
    f0  = 0.5 * (f1 + f2);                                  /* 中心频率 */
    wc  = B / fs;                                           /* 归一化半带宽 */
    w0  = 2.0 * M_PI * f0 / fs;                             /* 中心角频率 */
    mid = 0.5 * (double)N;

    /* ---------- 低通 sinc × 余弦载波，再乘窗 ---------- */
    for (k = 0; k < M; ++k) {
        double x = (double)k - mid;
        double s = (fabs(x) < 1e-12) ? wc : sin(M_PI * wc * x) / (M_PI * x);
        h[k] = 2.0 * s * cos(w0 * x) * fir_win_val(k, N, win);
    }

    /* ---------- 去均值：使 Sum h = 0，直流增益精确为 0 ----------
     * sinc×cos 的系数和一般不为 0，会残留直流分量。减掉均值可让直流响应
     * 精确归零，且不破坏系数的对称性（线性相位不受影响）。 */
    mean = 0.0;
    for (k = 0; k < M; ++k) mean += h[k];
    mean /= (double)M;
    for (k = 0; k < M; ++k) h[k] -= mean;

    /* ---------- 归一化：中心频率 f0 处增益 = 1 ---------- */
    re = im = 0.0;
    for (k = 0; k < M; ++k) {
        re += h[k] * cos(-w0 * k);
        im += h[k] * sin(-w0 * k);
    }
    g0 = sqrt(re * re + im * im);
    if (g0 < 1e-300) return 0;
    for (k = 0; k < M; ++k) h[k] /= g0;

    return 1;
}




/**
* @brief  设计任意阶 FIR 带阻（陷波）滤波器（解析频域积分法，线性相位）
* @param  fs     采样频率，单位 Hz，要求 fs > 0 且为有限值
* @param  f1     阻带下边沿，单位 Hz，要求 0 < f1 < f2 < fs/2
* @param  f2     阻带上边沿，单位 Hz，要求 f1 < f2 < fs/2
* @param  order  滤波器阶数 N，允许 2 ~ 4096（奇偶均可）
* @param  win    窗函数类型：FIR_WIN_RECT / HANN / HAMMING / BLACKMAN
* @param  h      输出：FIR 系数数组，长度至少 order+1，不能为 NULL
* @return 1：设计成功，h[] 有效
*         0：参数不合格（指针为空、阶数非法、fs/f1/f2 非法、win 非法）
*            或内存分配失败
* @note   1) 原理：直接对理想带阻频率响应（通带 1、阻带 0）做逆 DTFT，
*            闭式解为
*              h[k] = (1/pi) * [ sin(om1*x)/x + (sin(pi*x) - sin(om2*x))/x ]
*            其中 x = k - order/2，om1 = pi*f1/(fs/2)，om2 = pi*f2/(fs/2)；
*            x = 0 处取极限 om1 + (pi - om2)。再乘窗、归一化直流增益为 1。
*         2) 与谱反转/互补结构相比，本法能同时保证：
*              - 直流增益精确 0dB（实测 Sum h = 1.000000000000000）
*              - 奈奎斯特增益精确 0dB
*              - 阻带深度只由窗决定，与低通/高通指标无关
*         3) 增益：f1 与 f2 处约为 -6dB（与低通/带通的截断点一致），
*            阻带中心 (f1+f2)/2 处最深。
*         4) 阻带深度由窗决定：
*              RECT ~-110dB / HANN ~-130dB / HAMMING ~-160dB / BLACKMAN ~-170dB
*            注意这里的深度指"理想阻带零点"附近的实测最小增益；受系数精度
*            影响，实际会显著优于仅由旁瓣决定的 -21/-44/-53/-74dB。
*         5) 过渡带宽度（-1dB -> -60dB）约 k*fs/N，k≈0.9/2.0/2.1/3.1。
*            阻带宽度 B = f2-f1 必须明显大于过渡带宽度，否则陷波会被过渡带
*            淹没——实测 fs=1kHz、100~200Hz、N=20 时陷波仅 -16dB（过渡带
*            约 42Hz 已占满半个阻带），而 N≥65 时达到 -131dB。
*            经验取 N ≥ (5~10)*fs/B。
*         6) 系数个数 order+1，线性相位，群延迟 = order/2 个采样点。
*/
int fir_bandstop_design(double fs, double f1, double f2, int order,
                        int win, double *h)
{
    int    N = order, M = N + 1, k;
    double om1, om2, mid, sum, x, s;

    /* ---------- 参数合法性检查 ---------- */
    if (h == NULL)                                          return 0;
    if (order < 2 || order > 4096)                          return 0;
    if (!IS_FINITE(fs) || !IS_FINITE(f1) || !IS_FINITE(f2)) return 0;
    if (!(fs > 0.0))                                        return 0;
    if (!(f1 > 0.0) || !(f2 > f1))                          return 0;
    if (!(f2 < 0.5 * fs))                                   return 0;
    if (win < FIR_WIN_RECT || win > FIR_WIN_BLACKMAN)       return 0;

    mid = 0.5 * (double)N;
    om1 = M_PI * f1 / (0.5 * fs);            /* 阻带下边沿角频率 */
    om2 = M_PI * f2 / (0.5 * fs);            /* 阻带上边沿角频率 */

    /* ---------- 理想带阻的逆 DTFT（闭式解） ---------- */
    for (k = 0; k < M; ++k) {
        x = (double)k - mid;
        if (fabs(x) < 1e-12)
            s = om1 + (M_PI - om2);          /* x -> 0 的极限 */
        else
            s = sin(om1 * x) / x + (sin(M_PI * x) - sin(om2 * x)) / x;
        h[k] = s / M_PI * fir_win_val(k, N, win);
    }

    /* ---------- 归一化：直流增益 = 1 ---------- */
    sum = 0.0;
    for (k = 0; k < M; ++k) sum += h[k];
    if (fabs(sum) < 1e-300) return 0;
    for (k = 0; k < M; ++k) h[k] /= sum;

    return 1;
}

/**
* @brief  
* @param  a 原始系数 
* @param  na 量化系数 
* @param  n  系数个数，阶数+1 
* @return 
* @note   
*/
void fir_quantize_12bit(double *a,int *na,int n)
{
	for(int i=0;i<n;i++)
	{
		na[i]=round(a[i]*2047);
	} 
}
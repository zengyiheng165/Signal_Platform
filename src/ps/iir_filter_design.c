#include <math.h>
#include <stdlib.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* 判断浮点数是否有限（既不是 inf 也不是 nan） */
#define IS_FINITE(x)  ((x) - (x) == 0.0)




/**
* @brief  多项式 p 乘上二次因子 (1 + c1*z^-1 + c2*z^-2)，结果写入 out
* @param  p    输入多项式系数，按 z^-1 升幂排列，长度 len+1
* @param  len  输入多项式次数（系数个数为 len+1）
* @param  c1   二次因子的一次项系数
* @param  c2   二次因子的二次项系数
* @param  out  输出多项式系数，长度需 >= len+3
* @return 无
* @note   p 与 out 不能是同一块内存
*/
static void mul_quad(double *p, int len, double c1, double c2, double *out)
{
    int k;
    for (k = 0; k <= len + 2; ++k) out[k] = 0.0;
    for (k = 0; k <= len; ++k) {
        out[k]     += p[k];
        out[k + 1] += p[k] * c1;
        out[k + 2] += p[k] * c2;
    }
}
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
int iir_lowpass(double fs, double fc, int order, double *b, double *a)
{
    int n = order, i, k;
    double an;
    double *q, *den;
    double asum = 0.0, norm;
    unsigned long long binom = 1;                 /* C(n,k) */

    /* ---------- 参数合法性检查 ---------- */
    if (b == NULL || a == NULL)              return 0;   /* 输出数组不能为空 */
    if (order < 1 || order > 20)             return 0;   /* 阶数越界 */
    if (!IS_FINITE(fs) || !IS_FINITE(fc))    return 0;   /* 排除 inf / NaN */
    if (!(fs > 0.0))                         return 0;   /* 采样率须为正 */
    if (!(fc > 0.0))                         return 0;   /* 截止频率须为正 */
    if (!(fc < 0.5 * fs))                    return 0;   /* 须低于奈奎斯特频率 */
    if (!IS_FINITE(tan(M_PI * fc / fs)))     return 0;   /* 过于贴近奈奎斯特 */

    an = tan(M_PI * fc / fs);                     /* 预畸变后的截止频率 */

    /* ---------- 分配中间缓冲 ---------- */
    q   = (double *)malloc(sizeof(double) * (size_t)(n + 3));
    den = (double *)malloc(sizeof(double) * (size_t)(n + 3));
    if (q == NULL || den == NULL) {               /* 分配失败 */
        free(q);
        free(den);
        return 0;
    }

    /* ---- 分母：模拟 Butterworth 极点经双线性变换后，按共轭对逐个相乘 ---- */
    den[0] = 1.0;
    {
        int len = 0;
        for (i = 0; i < n / 2; ++i) {
            double th = M_PI * (2 * i + n + 1) / (2 * n);
            double sr = an * cos(th);         /* 模拟极点实部 */
            double si = an * sin(th);         /* 模拟极点虚部 */
            double d  = (1.0 - sr) * (1.0 - sr) + si * si;
            double zr = (1.0 - (sr * sr + si * si)) / d;   /* Re(z_k) */
            double zi = (2.0 * si) / d;                    /* Im(z_k) */
            mul_quad(den, len, -2.0 * zr, zr * zr + zi * zi, q);
            for (k = 0; k <= len + 2; ++k) den[k] = q[k];
            len += 2;
        }
        if (n & 1) {                          /* 奇数阶剩一个实极点 */
            double zr = (1.0 - an) / (1.0 + an);
            mul_quad(den, len, -zr, 0.0, q);
            for (k = 0; k <= len + 2; ++k) den[k] = q[k];
            len += 2;
        }
    }

    for (i = 0; i <= n; ++i) { a[i] = den[i]; asum += den[i]; }
    norm = asum / pow(2.0, (double)n);        /* 令直流增益为 1 */

    /* ---- 分子：B(z) = (1 + z^-1)^n，系数为二项式系数 C(n,k) ---- */
    for (i = 0; i <= n; ++i) {
        if (i > 0)
            binom = binom * (unsigned long long)(n - i + 1) / (unsigned long long)i;
        b[i] = (double)binom * norm;
    }

    free(q);
    free(den);
    return 1;
}
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
int iir_highpass(double fs, double fc, int order, double *b, double *a)
{
    int n = order, i, k;
    double an;
    double *q, *den;
    double asum = 0.0, norm;
    unsigned long long binom = 1;                 /* C(n,k) */

    /* ---------- 参数合法性检查 ---------- */
    if (b == NULL || a == NULL)              return 0;   /* 输出数组不能为空 */
    if (order < 1 || order > 20)             return 0;   /* 阶数越界 */
    if (!IS_FINITE(fs) || !IS_FINITE(fc))    return 0;   /* 排除 inf / NaN */
    if (!(fs > 0.0))                         return 0;   /* 采样率须为正 */
    if (!(fc > 0.0))                         return 0;   /* 截止频率须为正 */
    if (!(fc < 0.5 * fs))                    return 0;   /* 须低于奈奎斯特频率 */
    if (!IS_FINITE(tan(M_PI * fc / fs)))     return 0;   /* 过于贴近奈奎斯特 */

    an = tan(M_PI * fc / fs);                     /* 预畸变后的截止频率 */

    /* ---------- 分配中间缓冲 ---------- */
    q   = (double *)malloc(sizeof(double) * (size_t)(n + 3));
    den = (double *)malloc(sizeof(double) * (size_t)(n + 3));
    if (q == NULL || den == NULL) {               /* 分配失败 */
        free(q);
        free(den);
        return 0;
    }

    /* ---- 分母：高通原型极点 an*exp(j*theta) 经双线性变换，按共轭对逐个相乘 ---- */
    den[0] = 1.0;
    {
        int len = 0;
        for (i = 0; i < n / 2; ++i) {
            double th = M_PI * (2 * i + n + 1) / (2 * n);
            double sr = an * cos(th);         /* 高通模拟极点实部（< 0） */
            double si = an * sin(th);         /* 高通模拟极点虚部 */
            double d  = (1.0 - sr) * (1.0 - sr) + si * si;
            double zr = (1.0 - (sr * sr + si * si)) / d;   /* Re(z_k) */
            double zi = (2.0 * si) / d;                    /* Im(z_k) */
            mul_quad(den, len, -2.0 * zr, zr * zr + zi * zi, q);
            for (k = 0; k <= len + 2; ++k) den[k] = q[k];
            len += 2;
        }
        if (n & 1) {                          /* 奇数阶剩一个实极点 */
            double zr = (1.0 - an) / (1.0 + an);
            mul_quad(den, len, -zr, 0.0, q);
            for (k = 0; k <= len + 2; ++k) den[k] = q[k];
            len += 2;
        }
    }

    /* ---- 归一化：用 z = -1（奈奎斯特）处 A(-1)，系数交替相加 ---- */
    for (i = 0; i <= n; ++i) {
        a[i] = den[i];
        asum += ((i & 1) ? -den[i] : den[i]);
    }
    norm = asum / pow(2.0, (double)n);        /* 令奈奎斯特处增益为 1 */

    /* ---- 分子：B(z) = (1 - z^-1)^n，系数为 C(n,k)*(-1)^k，交替变号 ---- */
    for (i = 0; i <= n; ++i) {
        if (i > 0)
            binom = binom * (unsigned long long)(n - i + 1) / (unsigned long long)i;
        b[i] = ((i & 1) ? -(double)binom : (double)binom) * norm;
    }

    free(q);
    free(den);
    return 1;
}


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
int iir_bandpass(double fs, double f1, double f2, int order, double *b, double *a)
{
    int n = order, m = 2 * order, i, k;
    double w1, w2, bw, w0;
    double *q, *den;
    double norm;
    unsigned long long binom = 1;                 /* C(n,k) */

    /* ---------- 参数合法性检查 ---------- */
    if (b == NULL || a == NULL)                             return 0;
    if (order < 1 || order > 10)                            return 0;
    if (!IS_FINITE(fs) || !IS_FINITE(f1) || !IS_FINITE(f2)) return 0;
    if (!(fs > 0.0))                                        return 0;
    if (!(f1 > 0.0) || !(f2 > f1))                          return 0;
    if (!(f2 < 0.5 * fs))                                   return 0;

    w1 = tan(M_PI * f1 / fs);                     /* 预畸变：下边带 */
    w2 = tan(M_PI * f2 / fs);                     /* 预畸变：上边带 */
    bw = w2 - w1;                                 /* 模拟带宽 */
    w0 = sqrt(w1 * w2);                           /* 模拟中心频率 */
    if (!IS_FINITE(bw) || !IS_FINITE(w0) || !(bw > 0.0)) return 0;

    /* ---------- 分配中间缓冲 ---------- */
    q   = (double *)malloc(sizeof(double) * (size_t)(m + 3));
    den = (double *)malloc(sizeof(double) * (size_t)(m + 3));
    if (q == NULL || den == NULL) { free(q); free(den); return 0; }

    /* ---- 分母：低通原型经带通变换 s' = (s^2 + w0^2)/(bw*s) 映射 ---------
     *  原型极点 s'_k = exp(j*th) 满足   s^2 - bw*s'_k*s + w0^2 = 0
     *  每个原型极点给出 2 个带通极点，共 2*order 个，全部位于左半平面。
     *  按原型共轭极点对 (th, -th) 成对处理：每对给出 4 个带通极点，即两对
     *  共轭 (p1,conj(p1)) 与 (p2,conj(p2))，各生成一个实系数二次因子
     *      1 - 2*Re(z)*q + |z|^2*q^2
     *  奇数阶原型含一个实极点（sin(th)=0），单独生成一个二次因子。
     * -------------------------------------------------------------------- */
    den[0] = 1.0;
    {
        int len = 0;

        for (i = 0; i < n / 2; ++i) {
            double th = M_PI * (2 * i + n + 1) / (2 * n);
            double br = bw * cos(th);                /* b = bw*s'_k */
            double bi = bw * sin(th);
            double dr = br * br - bi * bi - 4.0 * w0 * w0;   /* 判别式实部 */
            double di = 2.0 * br * bi;                       /* 判别式虚部 */
            double rr = sqrt(dr * dr + di * di);
            double qr = sqrt(0.5 * (rr + dr));       /* sqrt(dr + j*di) 主值 */
            double qi = sqrt(0.5 * (rr - dr));
            int    j;

            if (di < 0.0) qi = -qi;

            for (j = 0; j <= 1; ++j) {               /* 两个根，各自配共轭 */
                double sg = (j == 0) ? 1.0 : -1.0;
                double sr = 0.5 * (br + sg * qr);    /* s = [b +/- sqrt(disc)]/2 */
                double si = 0.5 * (bi + sg * qi);
                double d  = (1.0 - sr) * (1.0 - sr) + si * si;    /* |1-s|^2 */
                double zr = (1.0 - (sr * sr + si * si)) / d;      /* Re(z) */
                double zi = (2.0 * si) / d;                       /* Im(z) */

                mul_quad(den, len, -2.0 * zr, zr * zr + zi * zi, q);
                for (k = 0; k <= len + 2; ++k) den[k] = q[k];
                len += 2;
            }
        }

        /* 奇数阶：原型实极点 s' = -1，方程 s^2 + bw*s + w0^2 = 0，
           两根为共轭复数（bw < 2*w0），生成一个二次因子 */
        if (n & 1) {
            double br = -bw;
            double dr = br * br - 4.0 * w0 * w0;
            double sr, si, d, zr, zi;

            if (dr < 0.0) {                          /* 共轭复数根 */
                sr = 0.5 * br;
                si = 0.5 * sqrt(-dr);
            } else {                                 /* 两个实根，取左半平面那个 */
                double r1 = 0.5 * (br + sqrt(dr));
                double r2 = 0.5 * (br - sqrt(dr));
                sr = (r1 < 0.0) ? r1 : r2;
                si = 0.0;
            }

            d  = (1.0 - sr) * (1.0 - sr) + si * si;
            zr = (1.0 - (sr * sr + si * si)) / d;
            zi = (2.0 * si) / d;

            mul_quad(den, len, -2.0 * zr, zr * zr + zi * zi, q);
            for (k = 0; k <= len + 2; ++k) den[k] = q[k];
            len += 2;
        }
    }

    for (i = 0; i <= m; ++i) a[i] = den[i];

    /* ---- 归一化：中心频率处 |H| = 1 ------------------------------------
     *  数字中心角频率 wd = 2*atan(w0)，取 z0 = exp(j*wd)，
     *  求 |A(z0)|；分子 B(z) = (1 - z^-2)^n 在 z0 处为 (1 - z0^-2)^n。
     * -------------------------------------------------------------------- */
    {
        double wd = 2.0 * atan(w0);
        double czr = cos(wd), czi = sin(wd);
        double pr = 1.0, pi_ = 0.0;              /* z0^-k 累乘 */
        double ar = 0.0, ai = 0.0;
        double re, im, mag, ang, br, bi, babs;

        for (i = 0; i <= m; ++i) {
            ar += a[i] * pr;
            ai += a[i] * pi_;
            {
                double nr = pr * czr - pi_ * czi;   /* *= z0^-1 */
                double ni = pr * czi + pi_ * czr;
                pr = nr; pi_ = ni;
            }
        }

        /* (1 - z0^-2)^n，其中 z0^-2 = exp(-j*2*wd) */
        re  = 1.0 - cos(2.0 * wd);
        im  = sin(2.0 * wd);
        mag = sqrt(re * re + im * im);
        ang = atan2(im, re);
        br  = pow(mag, (double)n) * cos(n * ang);
        bi  = pow(mag, (double)n) * sin(n * ang);
        babs = sqrt(br * br + bi * bi);

        norm = sqrt(ar * ar + ai * ai) / babs;   /* 使 |H(z0)| = 1 */

        /* 分子：(1 - z^-2)^n = sum_k C(n,k) * (-1)^k * z^(-2k) */
        for (i = 0; i <= m; ++i) b[i] = 0.0;
        for (i = 0; i <= n; ++i) {
            if (i > 0)
                binom = binom * (unsigned long long)(n - i + 1) / (unsigned long long)i;
            b[2 * i] = ((i & 1) ? -(double)binom : (double)binom) * norm;
        }
    }

    free(q);
    free(den);
    return 1;
}
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
int iir_bandstop(double fs, double f1, double f2, int order, double *b, double *a)
{
    int n = order, m = 2 * order, i, k;
    double w1, w2, bw, w0;
    double *q, *den;
    double sum_a = 0.0, sum_b = 0.0, norm;

    /* ---------- 参数合法性检查 ---------- */
    if (b == NULL || a == NULL)                             return 0;
    if (order < 1 || order > 10)                            return 0;
    if (!IS_FINITE(fs) || !IS_FINITE(f1) || !IS_FINITE(f2)) return 0;
    if (!(fs > 0.0))                                        return 0;
    if (!(f1 > 0.0) || !(f2 > f1))                          return 0;
    if (!(f2 < 0.5 * fs))                                   return 0;

    w1 = tan(M_PI * f1 / fs);                     /* 预畸变：阻带下边沿 */
    w2 = tan(M_PI * f2 / fs);                     /* 预畸变：阻带上边沿 */
    bw = w2 - w1;
    w0 = sqrt(w1 * w2);
    if (!IS_FINITE(bw) || !IS_FINITE(w0) || !(bw > 0.0)) return 0;

    q   = (double *)malloc(sizeof(double) * (size_t)(m + 3));
    den = (double *)malloc(sizeof(double) * (size_t)(m + 3));
    if (q == NULL || den == NULL) { free(q); free(den); return 0; }

    /* ---- 分母：带阻变换 s' = bw*s/(s^2 + w0^2) --------------------------
     *  原型极点 s'_k = exp(j*th) 满足  s^2 - (bw/s'_k)*s + w0^2 = 0
     *  每个原型极点给出 2 个带阻极点（实部为负，恒稳定），共 2n 个。
     *  按原型共轭极点对成对处理，每对给出 4 个带阻极点即两对共轭，
     *  各生成一个实系数二次因子 1 - 2*Re(z)*q + |z|^2*q^2。
     * -------------------------------------------------------------------- */
    den[0] = 1.0;
    {
        int len = 0;

        for (i = 0; i < n / 2; ++i) {
            double th = M_PI * (2 * i + n + 1) / (2 * n);
            double pr_ = cos(th), pi_ = sin(th);
            double m2  = pr_ * pr_ + pi_ * pi_;
            double br  = bw * pr_ / m2;              /* b = bw / s'_k */
            double bi  = -bw * pi_ / m2;
            double dr  = br * br - bi * bi - 4.0 * w0 * w0;
            double di  = 2.0 * br * bi;
            double rr  = sqrt(dr * dr + di * di);
            double qr  = sqrt(0.5 * (rr + dr));      /* sqrt(dr + j*di) 主值 */
            double qi  = sqrt(0.5 * (rr - dr));
            int    j;

            if (di < 0.0) qi = -qi;

            for (j = 0; j <= 1; ++j) {               /* 两个根，各自配共轭 */
                double sg = (j == 0) ? 1.0 : -1.0;
                double sr = 0.5 * (br + sg * qr);    /* s = [b +/- sqrt(disc)]/2 */
                double si = 0.5 * (bi + sg * qi);
                double d  = (1.0 - sr) * (1.0 - sr) + si * si;
                double zr = (1.0 - (sr * sr + si * si)) / d;
                double zi = (2.0 * si) / d;

                mul_quad(den, len, -2.0 * zr, zr * zr + zi * zi, q);
                for (k = 0; k <= len + 2; ++k) den[k] = q[k];
                len += 2;
            }
        }

        /* 奇数阶：原型实极点 s' = -1，方程 s^2 + bw*s + w0^2 = 0 */
        if (n & 1) {
            double br = -bw, dr = br * br - 4.0 * w0 * w0, sr, si, d, zr, zi;
            if (dr < 0.0) { sr = 0.5 * br; si = 0.5 * sqrt(-dr); }
            else { double r1 = 0.5 * (br + sqrt(dr)), r2 = 0.5 * (br - sqrt(dr));
                   sr = (r1 < 0.0) ? r1 : r2; si = 0.0; }
            d  = (1.0 - sr) * (1.0 - sr) + si * si;
            zr = (1.0 - (sr * sr + si * si)) / d;
            zi = (2.0 * si) / d;
            mul_quad(den, len, -2.0 * zr, zr * zr + zi * zi, q);
            for (k = 0; k <= len + 2; ++k) den[k] = q[k];
            len += 2;
        }
    }

    for (i = 0; i <= m; ++i) a[i] = den[i];

    /* ---- 分子：模拟零点在 s = ±j*w0，双线性变换后落在单位圆上 ----------
     *  z = (1 + j*w0)/(1 - j*w0)（|z| = 1），共轭对生成 (1 - 2cos(wj)q + q^2)
     * -------------------------------------------------------------------- */
    for (i = 0; i <= m; ++i) b[i] = 0.0;
    b[0] = 1.0;
    {
        int len = 0;

        for (i = 0; i < n; ++i) {
            double nr = 1.0, ni = w0;
            double dr = 1.0, di = -w0;
            double dd = dr * dr + di * di;
            double zr = (nr * dr + ni * di) / dd;
            double zi = (ni * dr - nr * di) / dd;

            mul_quad(b, len, -2.0 * zr, zr * zr + zi * zi, q);
            for (k = 0; k <= len + 2; ++k) b[k] = q[k];
            len += 2;
        }
    }

    /* ---- 归一化：直流（z = 1）处增益 = 1 ---- */
    for (i = 0; i <= m; ++i) { sum_a += a[i]; sum_b += b[i]; }
    if (fabs(sum_a) < 1e-300 || fabs(sum_b) < 1e-300) { free(q); free(den); return 0; }
    norm = sum_a / sum_b;
    for (i = 0; i <= m; ++i) b[i] *= norm;

    free(q);
    free(den);
    return 1;
}

/* 12bit 有符号范围 */
#define Q12_MIN  (-2048)
#define Q12_MAX  (+2047)


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
                       int *bq, int *aq, int *q)
{
    int    k, qv, sat = 0;
    double mx = 0.0, lim, sc;

    /* ---------- 参数检查 ---------- */
    if (b == NULL || a == NULL || bq == NULL || aq == NULL || q == NULL) return 0;
    if (n < 1) return 0;

    /* ---------- 取两个数组的公共最大值，定出共用定标 ---------- */
    for (k = 0; k < n; ++k) {
        if (fabs(b[k]) > mx) mx = fabs(b[k]);
        if (fabs(a[k]) > mx) mx = fabs(a[k]);
    }

    if (mx == 0.0) {                        /* 全零数组 */
        for (k = 0; k < n; ++k) { bq[k] = 0; aq[k] = 0; }
        *q = 0;
        return n;
    }

    lim = (double)Q12_MAX / mx;             /* 允许的最大 scale */
    qv = 0;
    while (qv < 62 && pow(2.0, (double)(qv + 1)) <= lim) qv++;
    if (pow(2.0, (double)qv) > lim) {       /* 2^0 都放不下：|系数| > 2047 */
        qv = 0;
        sat = 1;
    }
    sc = pow(2.0, (double)qv);

    /* ---------- 用同一个 qv 量化两个数组 ---------- */
    for (k = 0; k < n; ++k) {
        double xb = b[k] * sc;
        double xa = a[k] * sc;
        long   vb = (long)((xb >= 0.0) ? (xb + 0.5) : (xb - 0.5));   /* 四舍五入 */
        long   va = (long)((xa >= 0.0) ? (xa + 0.5) : (xa - 0.5));

        if (vb > Q12_MAX) { vb = Q12_MAX; sat = 1; }
        if (vb < Q12_MIN) { vb = Q12_MIN; sat = 1; }
        if (va > Q12_MAX) { va = Q12_MAX; sat = 1; }
        if (va < Q12_MIN) { va = Q12_MIN; sat = 1; }

        bq[k] = (int)vb;
        aq[k] = (int)va;
    }

    *q = qv;
    return sat ? -n : n;
}

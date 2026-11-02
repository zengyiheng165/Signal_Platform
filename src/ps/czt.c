#include "czt.h"
#include <math.h>
#include <stdint.h>
#include <xil_assert.h>
#include <xil_cache.h>


#define CZT_MAX_L 16384
#define PLFFT_MAX_LEN 16384

/* ---- FFT IP 定标系数 ------------------------------------------------------。 */
#define CZT_FFT_KIN_G   1.0        /* 去斜数据 g 的输入定标（ADC 幅度 ±2048） */
#define CZT_FFT_KOUT_G  1.0        /* g 的输出定标，与 KIN_G 互为倒数       */
#define CZT_FFT_KIN_H   16384.0    /* chirp 序列 h 的输入定标（幅度恒为 1）  */
#define CZT_FFT_KOUT_H  (1.0 / CZT_FFT_KIN_H)   /* h 的输出定标          */




static czt_complex_t s_czt_g[CZT_MAX_L];   /* g 序列 -> G -> G.*H */
static czt_complex_t s_czt_h[CZT_MAX_L];   /* h 序列 -> H         */
static czt_complex_t s_czt_y[CZT_MAX_L];   /* 卷积结果 y = ifft(G.*H) */

/* FFT IP 的输入/输出缓冲：
 *   TX 32bit/点（bit15-0 实部, bit31-16 虚部），打包结果写在这里，与源数据完全分离；
 *   RX 64bit/点（bit31-0 实部, bit63-32 虚部），IP 的输出。
 * 两块都独立分配，不再复用源数组的内存，逻辑上"打包 -> 送 IP -> 解包"三段互不干扰。 */
static u32  s_plfft_tx[PLFFT_MAX_LEN];
static u64  s_plfft_rx[PLFFT_MAX_LEN];


/**
 * @brief  求指数函数
 * @param  x exp(j*x)
 * @return 复数 exp(j*2*pi*t)，模恒为 1
 */
static czt_complex_t exp_complex(double x)
{
    czt_complex_t r;
    r.re = cos(x);
    r.im = sin(x);
    return r;
}

/**
 * @brief  复数乘法 c = a * b。
 * @param  a 被乘数
 * @param  b 乘数
 * @return a * b
 */
static czt_complex_t complex_mul(czt_complex_t a, czt_complex_t b)
{
    czt_complex_t r;
    r.re = a.re * b.re - a.im * b.im;
    r.im = a.re * b.im + a.im * b.re;
    return r;
}

/**
 * @brief  复数乘实数乘法 c = a * b。
 * @param  a 被乘数,实数
 * @param  b 乘数，复数
 * @return a * b
 */
static czt_complex_t complex_real_mul(double a, czt_complex_t b)
{
    czt_complex_t r;
    r.re = a* b.re ;
    r.im = a* b.im ;
    return r;
}

/**
 * @brief  复数取模 |z| = sqrt(re^2 + im^2)。
 * @param  z 输入复数
 * @return 复数幅度
 */
static double complex_abs(czt_complex_t z)
{
    return sqrt(z.re * z.re + z.im * z.im);
}

/**
 * @brief  求不小于 v 的最小 2 的整数次幂。
 * @param  v 任意正整数；v == 0 时返回 1
 * @return 2 的整数次幂
 * @note   用于确定 CZT 的 FFT 长度：L = czt_next_pow2(N + M - 1)，见 czt.m 第 42 行。
 */
int czt_next_pow2(int v)
{
    int p = 1;
    if (v == 0) return 1;
    while (p < v) p <<= 1;
    return p;
}
/**
 * @brief  把双精度复数序列打包成 FFT IP 要求的 32bit 输入字。
 * @param  dst 32bit/字的 FFT IP 输入缓冲，长度 >= len
 * @param  src 双精度复数数组
 * @param  len 点数
 * @param  k   定标系数：输出 = round(src*k)，按 int16 范围限幅
 * @return 无
 * @note   格式：bit15-0 = 实部(int16)，bit31-16 = 虚部(int16)，与 plfft.c 注释一致。
 *         限幅是必须的：一旦回绕，频谱会整体错乱。
 */
static void plfft_pack_iq16(u32 *dst, const czt_complex_t *src, int len, double k)
{
    for (int i = 0; i < len; i++) {
        double dre = src[i].re * k;
        double dim = src[i].im * k;
        int    ire, iim;

        ire = (dre >= 32767.0)  ?  32767 :
              (dre <= -32768.0) ? -32768 : (int)dre;
        iim = (dim >= 32767.0)  ?  32767 :
              (dim <= -32768.0) ? -32768 : (int)dim;

        dst[i] = ((u32)((u16)ire & 0xFFFFu)) | ((u32)((u16)iim & 0xFFFFu) << 16);
    }
}

/**
 * @brief  把 FFT IP 输出的 64bit 字解包回双精度复数序列。
 * @param  dst 目标双精度复数数组（可就是源复数序列本身，实现就地解包），长度 >= len
 * @param  src FFT IP 输出缓冲（8 字节/点）
 * @param  len 点数
 * @param  k   定标系数：输出 = src*k
 * @return 无
 * @note   
 */
static void plfft_unpack_iq_to(czt_complex_t *dst, u64 *src, int len, double k)
{
    for (int i = 0; i < len; i++) {
        int re = (int)((u32)(src[i] & 0xFFFFFFFFu));
        int im = (int)((u32)((src[i] >> 32) & 0xFFFFFFFFu));

        dst[i].re = (double)re * k;
        dst[i].im = (double)im * k;
    }
}

/**
 * @brief  在复数序列中求最大模值，用于自动选取打包定标系数。
 * @param  x   双精度复数数组
 * @param  len 点数
 * @return 最大模值；len <= 0 或 x == 0 时返回 0
 */
static double complex_max_abs(const czt_complex_t *x, int len)
{
    double m = 0.0;

    if (x == 0 || len <= 0) {
        return 0.0;
    }
    for (int i = 0; i < len; i++) {
        double a = fabs(x[i].re);
        double b = fabs(x[i].im);

        if (b > a) {
            a = b;              /* 用 |re|、|im| 的较大者近似模值，省掉开方，且更保守 */
        }
        if (a > m) {
            m = a;
        }
    }
    return m;
}

/**
 * @brief  计算 IFFT 输入打包所需的定标系数 k_in / k_out，保证不越界的条件下精度最优。
 * @param  x     待打包的双精度复数数组
 * @param  len   点数
 * @param  k_in  输出：输入定标（打包时乘）
 * @param  k_out 输出：输出定标（解包时乘）
 * @return 无
 */
static void plfft_ifft_scaling(const czt_complex_t *x, int len, double L,
                               double *k_in, double *k_out)
{
    const double kin_max = 65535.0;     /* 2^16-1：L*kin_max = 1.074e9 < 2^31，留一倍余量 */
    double maxabs = complex_max_abs(x, len);
    double kin;

    if (maxabs <= 0.0) {
        kin = 1.0;                      /* 频谱全零，任意合法值即可 */
    } else {
        kin = 32767.0 / maxabs;         /* 约束①：输入不越 int16 */
        if (kin > kin_max) {
            kin = kin_max;              /* 约束②：输出不越 31bit */
        }
    }

    *k_in  = kin;
    *k_out = (1.0 / L) / kin;           /* 与 k_in 合并正好完成 1/L 归一化 */
}

/**
 * @brief  L 点 FFT 正变换（PL 硬件加速版），就地运算。
 * @param  x     长度 L 的复数数组，输入时域序列，输出频域结果
 * @param  L     点数，必须是 2 的整数次幂，且 <= PLFFT_MAX_LEN
 * @param  k_in  输入定标：输入FFT ip核时乘的倍数
 * @param  k_out 输出定标：输出FFT ip核时乘的倍数
 * @param  AxiDman 选择硬件加速的 DMA 节点
 * @return 0 成功；-1 长度非法
 */
int czt_fft(czt_complex_t *x, int L, double k_in, double k_out, XAxiDma AxiDman)
{
    if (x == 0 || L < 2 || L > PLFFT_MAX_LEN) {
        return -1;
    }

    /* 打包到独立的 TX 缓冲：double 复数 -> 32bit 输入字 */
    plfft_pack_iq16(s_plfft_tx, x, L, k_in);

    /* 调用 PL 硬件加速 */
    fft_pl_accelerate((u8 *)s_plfft_tx, (u8 *)s_plfft_rx, L, AxiDman);

    /* RX 是"设备写内存、CPU 后读"，DMA 完成后必须无效化 cache，
       否则 CPU 可能读到 cache 里的旧数据（fft_pl_accelerate 内部对 RX 做的是 Flush，方向反了） */
    Xil_DCacheInvalidateRange((INTPTR)s_plfft_rx, (u32)L * 8u);   /* ★ 必须保留: RX 是设备写内存、CPU 后读, 只能 Invalidate(Flush 方向反了) */

    /* 解包回双精度复数，写回 x（就地得到结果，与 IFFT 侧统一用同一个函数） */
    plfft_unpack_iq_to(x, s_plfft_rx, L, k_out);

    return 0;
}

/**
 * @brief  L 点 FFT 反变换（PL 硬件加速版），就地运算，结果已含 1/L 归一化。
 * @param  x     长度 L 的复数数组，输入频域序列，输出时域结果
 * @param  L     点数，必须是 2 的整数次幂，且 <= PLFFT_MAX_LEN
 * @param  AxiDman 选择硬件加速的 DMA 节点
 * @return 0 成功；-1 长度非法
 */
int czt_ifft(czt_complex_t *x, int L, XAxiDma AxiDman)
{
    double k_in, k_out;

    if (x == 0 || L < 2 || L > PLFFT_MAX_LEN) {
        return -1;
    }

    /* 先按数据实际量级算出定标：k_in 尽量大，k_out 补齐 1/L */
    plfft_ifft_scaling(x, L, (double)L, &k_in, &k_out);

    /* 打包到独立的 TX 缓冲：double 复数 -> 32bit 输入字 */
    plfft_pack_iq16(s_plfft_tx, x, L, k_in);

    /* 调用 PL 硬件加速 */
   ifft_pl_accelerate((u8 *)s_plfft_tx, (u8 *)s_plfft_rx, L, AxiDman);

    /* 同 czt_fft：RX 是设备写内存，读完前必须无效化 cache */
    Xil_DCacheInvalidateRange((INTPTR)s_plfft_rx, (u32)L * 8u);   /* ★ 同上: 见 czt_fft 里的说明 */

    /* 解包并乘 k_out：其中包含 1/L 归一化，结果直接写入 s_czt_y */
    plfft_unpack_iq_to(s_czt_y, s_plfft_rx, L, k_out);

    return 0;
}



/**
* @brief  求CZT变换。
* @param  plan CZT变换参数，具体可以查看头文件。
* @param  xin 实数输入,长度为N
* @param  yout 幅度输出（缩放到真实幅度），长度为M
* @param  AxiDman 选择硬件加速的 DMA 节点
* @return 最大值所在的频率
* @note   使用该函数时，必须确保log2（plan.n+plan.m-1）的向上取整小于16384个点
*/
int czt(czt_plan_t plan, int16_t *xin , double*yout,XAxiDma AxiDman)
{
    if (xin == 0 || yout == 0 || plan.n <= 0 || plan.m < 2 || plan.fs <= 0.0) {
        return 0;
    }
    
    int    N = plan.n;
    int    M = plan.m;
    int    L;
    double df;
    double Ax,Wx;
    double scale;
     
    df = (plan.f_end - plan.f_start) / (double)(M - 1);  

    Ax = 2*PI* plan.f_start/plan.fs;
    Wx = -2*PI* df/plan.fs;
    scale = 2.0 / (double)N;  
    
    L = czt_next_pow2(N + M - 1);        
    if (L < 2 || L > CZT_MAX_L || N > CZT_MAX_L) {
        return 0;
    }   
/* ---------- 2. g[n] = x[n] * A^(-n) * W^(n^2/2)，然后补零到 L ----------
     *   A^(-n)     = exp(-j*2*PI*f_start*n/fs) = exp(j*Ax*(-n))
     *   W^(n^2/2)  = exp(-j*2*PI*df*(n^2/2)/fs) = exp(j*Wx*n^2/2)    */
     
    for(int i=0;i<N;i++)
    {
       s_czt_g[i]=complex_real_mul((double)xin[i],exp_complex(Ax*(-i)));
       s_czt_g[i]=complex_mul(s_czt_g[i],exp_complex(Wx*((double)i*i)/2));
    }
    for (int i = N; i < L; i++)
    {             
        s_czt_g[i].re = 0.0;
        s_czt_g[i].im = 0.0;
    }

        /* ---------- 3. h[p] = W^(-(p^2)/2) 的镜像序列，长度 L ----------
     *   p <  M-1 : h[p] = W^(-(p^2)/2)          = exp(j*Wx*p^2/2)
     *   p >= M-1 : h[p] = W^(-((L-p)^2)/2)      = exp(j*Wx*(L-p)^2/2)   */
    for (int p = 0; p < L; p++) {
        if (p < M - 1) {
            s_czt_h[p] = exp_complex(-Wx * ((double)p * p) / 2.0);
        } else {
            double q = (double)(L - p);
            s_czt_h[p] = exp_complex(-Wx * (q * q) / 2.0);
        }
    }


     /* ---------- 4. y = ifft(fft(g) .* fft(h)) ---------- */
    /* FFT接口：g 与 h 的取值范围不同，各自用自己的定标系数 */
    if (czt_fft(s_czt_g, L, CZT_FFT_KIN_G, CZT_FFT_KOUT_G, AxiDman) != 0) {
        return 0;                                    /* G = fft(g)，PL 硬件加速 */
    }
    if (czt_fft(s_czt_h, L, CZT_FFT_KIN_H, CZT_FFT_KOUT_H, AxiDman) != 0) {
        return 0;                                    /* H = fft(h)，PL 硬件加速 */
    }
    for (int i = 0; i < L; i++) {
        s_czt_g[i] = complex_mul(s_czt_g[i], s_czt_h[i]);
    }
    /* IFFT接口 */
    if (czt_ifft(s_czt_g, L, AxiDman) != 0) {
        return 0;                                    /* y = ifft(G .* H)，PL 硬件加速 */
    }


    /* ---------- 5. yout[k] = |W^(k^2/2) * y[k]| * scale，k = 0..M-1 ----------
     *   czt.m 第 65~66 行：X_czt = (W.^(k.^2/2)) .* y(1:M);  X_czt = X_czt/(N/2); */
    for (int i = 0; i < M; i++) {
        czt_complex_t w = exp_complex(Wx * ((double)i * i) / 2.0);
        yout[i] = complex_abs(complex_mul(w, s_czt_y[i])) * scale;
    }

    int max_fre=0;
    int max_pos=0;
    for(int i=0;i<M;i++)
    {
        if(yout[i]>=yout[max_pos])
        {
            max_pos=i;
        }
    }
    max_fre=round(plan.f_start+max_pos*df);
    return max_fre;
}

/* ---------------- Chirp-Z 啁啾预计算(供 czt_cached 使用) ----------------
   这三张表只与 plan 有关, 与输入数据无关; 频段/点数不变就一直有效。
   ★ s_czt_h 不能拿来存啁啾: czt_fft() 是就地变换, 会把里面的 h 算成 H。 */
static czt_complex_t s_cc_gn[CZT_MAX_L];   /* g 的调制因子 exp(j*Ax*(-i))*exp(j*Wx*i*i/2) */
static czt_complex_t s_cc_h[CZT_MAX_L];    /* h 啁啾(p<M-1 与 p>=M-1 两段) */
static czt_complex_t s_cc_wn[CZT_MAX_L];   /* 输出因子 exp(j*Wx*i*i/2) */
static czt_plan_t    s_cc_plan;            /* 缓存对应的参数 */
static int           s_cc_L = 0;           /* 缓存对应的变换长度 L */
static u8            s_cc_ok = 0u;         /* 1 = 缓存有效 */

/**
 * @brief  比较两份CZT参数是否完全一致(决定啁啾缓存能否复用)。
 * @param  a 第一份参数
 * @param  b 第二份参数
 * @return 1 一致；0 不一致
 */
static int cc_plan_same(const czt_plan_t *a, const czt_plan_t *b)
{
    if (a->n != b->n) { return 0; }
    if (a->m != b->m) { return 0; }
    if (a->fs != b->fs) { return 0; }
    if (a->f_start != b->f_start) { return 0; }
    if (a->f_end != b->f_end) { return 0; }
    return 1;
}

/**
* @brief  预计算CZT的三组旋转因子(啁啾)，只为 czt_cached() 服务。
* @param  plan CZT变换参数，含义与 czt() 完全相同。
* @return 0 成功；-1 参数非法(点数超过 CZT_MAX_L 等)。
* @note   只在"频段/点数改变"时调用一次；参数不变时 czt_cached() 会跳过重建。
*/
int czt_prepare(czt_plan_t plan)
{
    int N = plan.n;
    int M = plan.m;
    int L, i, p;
    double df, Ax, Wx;

    s_cc_ok = 0u;
    if ((N <= 0) || (M < 2) || (plan.fs <= 0.0)) {
        return -1;
    }
    L = czt_next_pow2(N + M - 1);
    if ((L < 2) || (L > CZT_MAX_L) || (N > CZT_MAX_L)) {
        return -1;
    }

    df = (plan.f_end - plan.f_start) / (double)(M - 1);
    Ax = 2 * PI * plan.f_start / plan.fs;
    Wx = -2 * PI * df / plan.fs;

    /* ① g 的调制因子(与 czt() 里分两步乘完全等价) */
    for (i = 0; i < N; i++) {
        s_cc_gn[i] = complex_mul(exp_complex(Ax * (-i)),
                                 exp_complex(Wx * ((double)i * i) / 2));
    }
    /* ② h 啁啾 */
    for (p = 0; p < L; p++) {
        if (p < (M - 1)) {
            s_cc_h[p] = exp_complex(-Wx * ((double)p * p) / 2.0);
        } else {
            double q = (double)(L - p);

            s_cc_h[p] = exp_complex(-Wx * (q * q) / 2.0);
        }
    }
    /* ③ 输出因子 */
    for (i = 0; i < M; i++) {
        s_cc_wn[i] = exp_complex(Wx * ((double)i * i) / 2.0);
    }

    s_cc_plan = plan;
    s_cc_L    = L;
    s_cc_ok   = 1u;
    return 0;
}

/**
* @brief  用预计算好的啁啾做CZT变换，数学上与 czt() 完全等价。
* @param  plan CZT变换参数；与上次不同时会自动调用 czt_prepare() 重建缓存。
* @param  xin 实数输入,长度为N
* @param  yout 幅度输出（缩放到真实幅度），长度为M
* @param  AxiDman 选择硬件加速的 DMA 节点（与 czt() 相同）
* @return 0 成功；-1 参数非法或 PL FFT 失败
* @note   h 啁啾来自缓存，而 czt_fft() 就地变换，所以先拷到 s_czt_h 再算。
*/
int czt_cached(czt_plan_t plan, int16_t *xin, double *yout, XAxiDma AxiDman)
{
    int N, M, L, i;
    double scale;

    if ((xin == 0) || (yout == 0) || (plan.n <= 0) || (plan.m < 2) || (plan.fs <= 0.0)) {
        return -1;
    }
    if ((s_cc_ok == 0u) || (cc_plan_same(&s_cc_plan, &plan) == 0)) {
        if (czt_prepare(plan) != 0) {
            return -1;
        }
    }
    N = plan.n;
    M = plan.m;
    L = s_cc_L;

    /* ---------- g[n] = x[n] * gn[n]，补零到 L ---------- */
    for (i = 0; i < N; i++) {
        s_czt_g[i] = complex_real_mul((double)xin[i], s_cc_gn[i]);
    }
    for (i = N; i < L; i++) {
        s_czt_g[i].re = 0.0;
        s_czt_g[i].im = 0.0;
    }

    /* ---------- G = fft(g)；H = fft(缓存里的 h) ---------- */
    if (czt_fft(s_czt_g, L, CZT_FFT_KIN_G, CZT_FFT_KOUT_G, AxiDman) != 0) {
        s_cc_ok = 0u;
        return -1;
    }
    for (i = 0; i < L; i++) {
        s_czt_h[i] = s_cc_h[i];                 /* ★ 保护缓存(czt_fft 就地覆盖) */
    }
    if (czt_fft(s_czt_h, L, CZT_FFT_KIN_H, CZT_FFT_KOUT_H, AxiDman) != 0) {
        s_cc_ok = 0u;
        return -1;
    }
    for (i = 0; i < L; i++) {
        s_czt_g[i] = complex_mul(s_czt_g[i], s_czt_h[i]);
    }

    /* ---------- y = ifft(G .* H) ---------- */
    if (czt_ifft(s_czt_g, L, AxiDman) != 0) {
        s_cc_ok = 0u;
        return -1;
    }

    /* ---------- yout[k] = |wn[k] * y[k]| * (2/N) ---------- */
    scale = 2.0 / (double)N;
    for (i = 0; i < M; i++) {
        yout[i] = complex_abs(complex_mul(s_cc_wn[i], s_czt_y[i])) * scale;
    }
    return 0;
}

/**
* @brief  取上一次 czt_prepare() 算出的变换长度 L。
* @return L = next_pow2(n+m-1)；0 表示还没有有效缓存。
* @note   供调用方核对"L 是否等于 PL 的 FFT 点数设置"。
*/
int czt_cached_L(void)
{
    return s_cc_L;
}

/**
 * @brief  在CZT幅度谱中查找最大值位置，用于估计信号频率。
 * @param  yout CZT幅度谱
 * @param  m    谱点数
 * @return 峰值序号（0 ~ m-1）；实际频率 = f_start + k*(f_end-f_start)/(m-1)
 */
int czt_max_index(double *yout, int m)
{
    int maxid = 0;

    if (yout == 0 || m <= 0) {
        return 0;
    }
    for (int i = 1; i < m; i++) {
        if (yout[i] > yout[maxid]) {
            maxid = i;
        }
    }
    return maxid;
}

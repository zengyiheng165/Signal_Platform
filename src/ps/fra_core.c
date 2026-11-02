/* fra_core.c - 见 fra_core.h */
#include "fra_core.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define FRA_PI  3.14159265358979323846

/* PS 端单点 DFT(递推相位因子): 与 PL FFT 数值一致(实测比值 100%), 但不受 PL 配置影响。
   正式测量用它, 16384 点约 0.15ms。 */



/* sum c[i]*(j x)^i */
static void cpoly(const double *c, int deg, double x, double *rr, double *ri)
{
    double pr = 1.0, pi = 0.0, sr = 0.0, si = 0.0;
    int i;

    for (i = 0; i <= deg; i++) {
        double t;

        sr += c[i] * pr;
        si += c[i] * pi;
        t  = -pi * x;
        pi =  pr * x;
        pr =  t;
    }
    *rr = sr; *ri = si;
}

/* 高斯消元(部分主元) */
static int gsolve(double *A, double *y, int n, double *x)
{
    int i, j, k, p;

    for (k = 0; k < n; k++) {
        double mx = fabs(A[k * n + k]);

        p = k;
        for (i = k + 1; i < n; i++) {
            if (fabs(A[i * n + k]) > mx) { mx = fabs(A[i * n + k]); p = i; }
        }
        if (mx < 1e-30) { return -1; }
        if (p != k) {
            for (j = 0; j < n; j++) { double t = A[k*n+j]; A[k*n+j] = A[p*n+j]; A[p*n+j] = t; }
            { double t = y[k]; y[k] = y[p]; y[p] = t; }
        }
        for (i = k + 1; i < n; i++) {
            double f = A[i * n + k] / A[k * n + k];

            for (j = k; j < n; j++) { A[i*n+j] -= f * A[k*n+j]; }
            y[i] -= f * y[k];
        }
    }
    for (i = n - 1; i >= 0; i--) {
        double s = y[i];

        for (j = i + 1; j < n; j++) { s -= A[i*n+j] * x[j]; }
        x[i] = s / A[i*n+i];
    }
    return 0;
}

/* 一轮加权 Levy 最小二乘 */
static int levy_once(const double *f_hz, const double *h_re, const double *h_im, int np,
                     int m, int n, const double *w, double *b, double *a, double *out_w)
{
    int nu = n + m + 1;
    double *N, *g, *x, *cr, *ci;
    int i, j, k, idx, ret;

    N  = (double *)calloc((size_t)(nu * nu), sizeof(double));
    g  = (double *)calloc((size_t)nu, sizeof(double));
    x  = (double *)calloc((size_t)nu, sizeof(double));
    cr = (double *)calloc((size_t)nu, sizeof(double));
    ci = (double *)calloc((size_t)nu, sizeof(double));
    if ((N == 0) || (g == 0) || (x == 0) || (cr == 0) || (ci == 0)) {
        free(N); free(g); free(x); free(cr); free(ci);
        return -1;
    }

    for (k = 0; k < np; k++) {
        double wr = 2.0 * FRA_PI * f_hz[k];
        double pw_r = 1.0, pw_i = 0.0;
        double hr = h_re[k], hi = h_im[k];
        double wk = (w != 0) ? w[k] : 1.0;

        idx = 0;
        for (i = 1; i <= n; i++) {                       /* a_i: (jw)^i * H */
            double t = -pw_i * wr;

            pw_i = pw_r * wr;
            pw_r = t;
            cr[idx] = pw_r * hr - pw_i * hi;
            ci[idx] = pw_r * hi + pw_i * hr;
            idx++;
        }
        pw_r = 1.0; pw_i = 0.0;
        for (i = 0; i <= m; i++) {                       /* b_i: -(jw)^i */
            double t;

            cr[idx] = -pw_r;
            ci[idx] = -pw_i;
            idx++;
            t = -pw_i * wr;
            pw_i = pw_r * wr;
            pw_r = t;
        }
        for (i = 0; i < nu; i++) {                       /* 正规方程(实/虚两行) */
            for (j = 0; j < nu; j++) {
                N[i*nu+j] += wk * (cr[i] * cr[j] + ci[i] * ci[j]);
            }
            g[i] += wk * (cr[i] * (-hr) + ci[i] * (-hi));
        }
    }
    ret = gsolve(N, g, nu, x);
    if (ret == 0) {
        idx = 0;
        a[0] = 1.0;
        for (i = 1; i <= n; i++) { a[i] = x[idx++]; }
        for (i = 0; i <= m; i++) { b[i] = x[idx++]; }
        if (out_w != 0) {                                /* SK 权重 1/|A|^2 */
            for (k = 0; k < np; k++) {
                double ar, ai;

                cpoly(a, n, 2.0 * FRA_PI * f_hz[k], &ar, &ai);
                out_w[k] = 1.0 / (ar * ar + ai * ai + 1e-12);
            }
        }
    }
    free(N); free(g); free(x); free(cr); free(ci);
    return ret;
}

double fra_fit(const double *f_hz, const double *h_re, const double *h_im, int np,
               int m, int n, double *b, double *a)
{
    double *w1, *w2, *wp, *wn, res = 0.0;
    int it, k;

    if ((np <= (m + n + 1)) || (m < 0) || (n < 1) ||
        (m > FRA_MAX_ORDER) || (n > FRA_MAX_ORDER)) { return -1.0; }
    w1 = (double *)calloc((size_t)np, sizeof(double));
    w2 = (double *)calloc((size_t)np, sizeof(double));
    if ((w1 == 0) || (w2 == 0)) { free(w1); free(w2); return -1.0; }

    wp = 0; wn = w1;
    for (it = 0; it < 3; it++) {                          /* 3 轮 SK 迭代 */
        double *t;

        if (levy_once(f_hz, h_re, h_im, np, m, n, wp, b, a, wn) != 0) {
            free(w1); free(w2);
            return -1.0;
        }
        t = wp; wp = wn; wn = t;
    }
    for (k = 0; k < np; k++) {                            /* 相对残差 */
        double fr, fi, gr, gi, e, d;

        fra_eval(b, m, a, n, f_hz[k], &fr, &fi);
        gr = h_re[k]; gi = h_im[k];
        e = (fr - gr) * (fr - gr) + (fi - gi) * (fi - gi);
        d = gr * gr + gi * gi;
        res += (d > 1e-30) ? (e / d) : e;
    }
    free(w1); free(w2);
    return sqrt(res / (double)np);
}

void fra_eval(const double *b, int m, const double *a, int n, double f_hz,
              double *o_re, double *o_im)
{
    double w = 2.0 * FRA_PI * f_hz;
    double br, bi, ar, ai, d;

    cpoly(b, m, w, &br, &bi);
    cpoly(a, n, w, &ar, &ai);
    d = ar * ar + ai * ai;
    if (d < 1e-30) { d = 1e-30; }
    *o_re = (br * ar + bi * ai) / d;
    *o_im = (bi * ar - br * ai) / d;
}

/* 迭代 radix-2 FFT(原地, 复数) */
static void fft_radix2(double *re, double *im, int n, int inverse)
{
    int i, j, k, len;

    for (i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;

        for (; (j & bit) != 0; bit >>= 1) { j ^= bit; }
        j ^= bit;
        if (i < j) {
            double t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (len = 2; len <= n; len <<= 1) {
        double ang = 2.0 * FRA_PI / (double)len * (inverse ? 1.0 : -1.0);
        double wr = cos(ang), wi = sin(ang);

        for (i = 0; i < n; i += len) {
            double pr = 1.0, pi = 0.0;

            for (k = 0; k < len / 2; k++) {
                int i0 = i + k, i1 = i + k + len / 2;
                double xr = re[i1] * pr - im[i1] * pi;
                double xi = re[i1] * pi + im[i1] * pr;
                double t;

                re[i1] = re[i0] - xr;
                im[i1] = im[i0] - xi;
                re[i0] += xr;
                im[i0] += xi;
                t = pr * wr - pi * wi;
                pi = pr * wi + pi * wr;
                pr = t;
            }
        }
    }
    if (inverse) {
        for (i = 0; i < n; i++) { re[i] /= (double)n; im[i] /= (double)n; }
    }
}


void fra_step(const float *h, int n, float *s)
{
    double acc = 0.0;
    int i;

    for (i = 0; i < n; i++) {
        acc += (double)h[i];
        s[i] = (float)acc;
    }
}

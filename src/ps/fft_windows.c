#include "fft_windows.h"
#include "math.h"

/* 汉宁窗权值表缓存(定点，4096对应1.0)，n变化时重建 */
#define HANN_TAB_MAX 16384
static int hann_tab[HANN_TAB_MAX];
static int hann_n = 0;

/* 重建n点汉宁窗权值表: w[i] = 0.5*(1-cos(2*PI*i/n)) * 4096 */
static void hann_build(int n)
{
    hann_tab[0] = 0;                    /* i=0   -> w=0 */
    if (n > 1) {
        hann_tab[n / 2] = 4096;         /* i=n/2 -> w=1 */
    }
    for (int i = 1; i < n / 2; i++) {
        double c = cos(2.0 * PI * (double)i / (double)(n - 1));
        hann_tab[i] = (int)((1.0 - c) * 2048.0 + 0.5);
    }
    /* 利用对称性 w[n-i] = w[i] 填后半段，省一半cos运算 */
    for (int i = 1; i < n / 2; i++) {
        hann_tab[n - i] = hann_tab[i];
    }
    hann_n = n;
}

void hanning_windows(int* data,int n) //汉宁窗，data:原始数据，范围-2048-2047,n:FFT点数
{
    if (data == 0 || n < 2) {
        return;                         /* 数据为空或点数过小，不做处理 */
    }
    if (n > HANN_TAB_MAX) {
        return;                         /* 超出窗口表容量，避免越界写 */
    }
    if (hann_n != n) {
        hann_build(n);                  /* 首次调用或点数变化时重建权值表 */
    }

    for (int i = 0; i < n; i++) {
        /* 定点运算: (data*weight)>>12 等价于 data*(weight/4096)，
           +2048为四舍五入，再限幅保证结果仍在-2048~2047 */
        int v = (data[i] * hann_tab[i] + 2048) >> 12;
        if (v > 2047) {
            v = 2047;
        }
        else if (v < -2048) {
            v = -2048;
        }
        data[i] = v;
    }
}

/* 汉明窗权值表缓存(定点，4096对应1.0)，n变化时重建 */
#define HAMM_TAB_MAX 16384
static int hamm_tab[HAMM_TAB_MAX];
static int hamm_n = 0;

/* 重建n点汉明窗权值表: w[i] = (0.54-0.46*cos(2*PI*i/n)) * 4096 */
static void hamm_build(int n)
{
    /* 端点w=0.08(不归零)，中点w=1.0 */
    hamm_tab[0] = (int)(0.08 * 4096.0 + 0.5);
    if (n > 1) {
        hamm_tab[n / 2] = 4096;
    }
    for (int i = 1; i < n / 2; i++) {
        double c = cos(2.0 * PI * (double)i / (double)(n - 1));
        hamm_tab[i] = (int)((0.54 - 0.46 * c) * 4096.0 + 0.5);
    }
    /* 利用对称性 w[n-i] = w[i] 填后半段，省一半cos运算 */
    for (int i = 1; i < n / 2; i++) {
        hamm_tab[n - i] = hamm_tab[i];
    }
    hamm_n = n;
}

void hamming_windows(int* data,int n) //汉明窗，data:原始数据，范围-2048-2047,n:FFT点数
{
    if (data == 0 || n < 2) {
        return;                         /* 数据为空或点数过小，不做处理 */
    }
    if (n > HAMM_TAB_MAX) {
        return;                         /* 超出窗口表容量，避免越界写 */
    }
    if (hamm_n != n) {
        hamm_build(n);                  /* 首次调用或点数变化时重建权值表 */
    }

    for (int i = 0; i < n; i++) {
        /* 与汉宁窗相同的定点处理，系数0.54使相干增益略大于1/2，
           乘完可能超出12位范围，故必须限幅 */
        int v = (data[i] * hamm_tab[i] + 2048) >> 12;
        if (v > 2047) {
            v = 2047;
        }
        else if (v < -2048) {
            v = -2048;
        }
        data[i] = v;
    }
}

/* 布莱克曼窗权值表缓存(定点，4096对应1.0)，n变化时重建 */
#define BLAC_TAB_MAX 16384
static int blac_tab[BLAC_TAB_MAX];
static int blac_n = 0;

/* 重建n点布莱克曼窗权值表:
   w[i] = (0.42 - 0.5*cos(2*PI*i/n) + 0.08*cos(4*PI*i/n)) * 4096 */
static void blac_build(int n)
{
    /* 端点w=0，中点w=1.0 */
    blac_tab[0] = 0;
    if (n > 1) {
        blac_tab[n / 2] = 4096;
    }
    for (int i = 1; i < n / 2; i++) {
        double a = 2.0 * PI * (double)i / (double)(n - 1);
        blac_tab[i] = (int)((0.42 - 0.5 * cos(a) + 0.08 * cos(2.0 * a)) * 4096.0 + 0.5);
    }
    /* 利用对称性 w[n-i] = w[i] 填后半段，省一半cos运算 */
    for (int i = 1; i < n / 2; i++) {
        blac_tab[n - i] = blac_tab[i];
    }
    blac_n = n;
}

void blackman_windows(int* data,int n) //布莱克曼窗，data:原始数据，范围-2048-2047,n:FFT点数
{
    if (data == 0 || n < 2) {
        return;                         /* 数据为空或点数过小，不做处理 */
    }
    if (n > BLAC_TAB_MAX) {
        return;                         /* 超出窗口表容量，避免越界写 */
    }
    if (blac_n != n) {
        blac_build(n);                  /* 首次调用或点数变化时重建权值表 */
    }

    for (int i = 0; i < n; i++) {
        /* 与前两个窗相同的定点处理与限幅 */
        int v = (data[i] * blac_tab[i] + 2048) >> 12;
        if (v > 2047) {
            v = 2047;
        }
        else if (v < -2048) {
            v = -2048;
        }
        data[i] = v;
    }
}

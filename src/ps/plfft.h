#ifndef  __PLFFT_H__
#define  __PLFFT_H__

#include "xaxidma.h"
#include "xparameters.h"
#include "fft_init_d.h"

/* FFT IP 的数据格式（由 prj 里 system_xfft_0_0.xml 的配置确定，调用方填缓冲必须遵守） */
#define PLFFT_TX_BYTES_PER_PT   4u    /* TX: bit15-0 实部(int16), bit31-16 虚部(int16) */
#define PLFFT_RX_BYTES_PER_PT   8u    /* RX: bit31-0 实部(int32), bit63-32 虚部(int32) */
#define PLFFT_TX_BUF_BYTES(n)   ((u32)(n) * PLFFT_TX_BYTES_PER_PT)
#define PLFFT_RX_BUF_BYTES(n)   ((u32)(n) * PLFFT_RX_BYTES_PER_PT)

/**
* @brief  FFT变换硬件加速。
* @param  TX      每一个数占4字节 bit15-0为实部  bit31-16为虚部，结果输入,长度为N
* @param  RX      每一个数占8字节 bit31-0为实部  bit63-32为虚部，结果输出,长度为N
* @param  fft_len FFT长度
* @return 
* @note   
*/
void fft_pl_accelerate(u8* TX, u8*RX,int fft_len,XAxiDma AxiDman);

/**
* @brief  IFFT变换硬件加速。
* @param  TX      每一个数占4字节 bit15-0为实部  bit31-16为虚部，结果输入,长度为N
* @param  RX      每一个数占8字节 bit31-0为实部  bit63-32为虚部，结果输出,长度为N
* @param  fft_len FFT长度
* @return 
* @note   与 fft_pl_accelerate 的唯一区别是旋转因子取共轭，
*         输出按位增长且不除以 N。
*/
void ifft_pl_accelerate(u8* TX, u8*RX,int fft_len,XAxiDma AxiDman);

#endif

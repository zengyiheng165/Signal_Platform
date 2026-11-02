#ifndef __FFT_WINDOWS_H_
#define __FFT_WINDOWS_H_

#define PI              3.141592653589793f

void hanning_windows(int* data,int n); //汉宁窗，data:原始数据，范围-2048-2047,n:FFT点数
void hamming_windows(int* data,int n); //汉明窗，data:原始数据，范围-2048-2047,n:FFT点数
void blackman_windows(int* data,int n); //布莱克曼窗，data:原始数据，范围-2048-2047,n:FFT点数

#endif 

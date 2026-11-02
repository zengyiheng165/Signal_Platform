#ifndef __DDS_WAVE_H_
#define __DDS_WAVE_H_

#include "my_dds_bram_23.h"
#include "my_dds.h"
#include "xparameters.h"
#include "xbram.h"
#include "math.h"

#define  START_ADDR      0 //启始地址
#define  BRAM_DATA_BYTE  4 //BRAM位宽
#define ch_data_len 4096
#define PI              3.141592653589793f

/* 最近一次写进 BRAM 的波形表(信号发生器界面预览用): g_awg_tab_ok = 1 表示有效 */
extern u16 g_awg_tab[ch_data_len];
extern u8  g_awg_tab_ok;


void str_wr_bram(int* dds_data); //将dds_data的4096个波形数据放入BRAM
void pl_bram_config(int fre,int pha,int am); //fre:配置频率：实际频率  pha：配置相位0-4096  am：配置幅度0-1024
void pl_dds1_config(int fre,int pha,int am,int type); //设置普通DDS通道一(DAC1)
void pl_dds2_config(int fre,int pha,int am,int type); //设置普通DDS通道二(DAC2)
void set_AM_wave(double Am,double Ac,double fmfc);//AM调制波形。Am：调制信号幅度，Ac：载波幅度，fmfc：载波频率相对于调制信号频率的倍数
void set_AMDSB_wave(double Am,double Ac,int fmfc);//DSB调制波形。Am：调制信号幅度，Ac：载波幅度，fmfc：载波频率相对于调制信号频率的倍数
void set_pulse(float duty);//方波，任意占空比，duty：占空比（数据范围0-1）
void set_FM_wave(double Am,double Ac,int fmfc,int Kf);//FM调制波形。Am：调制信号幅度，Ac：载波幅度，fmfc：载波频率相对于调制信号频率的倍数,Kf:调频灵敏度
void set_Sinc_wave(double wide);//Sinc函数，star：周期开始，end：周期结束
void set_2ASK_wave(double Ac,int fmfc,int bits,int* code) ; //2ASK波形。Ac：载波幅度，fmfc：一个周期内载波周期数，bits：一个周期的码元个数(=2^bit_type)，bit_type：码型选择
void set_PM_wave(double Am,double Ac,int fmfc,double Kp);
void set_harmonic_wave(int* am,int*pha,int n); //am:存储多次谐波幅度数组，pha：存储多次谐波的相位数组，n:谐波次数
#endif 
 

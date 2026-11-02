#ifndef  _FILTER_H__
#define  _FILTER_H__

#include "my_fir_init.h"
#include "my_iir.h"
#include "sleep.h"

/**
* @brief  FIR滤波器系数重载。
* @param  coefficient   FIR滤波器的系数数组，硬件默认12bit量化（即系数范围-2048-2047），最高10阶
* @return 无
* @note   FIR滤波器公式 y[n]=a0x[n]+a1x[n-1]+...+amx[n-m]，与coefficient系数依次对应。系数应尽量对称。
*/
void FIR_COEFFICIENT_RELOAD(int *coefficient);

/**
* @brief  IIR滤波器系数重载。
* @param  coefficientX   IIR滤波器的x[n]系数数组,整数，最高10阶
* @param  coefficientY   IIR滤波器的y[n]系数数组,整数，最高10阶
* @return 无
* @note   IIR滤波器公式  b0y[n]=a0x[n]+a1x[n-1]+...+amx[n-m]-b1y[n-1]-b2y[n-2]-...-bmy[n-m]
a与coefficientX一一对应，b0与coefficientY一一对应，b0可视为量化系数
*/
void IIR_COEFFICIENT_SET(int *coefficientX,int *coefficientY);

#endif

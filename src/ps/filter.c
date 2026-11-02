#include "filter.h"

/**
* @brief  FIR滤波器系数重载。
* @param  coefficient   FIR滤波器的系数数组，硬件默认12bit量化（即系数范围-2048-2047），最高10阶
* @return 无
* @note   FIR滤波器公式 y[n]=a0x[n]+a1x[n-1]+...+amx[n-m]，与coefficient系数依次对应。系数应尽量对称。
*/
void FIR_COEFFICIENT_RELOAD(int *coefficient)
{
    MY_FIR_INIT_mWriteReg(XPAR_MY_FIR_INIT_0_BASEADDR,MY_FIR_INIT_S00_AXI_SLV_REG0_OFFSET,coefficient[0]); //21    61   177   329   436   436   329   177    61    21
    
    MY_FIR_INIT_mWriteReg(XPAR_MY_FIR_INIT_0_BASEADDR,MY_FIR_INIT_S00_AXI_SLV_REG1_OFFSET,coefficient[1]);

    MY_FIR_INIT_mWriteReg(XPAR_MY_FIR_INIT_0_BASEADDR,MY_FIR_INIT_S00_AXI_SLV_REG2_OFFSET,coefficient[2]);

    MY_FIR_INIT_mWriteReg(XPAR_MY_FIR_INIT_0_BASEADDR,MY_FIR_INIT_S00_AXI_SLV_REG3_OFFSET,coefficient[3]);

    MY_FIR_INIT_mWriteReg(XPAR_MY_FIR_INIT_0_BASEADDR,MY_FIR_INIT_S00_AXI_SLV_REG4_OFFSET,coefficient[4]);

    MY_FIR_INIT_mWriteReg(XPAR_MY_FIR_INIT_0_BASEADDR,MY_FIR_INIT_S00_AXI_SLV_REG5_OFFSET,coefficient[5]);

    MY_FIR_INIT_mWriteReg(XPAR_MY_FIR_INIT_0_BASEADDR,MY_FIR_INIT_S00_AXI_SLV_REG6_OFFSET,coefficient[6]);

    MY_FIR_INIT_mWriteReg(XPAR_MY_FIR_INIT_0_BASEADDR,MY_FIR_INIT_S00_AXI_SLV_REG7_OFFSET,coefficient[7]);

    MY_FIR_INIT_mWriteReg(XPAR_MY_FIR_INIT_0_BASEADDR,MY_FIR_INIT_S00_AXI_SLV_REG8_OFFSET,coefficient[8]);

    MY_FIR_INIT_mWriteReg(XPAR_MY_FIR_INIT_0_BASEADDR,MY_FIR_INIT_S00_AXI_SLV_REG9_OFFSET,coefficient[9]);

    MY_FIR_INIT_mWriteReg(XPAR_MY_FIR_INIT_0_BASEADDR,MY_FIR_INIT_S00_AXI_SLV_REG10_OFFSET,1);//重装完成标志，若重装完成，请拉高再拉低。
    usleep(1);
    MY_FIR_INIT_mWriteReg(XPAR_MY_FIR_INIT_0_BASEADDR,MY_FIR_INIT_S00_AXI_SLV_REG10_OFFSET,0);
}

/**
* @brief  IIR滤波器系数重载。
* @param  coefficientX   IIR滤波器的x[n]系数数组,整数，最高10阶
* @param  coefficientY   IIR滤波器的y[n]系数数组,整数，最高10阶
* @return 无
* @note   IIR滤波器公式  b0y[n]=a0x[n]+a1x[n-1]+...+amx[n-m]-b1y[n-1]-b2y[n-2]-...-bmy[n-m]
a与coefficientX一一对应，b与coefficientY一一对应，b0可视为量化系数
*/
void IIR_COEFFICIENT_SET(int *coefficientX,int *coefficientY)
{
    MY_IIR_mWriteReg(XPAR_MY_IIR_1_BASEADDR,MY_IIR_S00_AXI_SLV_REG0_OFFSET,coefficientX[0]);

    MY_IIR_mWriteReg(XPAR_MY_IIR_1_BASEADDR,MY_IIR_S00_AXI_SLV_REG1_OFFSET,coefficientX[1]);

    MY_IIR_mWriteReg(XPAR_MY_IIR_1_BASEADDR,MY_IIR_S00_AXI_SLV_REG2_OFFSET,coefficientX[2]);

    MY_IIR_mWriteReg(XPAR_MY_IIR_1_BASEADDR,MY_IIR_S00_AXI_SLV_REG3_OFFSET,coefficientX[3]);

    MY_IIR_mWriteReg(XPAR_MY_IIR_1_BASEADDR,MY_IIR_S00_AXI_SLV_REG4_OFFSET,coefficientX[4]);

    MY_IIR_mWriteReg(XPAR_MY_IIR_1_BASEADDR,MY_IIR_S00_AXI_SLV_REG5_OFFSET,coefficientX[5]);

    MY_IIR_mWriteReg(XPAR_MY_IIR_1_BASEADDR,MY_IIR_S00_AXI_SLV_REG6_OFFSET,coefficientX[6]);

    MY_IIR_mWriteReg(XPAR_MY_IIR_1_BASEADDR,MY_IIR_S00_AXI_SLV_REG7_OFFSET,coefficientX[7]);

    MY_IIR_mWriteReg(XPAR_MY_IIR_1_BASEADDR,MY_IIR_S00_AXI_SLV_REG8_OFFSET,coefficientY[0]);

    MY_IIR_mWriteReg(XPAR_MY_IIR_1_BASEADDR,MY_IIR_S00_AXI_SLV_REG9_OFFSET,coefficientY[1]);

    MY_IIR_mWriteReg(XPAR_MY_IIR_1_BASEADDR,MY_IIR_S00_AXI_SLV_REG10_OFFSET,coefficientY[2]);

    MY_IIR_mWriteReg(XPAR_MY_IIR_1_BASEADDR,MY_IIR_S00_AXI_SLV_REG11_OFFSET,coefficientY[3]);

    MY_IIR_mWriteReg(XPAR_MY_IIR_1_BASEADDR,MY_IIR_S00_AXI_SLV_REG12_OFFSET,coefficientY[4]);

    MY_IIR_mWriteReg(XPAR_MY_IIR_1_BASEADDR,MY_IIR_S00_AXI_SLV_REG13_OFFSET,coefficientY[5]);

    MY_IIR_mWriteReg(XPAR_MY_IIR_1_BASEADDR,MY_IIR_S00_AXI_SLV_REG14_OFFSET,coefficientY[6]);

    MY_IIR_mWriteReg(XPAR_MY_IIR_1_BASEADDR,MY_IIR_S00_AXI_SLV_REG15_OFFSET,coefficientY[7]);
}
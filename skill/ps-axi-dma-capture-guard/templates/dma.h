#ifndef __DMA_H__
#define __DMA_H__

#include <xaxidma_hw.h>
#include "xaxidma.h"

#define RX_INTR_ID0       XPS_FPGA0_INT_ID
#define TX_INTR_ID0       XPS_FPGA1_INT_ID
#define RX_INTR_ID1       XPS_FPGA2_INT_ID
#define TX_INTR_ID1       XPS_FPGA3_INT_ID 
#define RESET_TIMEOUT_COUNTER	   10000           //复位超时的计数值

extern u32 TxDone0;        //发送完成的标志
extern u32 RxDone0;        //接收完成的标志
extern u32 Error0;         //错误信号的标志

extern u32 TxDone1;        //发送完成的标志
extern u32 RxDone1;        //接收完成的标志
extern u32 Error1;         //错误信号的标志


void dma_init(XAxiDma* AxiDma0, XAxiDma* AxiDma1);

void RxIntrHandler0(void *Callback);
void RxIntrHandler1(void *Callback);
void TxIntrHandler0(void *Callback);
void TxIntrHandler1(void *Callback);

#endif

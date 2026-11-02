#include <stdint.h>
#include <stdio.h>
#include "platform.h"
#include "sleep.h"
#include "xparameters.h"
#include "xil_printf.h"

#include "dma.h"
#include "intr.h"               //中断头文件

#define POLL_TIMEOUT_COUNTER    100000U
#define NUMBER_OF_EVENTS	1

#define N 16384          // 数据点数
#define MAX_PKT_LEN		           N*8            //发送数据包的长度

XScuGic Intc;   //中断控制器的驱动实例


XAxiDma AxiDma0;		//定义AXI DMA 的驱动实例
XAxiDma AxiDma1;		//定义AXI DMA 的驱动实例


u8 *RxBufferPtr0;
u8 *RxBufferPtr1;
u8 *TxBufferPtr0;
u8 *TxBufferPtr1;

int *Rx0;
int *Tx0;
long long int *Rx1;
long long int *Tx1;

u8* buff;

int main()
{
    init_platform();
    setbuf(stdout, NULL);

    //初始化dma
    dma_init(&AxiDma0,&AxiDma1);

    //要使用DMA中断，还要编写中断初始化函数
    intr_init(&Intc,&AxiDma0,&AxiDma1);

    //动态内存分配
	RxBufferPtr0 = malloc((MAX_PKT_LEN/2)*sizeof(u8));
    RxBufferPtr1 = malloc(MAX_PKT_LEN*sizeof(u8));
    TxBufferPtr0 = malloc((MAX_PKT_LEN/2)*sizeof(u8));
    TxBufferPtr1 = malloc(MAX_PKT_LEN*sizeof(u8));

    TxBufferPtr0 = buff;

    Rx0 = (int*)RxBufferPtr0;
    Rx1 = (long long int*)RxBufferPtr1;

    TxDone0 = 0;
    RxDone0 = 0;
    Error0 = 0;

    Xil_DCacheFlushRange((UINTPTR)TxBufferPtr0, MAX_PKT_LEN/2);
    Xil_DCacheFlushRange((UINTPTR)RxBufferPtr0, MAX_PKT_LEN/2);
    
    XAxiDma_SimpleTransfer(&AxiDma0, (UINTPTR) RxBufferPtr0,MAX_PKT_LEN/2, XAXIDMA_DEVICE_TO_DMA);
    XAxiDma_SimpleTransfer(&AxiDma0, (UINTPTR) TxBufferPtr0,MAX_PKT_LEN/2, XAXIDMA_DMA_TO_DEVICE);

    Xil_WaitForEventSet(POLL_TIMEOUT_COUNTER, NUMBER_OF_EVENTS, &RxDone0);
    Xil_WaitForEventSet(POLL_TIMEOUT_COUNTER, NUMBER_OF_EVENTS, &TxDone0); 

    TxDone0 = 0;
    RxDone0 = 0;
    Error0 = 0;


    RxBufferPtr0=NULL;
    RxBufferPtr1=NULL;
    TxBufferPtr0=NULL;
    TxBufferPtr1=NULL;
    free(RxBufferPtr0);
    free(RxBufferPtr1);
    free(TxBufferPtr0);
    free(TxBufferPtr1);

    cleanup_platform();
    return 0;
}

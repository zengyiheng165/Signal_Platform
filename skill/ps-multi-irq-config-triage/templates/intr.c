#include <stdio.h>
#include "intr.h"
#include <xparameters_ps.h>
#include "xparameters.h"


#define INTC_DEVICE_ID          XPAR_SCUGIC_SINGLE_DEVICE_ID //中断控制器的中断ID号

//中断初始化函数
int intr_init(XScuGic* Intc,XAxiDma* AxiDma0,XAxiDma* AxiDma1,XUartPs* Uart_Ps)
{
    int status;
    XScuGic_Config *intc_cfg;
    
    //初始化中断控制器
    intc_cfg = XScuGic_LookupConfig(INTC_DEVICE_ID);
    if (NULL == intc_cfg) {
        return XST_FAILURE;
    }

    //中断初始化
    status = XScuGic_CfgInitialize(Intc, intc_cfg, intc_cfg -> CpuBaseAddress);
    if (status != XST_SUCCESS) {
        return XST_FAILURE;
    }

    //设置优先级和触发类型
    XScuGic_SetPriorityTriggerType(Intc, RX_INTR_ID0, 0xA0, 0x3);
    XScuGic_SetPriorityTriggerType(Intc, RX_INTR_ID1, 0xA0, 0x3);
    XScuGic_SetPriorityTriggerType(Intc, TX_INTR_ID0, 0xA0, 0x3);
    XScuGic_SetPriorityTriggerType(Intc, TX_INTR_ID1, 0xA0, 0x3);

    //关联中断ID和中断处理函数  
    status = XScuGic_Connect(Intc, RX_INTR_ID0,
            (Xil_InterruptHandler) RxIntrHandler0, AxiDma0);
    if (status != XST_SUCCESS) {
        return status;
    }
    
    status = XScuGic_Connect(Intc, RX_INTR_ID1,
            (Xil_InterruptHandler) RxIntrHandler1, AxiDma1);
    if (status != XST_SUCCESS) {
        return status;
    }

    status = XScuGic_Connect(Intc, TX_INTR_ID0,
            (Xil_InterruptHandler) TxIntrHandler0, AxiDma0);
    if (status != XST_SUCCESS) {
        return status;
    }
    
    status = XScuGic_Connect(Intc, TX_INTR_ID1,
            (Xil_InterruptHandler) TxIntrHandler1, AxiDma1);
    if (status != XST_SUCCESS) {
        return status;
    }

    status = XScuGic_Connect(Intc, UART_INT_IRQ_ID,
            (Xil_InterruptHandler) uart_intr_handler, Uart_Ps);
    if (status != XST_SUCCESS) {
        return status;
    }

    //设置并打开UART中断异常处理功能
    Xil_ExceptionInit();
    Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT, (Xil_ExceptionHandler) XScuGic_InterruptHandler,(void *) Intc);
    Xil_ExceptionEnable();

    //设置UART中断触发方式
    XUartPs_SetInterruptMask(Uart_Ps, XUARTPS_IXR_RXOVR);

    XAxiDma_IntrDisable(AxiDma0, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DEVICE_TO_DMA);
    XAxiDma_IntrDisable(AxiDma1, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DEVICE_TO_DMA);
    XAxiDma_IntrDisable(AxiDma0, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DMA_TO_DEVICE);
    XAxiDma_IntrDisable(AxiDma1, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DMA_TO_DEVICE);
    
    //使能中断
    XScuGic_Enable(Intc, UART_INT_IRQ_ID);
    XScuGic_Enable(Intc, RX_INTR_ID0);
    XScuGic_Enable(Intc, RX_INTR_ID1);
    XScuGic_Enable(Intc, TX_INTR_ID0);
    XScuGic_Enable(Intc, TX_INTR_ID1);

    XAxiDma_IntrDisable(AxiDma0, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DEVICE_TO_DMA);
    XAxiDma_IntrDisable(AxiDma1, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DEVICE_TO_DMA);
    XAxiDma_IntrDisable(AxiDma0, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DMA_TO_DEVICE);
    XAxiDma_IntrDisable(AxiDma1, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DMA_TO_DEVICE);

    //使能DMA中断
    XAxiDma_IntrEnable(AxiDma0, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DEVICE_TO_DMA);
    XAxiDma_IntrEnable(AxiDma1, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DEVICE_TO_DMA);
    XAxiDma_IntrEnable(AxiDma0, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DMA_TO_DEVICE);
    XAxiDma_IntrEnable(AxiDma1, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DMA_TO_DEVICE);
    
    return XST_SUCCESS;
}


void disable_intr_system(XScuGic * int_ins_ptr, u16 rx1_intr_id,
        u16 rx2_intr_id)
{
    XScuGic_Disconnect(int_ins_ptr, rx1_intr_id);
    XScuGic_Disconnect(int_ins_ptr, rx2_intr_id);
}
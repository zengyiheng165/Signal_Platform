#include <stdio.h>
#include "dma.h"
#include "xparameters.h"


u32 TxDone0=0;        //发送完成的标志
u32 RxDone0=0;        //接收完成的标志
u32 Error0=0;         //错误信号的标志

u32 TxDone1=0;        //发送完成的标志
u32 RxDone1=0;        //接收完成的标志
u32 Error1=0;         //错误信号的标志


void dma_init(XAxiDma* AxiDma0, XAxiDma* AxiDma1)
{
    XAxiDma_Config *config0;
    XAxiDma_Config *config1;

    config0 = XAxiDma_LookupConfig(XPAR_XAXIDMA_0_BASEADDR);

    //初始化DMA引擎
    XAxiDma_CfgInitialize(AxiDma0, config0);

    
    if (XAxiDma_HasSg(AxiDma0)) 
    {
        xil_printf("Device configured as SG mode \r\n");
    }  


    config1 = XAxiDma_LookupConfig(XPAR_AXI_DMA_2_BASEADDR);

    XAxiDma_CfgInitialize(AxiDma1, config1);

    if (XAxiDma_HasSg(AxiDma1)) 
    {
        xil_printf("Device configured as SG mode \r\n");
    }
}


void RxIntrHandler0(void *Callback)
{
    u32 IrqStatus;
	int TimeOut;
	XAxiDma *AxiDmaInst = (XAxiDma *)Callback;

	//读取S2MM通道的中断状态
	IrqStatus = XAxiDma_IntrGetIrq(AxiDmaInst, XAXIDMA_DEVICE_TO_DMA);

	//清除中断
	XAxiDma_IntrAckIrq(AxiDmaInst, IrqStatus, XAXIDMA_DEVICE_TO_DMA);

	/*
	 * If no interrupt is asserted, we do not do anything
	 */
	if (!(IrqStatus & XAXIDMA_IRQ_ALL_MASK)) {

		return;
	}

	//接收出错
	if ((IrqStatus & XAXIDMA_IRQ_ERROR_MASK)) {

		Error0 = 1;

		/*
		 * Reset should never fail for transmit channel
		 */
		XAxiDma_Reset(AxiDmaInst);

		TimeOut = RESET_TIMEOUT_COUNTER;

		while (TimeOut) {
			if (XAxiDma_ResetIsDone(AxiDmaInst)) {
				break;
			}

			TimeOut -= 1;
		}

		return;
	}
    
	//接收完成
	if ((IrqStatus & XAXIDMA_IRQ_IOC_MASK)) {

		RxDone0 = 1;
	}
}


void RxIntrHandler1(void *Callback)
{
    u32 IrqStatus;
	int TimeOut;
	XAxiDma *AxiDmaInst = (XAxiDma *)Callback;

	//读取S2MM通道的中断状态
	IrqStatus = XAxiDma_IntrGetIrq(AxiDmaInst, XAXIDMA_DEVICE_TO_DMA);

	//清除中断
	XAxiDma_IntrAckIrq(AxiDmaInst, IrqStatus, XAXIDMA_DEVICE_TO_DMA);

	/*
	 * If no interrupt is asserted, we do not do anything
	 */
	if (!(IrqStatus & XAXIDMA_IRQ_ALL_MASK)) {

		return;
	}

	//接收出错
	if ((IrqStatus & XAXIDMA_IRQ_ERROR_MASK)) {

		Error1 = 1;

		/*
		 * Reset should never fail for transmit channel
		 */
		XAxiDma_Reset(AxiDmaInst);

		TimeOut = RESET_TIMEOUT_COUNTER;

		while (TimeOut) {
			if (XAxiDma_ResetIsDone(AxiDmaInst)) {
				break;
			}

			TimeOut -= 1;
		}

		return;
	}

	//接收完成
	if ((IrqStatus & XAXIDMA_IRQ_IOC_MASK)) {

		RxDone1 = 1;
	}
}


void TxIntrHandler0(void *Callback)
{
	u32 IrqStatus;
	int TimeOut;
	XAxiDma *AxiDmaInst = (XAxiDma *)Callback;

	//读取S2MM通道的中断状态
	IrqStatus = XAxiDma_IntrGetIrq(AxiDmaInst, XAXIDMA_DMA_TO_DEVICE);

	//清除中断
	XAxiDma_IntrAckIrq(AxiDmaInst, IrqStatus, XAXIDMA_DMA_TO_DEVICE);

	/*
	 * If no interrupt is asserted, we do not do anything
	 */
	if (!(IrqStatus & XAXIDMA_IRQ_ALL_MASK)) {

		return;
	}

	//发送出错
	if ((IrqStatus & XAXIDMA_IRQ_ERROR_MASK)) {

		Error0 = 1;

		/*
		 * Reset should never fail for transmit channel
		 */
		XAxiDma_Reset(AxiDmaInst);

		TimeOut = RESET_TIMEOUT_COUNTER;

		while (TimeOut) {
			if (XAxiDma_ResetIsDone(AxiDmaInst)) {
				break;
			}

			TimeOut -= 1;
		}

		return;
	}

	//接收完成
	if ((IrqStatus & XAXIDMA_IRQ_IOC_MASK)) {

		TxDone0 = 1;
	}
}


void TxIntrHandler1(void *Callback)
{
	u32 IrqStatus;
	int TimeOut;
	XAxiDma *AxiDmaInst = (XAxiDma *)Callback;

	//读取S2MM通道的中断状态
	IrqStatus = XAxiDma_IntrGetIrq(AxiDmaInst, XAXIDMA_DMA_TO_DEVICE);

	//清除中断
	XAxiDma_IntrAckIrq(AxiDmaInst, IrqStatus, XAXIDMA_DMA_TO_DEVICE);

	/*
	 * If no interrupt is asserted, we do not do anything
	 */
	if (!(IrqStatus & XAXIDMA_IRQ_ALL_MASK)) {

		return;
	}

	//发送出错
	if ((IrqStatus & XAXIDMA_IRQ_ERROR_MASK)) {

		Error1 = 1;

		/*
		 * Reset should never fail for transmit channel
		 */
		XAxiDma_Reset(AxiDmaInst);

		TimeOut = RESET_TIMEOUT_COUNTER;

		while (TimeOut) {
			if (XAxiDma_ResetIsDone(AxiDmaInst)) {
				break;
			}

			TimeOut -= 1;
		}

		return;
	}

	//接收完成
	if ((IrqStatus & XAXIDMA_IRQ_IOC_MASK)) {

		TxDone1 = 1;
	}
}

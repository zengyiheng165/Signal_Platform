#include "plfft.h"
#include "xil_util.h"
#include "math.h"
#include "sleep.h"


extern u32 TxDone2;        //发送完成的标志
extern u32 RxDone2;        //接收完成的标志
extern u32 Error2;         //错误信号的标志

#define POLL_TIMEOUT_COUNTER    10000U
#define NUMBER_OF_EVENTS	1

/**
* @brief  FFT变换硬件加速。
* @param  TX      每一个数占4字节 bit15-0为实部  bit31-16为虚部，结果输入,长度为N
* @param  RX      每一个数占8字节 bit31-0为实部  bit63-32为虚部，结果输出,长度为N
* @param  fft_len FFT长度
* @return 
* @note   
*/
void fft_pl_accelerate(u8* TX, u8*RX,int fft_len,XAxiDma AxiDman)
{
    int Status;

    FFT_INIT_D_mWriteReg(XPAR_FFT_INIT_D_0_BASEADDR,FFT_INIT_D_S00_AXI_SLV_REG0_OFFSET,(int)(log2(fft_len))|0x20);
    usleep(1);

    TxDone2 = 0;
    RxDone2 = 0;
    Error2 = 0;
    
    Xil_DCacheFlushRange((UINTPTR)TX, fft_len*4);
    Xil_DCacheFlushRange((UINTPTR)RX, fft_len*8);
    
    XAxiDma_SimpleTransfer(&AxiDman, (UINTPTR) RX,fft_len*8, XAXIDMA_DEVICE_TO_DMA);
    XAxiDma_SimpleTransfer(&AxiDman, (UINTPTR) TX,fft_len*4, XAXIDMA_DMA_TO_DEVICE);
    
    Status = Xil_WaitForEventSet(POLL_TIMEOUT_COUNTER, NUMBER_OF_EVENTS, &RxDone2);
	if (Status != XST_SUCCESS) {
		xil_printf("Receive failed111 %d\r\n", Status);
	}
        Status = Xil_WaitForEventSet(POLL_TIMEOUT_COUNTER, NUMBER_OF_EVENTS, &TxDone2);
	if (Status != XST_SUCCESS) {
			xil_printf("Transmit failed111 %d\r\n", Status);
	}
                                                        
}


/**
* @brief  IFFT变换硬件加速。
* @param  TX      每一个数占4字节 bit15-0为实部  bit31-16为虚部，结果输入,长度为N
* @param  RX      每一个数占8字节 bit31-0为实部  bit63-32为虚部，结果输出,长度为N
* @param  fft_len FFT长度
* @return 
* @note   与 fft_pl_accelerate 的唯一区别是旋转因子取共轭，
*         输出按位增长且不除以 N。
*/
void ifft_pl_accelerate(u8* TX, u8*RX,int fft_len,XAxiDma AxiDman)
{
    int Status;

    FFT_INIT_D_mWriteReg(XPAR_FFT_INIT_D_0_BASEADDR,FFT_INIT_D_S00_AXI_SLV_REG0_OFFSET,(int)(log2(fft_len)));
    usleep(1);

    TxDone2 = 0;
    RxDone2 = 0;
    Error2 = 0;

    Xil_DCacheFlushRange((UINTPTR)TX, fft_len*4);
    Xil_DCacheFlushRange((UINTPTR)RX, fft_len*8);
    
    XAxiDma_SimpleTransfer(&AxiDman, (UINTPTR) RX,fft_len*8, XAXIDMA_DEVICE_TO_DMA);
    XAxiDma_SimpleTransfer(&AxiDman, (UINTPTR) TX,fft_len*4, XAXIDMA_DMA_TO_DEVICE);
    
    Status = Xil_WaitForEventSet(POLL_TIMEOUT_COUNTER, NUMBER_OF_EVENTS, &RxDone2);
	if (Status != XST_SUCCESS) {
		xil_printf("Receive failed111 %d\r\n", Status);
	}
        Status = Xil_WaitForEventSet(POLL_TIMEOUT_COUNTER, NUMBER_OF_EVENTS, &TxDone2);
	if (Status != XST_SUCCESS) {
			xil_printf("Transmit failed111 %d\r\n", Status);
	}
                                                        
}

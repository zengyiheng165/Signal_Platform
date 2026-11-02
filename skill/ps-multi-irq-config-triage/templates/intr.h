#ifndef __INTR_H__
#define __INTR_H__

#include "xil_exception.h"
#include "xdebug.h"
#include "xil_util.h"
#include "xscugic.h"
#include "xinterrupt_wrap.h"
#include <xstatus.h>
#include "complex.h"

#include "uart.h"
#include "dma.h"

int intr_init(XScuGic* Intc,XAxiDma* AxiDma0,XAxiDma* AxiDma1,XUartPs* Uart_Ps);
void disable_intr_system(XScuGic * int_ins_ptr, u16 rx1_intr_id,
        u16 rx2_intr_id);

#endif

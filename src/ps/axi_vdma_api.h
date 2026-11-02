#ifndef __AXI_VDMA_API_H__
#define __AXI_VDMA_API_H__

#include "xaxivdma.h"
#include "xparameters.h"
#include "xil_exception.h"

int run_vdma_frame_buffer(XAxiVdma* InstancePtr, int baseaddress, int hsize,
		int vsize, int buf_base_addr, int number_frame_count,
		int enable_frm_cnt_intr);

#endif

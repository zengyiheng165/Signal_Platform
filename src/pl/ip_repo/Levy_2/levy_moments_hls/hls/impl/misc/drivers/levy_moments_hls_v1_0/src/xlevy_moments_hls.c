// ==============================================================
// Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2026.1 (64-bit)
// Tool Version Limit: 2026.06
// Copyright 1986-2022 Xilinx, Inc. All Rights Reserved.
// Copyright 2022-2026 Advanced Micro Devices, Inc. All Rights Reserved.
// 
// ==============================================================
/***************************** Include Files *********************************/
#include "xlevy_moments_hls.h"

/************************** Function Implementation *************************/
#ifndef __linux__
int XLevy_moments_hls_CfgInitialize(XLevy_moments_hls *InstancePtr, XLevy_moments_hls_Config *ConfigPtr) {
    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(ConfigPtr != NULL);

    InstancePtr->Control_BaseAddress = ConfigPtr->Control_BaseAddress;
    InstancePtr->IsReady = XIL_COMPONENT_IS_READY;

    return XST_SUCCESS;
}
#endif

void XLevy_moments_hls_Start(XLevy_moments_hls *InstancePtr) {
    u32 Data;

    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XLevy_moments_hls_ReadReg(InstancePtr->Control_BaseAddress, XLEVY_MOMENTS_HLS_CONTROL_ADDR_AP_CTRL) & 0x80;
    XLevy_moments_hls_WriteReg(InstancePtr->Control_BaseAddress, XLEVY_MOMENTS_HLS_CONTROL_ADDR_AP_CTRL, Data | 0x01);
}

u32 XLevy_moments_hls_IsDone(XLevy_moments_hls *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XLevy_moments_hls_ReadReg(InstancePtr->Control_BaseAddress, XLEVY_MOMENTS_HLS_CONTROL_ADDR_AP_CTRL);
    return (Data >> 1) & 0x1;
}

u32 XLevy_moments_hls_IsIdle(XLevy_moments_hls *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XLevy_moments_hls_ReadReg(InstancePtr->Control_BaseAddress, XLEVY_MOMENTS_HLS_CONTROL_ADDR_AP_CTRL);
    return (Data >> 2) & 0x1;
}

u32 XLevy_moments_hls_IsReady(XLevy_moments_hls *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XLevy_moments_hls_ReadReg(InstancePtr->Control_BaseAddress, XLEVY_MOMENTS_HLS_CONTROL_ADDR_AP_CTRL);
    // check ap_start to see if the pcore is ready for next input
    return !(Data & 0x1);
}

void XLevy_moments_hls_EnableAutoRestart(XLevy_moments_hls *InstancePtr) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XLevy_moments_hls_WriteReg(InstancePtr->Control_BaseAddress, XLEVY_MOMENTS_HLS_CONTROL_ADDR_AP_CTRL, 0x80);
}

void XLevy_moments_hls_DisableAutoRestart(XLevy_moments_hls *InstancePtr) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XLevy_moments_hls_WriteReg(InstancePtr->Control_BaseAddress, XLEVY_MOMENTS_HLS_CONTROL_ADDR_AP_CTRL, 0);
}

u32 XLevy_moments_hls_Get_return(XLevy_moments_hls *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XLevy_moments_hls_ReadReg(InstancePtr->Control_BaseAddress, XLEVY_MOMENTS_HLS_CONTROL_ADDR_AP_RETURN);
    return Data;
}
void XLevy_moments_hls_Set_samples(XLevy_moments_hls *InstancePtr, u64 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XLevy_moments_hls_WriteReg(InstancePtr->Control_BaseAddress, XLEVY_MOMENTS_HLS_CONTROL_ADDR_SAMPLES_DATA, (u32)(Data));
    XLevy_moments_hls_WriteReg(InstancePtr->Control_BaseAddress, XLEVY_MOMENTS_HLS_CONTROL_ADDR_SAMPLES_DATA + 4, (u32)(Data >> 32));
}

u64 XLevy_moments_hls_Get_samples(XLevy_moments_hls *InstancePtr) {
    u64 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XLevy_moments_hls_ReadReg(InstancePtr->Control_BaseAddress, XLEVY_MOMENTS_HLS_CONTROL_ADDR_SAMPLES_DATA);
    Data += (u64)XLevy_moments_hls_ReadReg(InstancePtr->Control_BaseAddress, XLEVY_MOMENTS_HLS_CONTROL_ADDR_SAMPLES_DATA + 4) << 32;
    return Data;
}

void XLevy_moments_hls_Set_point_count(XLevy_moments_hls *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XLevy_moments_hls_WriteReg(InstancePtr->Control_BaseAddress, XLEVY_MOMENTS_HLS_CONTROL_ADDR_POINT_COUNT_DATA, Data);
}

u32 XLevy_moments_hls_Get_point_count(XLevy_moments_hls *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XLevy_moments_hls_ReadReg(InstancePtr->Control_BaseAddress, XLEVY_MOMENTS_HLS_CONTROL_ADDR_POINT_COUNT_DATA);
    return Data;
}

u32 XLevy_moments_hls_Get_m0(XLevy_moments_hls *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XLevy_moments_hls_ReadReg(InstancePtr->Control_BaseAddress, XLEVY_MOMENTS_HLS_CONTROL_ADDR_M0_DATA);
    return Data;
}

u32 XLevy_moments_hls_Get_m0_vld(XLevy_moments_hls *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XLevy_moments_hls_ReadReg(InstancePtr->Control_BaseAddress, XLEVY_MOMENTS_HLS_CONTROL_ADDR_M0_CTRL);
    return Data & 0x1;
}

u32 XLevy_moments_hls_Get_m1(XLevy_moments_hls *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XLevy_moments_hls_ReadReg(InstancePtr->Control_BaseAddress, XLEVY_MOMENTS_HLS_CONTROL_ADDR_M1_DATA);
    return Data;
}

u32 XLevy_moments_hls_Get_m1_vld(XLevy_moments_hls *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XLevy_moments_hls_ReadReg(InstancePtr->Control_BaseAddress, XLEVY_MOMENTS_HLS_CONTROL_ADDR_M1_CTRL);
    return Data & 0x1;
}

u32 XLevy_moments_hls_Get_m2(XLevy_moments_hls *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XLevy_moments_hls_ReadReg(InstancePtr->Control_BaseAddress, XLEVY_MOMENTS_HLS_CONTROL_ADDR_M2_DATA);
    return Data;
}

u32 XLevy_moments_hls_Get_m2_vld(XLevy_moments_hls *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XLevy_moments_hls_ReadReg(InstancePtr->Control_BaseAddress, XLEVY_MOMENTS_HLS_CONTROL_ADDR_M2_CTRL);
    return Data & 0x1;
}

u32 XLevy_moments_hls_Get_m3(XLevy_moments_hls *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XLevy_moments_hls_ReadReg(InstancePtr->Control_BaseAddress, XLEVY_MOMENTS_HLS_CONTROL_ADDR_M3_DATA);
    return Data;
}

u32 XLevy_moments_hls_Get_m3_vld(XLevy_moments_hls *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XLevy_moments_hls_ReadReg(InstancePtr->Control_BaseAddress, XLEVY_MOMENTS_HLS_CONTROL_ADDR_M3_CTRL);
    return Data & 0x1;
}

u32 XLevy_moments_hls_Get_m4(XLevy_moments_hls *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XLevy_moments_hls_ReadReg(InstancePtr->Control_BaseAddress, XLEVY_MOMENTS_HLS_CONTROL_ADDR_M4_DATA);
    return Data;
}

u32 XLevy_moments_hls_Get_m4_vld(XLevy_moments_hls *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XLevy_moments_hls_ReadReg(InstancePtr->Control_BaseAddress, XLEVY_MOMENTS_HLS_CONTROL_ADDR_M4_CTRL);
    return Data & 0x1;
}

u32 XLevy_moments_hls_Get_m5(XLevy_moments_hls *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XLevy_moments_hls_ReadReg(InstancePtr->Control_BaseAddress, XLEVY_MOMENTS_HLS_CONTROL_ADDR_M5_DATA);
    return Data;
}

u32 XLevy_moments_hls_Get_m5_vld(XLevy_moments_hls *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XLevy_moments_hls_ReadReg(InstancePtr->Control_BaseAddress, XLEVY_MOMENTS_HLS_CONTROL_ADDR_M5_CTRL);
    return Data & 0x1;
}

u32 XLevy_moments_hls_Get_m6(XLevy_moments_hls *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XLevy_moments_hls_ReadReg(InstancePtr->Control_BaseAddress, XLEVY_MOMENTS_HLS_CONTROL_ADDR_M6_DATA);
    return Data;
}

u32 XLevy_moments_hls_Get_m6_vld(XLevy_moments_hls *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XLevy_moments_hls_ReadReg(InstancePtr->Control_BaseAddress, XLEVY_MOMENTS_HLS_CONTROL_ADDR_M6_CTRL);
    return Data & 0x1;
}

void XLevy_moments_hls_InterruptGlobalEnable(XLevy_moments_hls *InstancePtr) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XLevy_moments_hls_WriteReg(InstancePtr->Control_BaseAddress, XLEVY_MOMENTS_HLS_CONTROL_ADDR_GIE, 1);
}

void XLevy_moments_hls_InterruptGlobalDisable(XLevy_moments_hls *InstancePtr) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XLevy_moments_hls_WriteReg(InstancePtr->Control_BaseAddress, XLEVY_MOMENTS_HLS_CONTROL_ADDR_GIE, 0);
}

void XLevy_moments_hls_InterruptEnable(XLevy_moments_hls *InstancePtr, u32 Mask) {
    u32 Register;

    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Register =  XLevy_moments_hls_ReadReg(InstancePtr->Control_BaseAddress, XLEVY_MOMENTS_HLS_CONTROL_ADDR_IER);
    XLevy_moments_hls_WriteReg(InstancePtr->Control_BaseAddress, XLEVY_MOMENTS_HLS_CONTROL_ADDR_IER, Register | Mask);
}

void XLevy_moments_hls_InterruptDisable(XLevy_moments_hls *InstancePtr, u32 Mask) {
    u32 Register;

    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Register =  XLevy_moments_hls_ReadReg(InstancePtr->Control_BaseAddress, XLEVY_MOMENTS_HLS_CONTROL_ADDR_IER);
    XLevy_moments_hls_WriteReg(InstancePtr->Control_BaseAddress, XLEVY_MOMENTS_HLS_CONTROL_ADDR_IER, Register & (~Mask));
}

void XLevy_moments_hls_InterruptClear(XLevy_moments_hls *InstancePtr, u32 Mask) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XLevy_moments_hls_WriteReg(InstancePtr->Control_BaseAddress, XLEVY_MOMENTS_HLS_CONTROL_ADDR_ISR, Mask);
}

u32 XLevy_moments_hls_InterruptGetEnabled(XLevy_moments_hls *InstancePtr) {
    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    return XLevy_moments_hls_ReadReg(InstancePtr->Control_BaseAddress, XLEVY_MOMENTS_HLS_CONTROL_ADDR_IER);
}

u32 XLevy_moments_hls_InterruptGetStatus(XLevy_moments_hls *InstancePtr) {
    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    return XLevy_moments_hls_ReadReg(InstancePtr->Control_BaseAddress, XLEVY_MOMENTS_HLS_CONTROL_ADDR_ISR);
}


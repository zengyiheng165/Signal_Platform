// ==============================================================
// Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2026.1 (64-bit)
// Tool Version Limit: 2026.06
// Copyright 1986-2022 Xilinx, Inc. All Rights Reserved.
// Copyright 2022-2026 Advanced Micro Devices, Inc. All Rights Reserved.
// 
// ==============================================================
#ifndef XLEVY_MOMENTS_HLS_H
#define XLEVY_MOMENTS_HLS_H

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/
#ifndef __linux__
#include "xil_types.h"
#include "xil_assert.h"
#include "xstatus.h"
#include "xil_io.h"
#else
#include <stdint.h>
#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stddef.h>
#endif
#include "xlevy_moments_hls_hw.h"

/**************************** Type Definitions ******************************/
#ifdef __linux__
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#else
typedef struct {
#ifdef SDT
    char *Name;
#else
    u16 DeviceId;
#endif
    u64 Control_BaseAddress;
} XLevy_moments_hls_Config;
#endif

typedef struct {
    u64 Control_BaseAddress;
    u32 IsReady;
} XLevy_moments_hls;

typedef u32 word_type;

/***************** Macros (Inline Functions) Definitions *********************/
#ifndef __linux__
#define XLevy_moments_hls_WriteReg(BaseAddress, RegOffset, Data) \
    Xil_Out32((BaseAddress) + (RegOffset), (u32)(Data))
#define XLevy_moments_hls_ReadReg(BaseAddress, RegOffset) \
    Xil_In32((BaseAddress) + (RegOffset))
#else
#define XLevy_moments_hls_WriteReg(BaseAddress, RegOffset, Data) \
    *(volatile u32*)((BaseAddress) + (RegOffset)) = (u32)(Data)
#define XLevy_moments_hls_ReadReg(BaseAddress, RegOffset) \
    *(volatile u32*)((BaseAddress) + (RegOffset))

#define Xil_AssertVoid(expr)    assert(expr)
#define Xil_AssertNonvoid(expr) assert(expr)

#define XST_SUCCESS             0
#define XST_DEVICE_NOT_FOUND    2
#define XST_OPEN_DEVICE_FAILED  3
#define XIL_COMPONENT_IS_READY  1
#endif

/************************** Function Prototypes *****************************/
#ifndef __linux__
#ifdef SDT
int XLevy_moments_hls_Initialize(XLevy_moments_hls *InstancePtr, UINTPTR BaseAddress);
XLevy_moments_hls_Config* XLevy_moments_hls_LookupConfig(UINTPTR BaseAddress);
#else
int XLevy_moments_hls_Initialize(XLevy_moments_hls *InstancePtr, u16 DeviceId);
XLevy_moments_hls_Config* XLevy_moments_hls_LookupConfig(u16 DeviceId);
#endif
int XLevy_moments_hls_CfgInitialize(XLevy_moments_hls *InstancePtr, XLevy_moments_hls_Config *ConfigPtr);
#else
int XLevy_moments_hls_Initialize(XLevy_moments_hls *InstancePtr, const char* InstanceName);
int XLevy_moments_hls_Release(XLevy_moments_hls *InstancePtr);
#endif

void XLevy_moments_hls_Start(XLevy_moments_hls *InstancePtr);
u32 XLevy_moments_hls_IsDone(XLevy_moments_hls *InstancePtr);
u32 XLevy_moments_hls_IsIdle(XLevy_moments_hls *InstancePtr);
u32 XLevy_moments_hls_IsReady(XLevy_moments_hls *InstancePtr);
void XLevy_moments_hls_EnableAutoRestart(XLevy_moments_hls *InstancePtr);
void XLevy_moments_hls_DisableAutoRestart(XLevy_moments_hls *InstancePtr);
u32 XLevy_moments_hls_Get_return(XLevy_moments_hls *InstancePtr);

void XLevy_moments_hls_Set_samples(XLevy_moments_hls *InstancePtr, u64 Data);
u64 XLevy_moments_hls_Get_samples(XLevy_moments_hls *InstancePtr);
void XLevy_moments_hls_Set_point_count(XLevy_moments_hls *InstancePtr, u32 Data);
u32 XLevy_moments_hls_Get_point_count(XLevy_moments_hls *InstancePtr);
u32 XLevy_moments_hls_Get_m0(XLevy_moments_hls *InstancePtr);
u32 XLevy_moments_hls_Get_m0_vld(XLevy_moments_hls *InstancePtr);
u32 XLevy_moments_hls_Get_m1(XLevy_moments_hls *InstancePtr);
u32 XLevy_moments_hls_Get_m1_vld(XLevy_moments_hls *InstancePtr);
u32 XLevy_moments_hls_Get_m2(XLevy_moments_hls *InstancePtr);
u32 XLevy_moments_hls_Get_m2_vld(XLevy_moments_hls *InstancePtr);
u32 XLevy_moments_hls_Get_m3(XLevy_moments_hls *InstancePtr);
u32 XLevy_moments_hls_Get_m3_vld(XLevy_moments_hls *InstancePtr);
u32 XLevy_moments_hls_Get_m4(XLevy_moments_hls *InstancePtr);
u32 XLevy_moments_hls_Get_m4_vld(XLevy_moments_hls *InstancePtr);
u32 XLevy_moments_hls_Get_m5(XLevy_moments_hls *InstancePtr);
u32 XLevy_moments_hls_Get_m5_vld(XLevy_moments_hls *InstancePtr);
u32 XLevy_moments_hls_Get_m6(XLevy_moments_hls *InstancePtr);
u32 XLevy_moments_hls_Get_m6_vld(XLevy_moments_hls *InstancePtr);

void XLevy_moments_hls_InterruptGlobalEnable(XLevy_moments_hls *InstancePtr);
void XLevy_moments_hls_InterruptGlobalDisable(XLevy_moments_hls *InstancePtr);
void XLevy_moments_hls_InterruptEnable(XLevy_moments_hls *InstancePtr, u32 Mask);
void XLevy_moments_hls_InterruptDisable(XLevy_moments_hls *InstancePtr, u32 Mask);
void XLevy_moments_hls_InterruptClear(XLevy_moments_hls *InstancePtr, u32 Mask);
u32 XLevy_moments_hls_InterruptGetEnabled(XLevy_moments_hls *InstancePtr);
u32 XLevy_moments_hls_InterruptGetStatus(XLevy_moments_hls *InstancePtr);

#ifdef __cplusplus
}
#endif

#endif

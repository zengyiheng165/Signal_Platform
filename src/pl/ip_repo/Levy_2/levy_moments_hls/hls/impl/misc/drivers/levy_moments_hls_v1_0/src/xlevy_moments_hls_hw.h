// ==============================================================
// Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2026.1 (64-bit)
// Tool Version Limit: 2026.06
// Copyright 1986-2022 Xilinx, Inc. All Rights Reserved.
// Copyright 2022-2026 Advanced Micro Devices, Inc. All Rights Reserved.
// 
// ==============================================================
// control
// 0x00 : Control signals
//        bit 0  - ap_start (Read/Write/COH)
//        bit 1  - ap_done (Read/COR)
//        bit 2  - ap_idle (Read)
//        bit 3  - ap_ready (Read/COR)
//        bit 7  - auto_restart (Read/Write)
//        bit 9  - interrupt (Read)
//        others - reserved
// 0x04 : Global Interrupt Enable Register
//        bit 0  - Global Interrupt Enable (Read/Write)
//        others - reserved
// 0x08 : IP Interrupt Enable Register (Read/Write)
//        bit 0 - enable ap_done interrupt (Read/Write)
//        bit 1 - enable ap_ready interrupt (Read/Write)
//        others - reserved
// 0x0c : IP Interrupt Status Register (Read/TOW)
//        bit 0 - ap_done (Read/TOW)
//        bit 1 - ap_ready (Read/TOW)
//        others - reserved
// 0x10 : Data signal of ap_return
//        bit 31~0 - ap_return[31:0] (Read)
// 0x18 : Data signal of samples
//        bit 31~0 - samples[31:0] (Read/Write)
// 0x1c : Data signal of samples
//        bit 31~0 - samples[63:32] (Read/Write)
// 0x20 : reserved
// 0x24 : Data signal of point_count
//        bit 31~0 - point_count[31:0] (Read/Write)
// 0x28 : reserved
// 0x2c : Data signal of m0
//        bit 31~0 - m0[31:0] (Read)
// 0x30 : Control signal of m0
//        bit 0  - m0_ap_vld (Read/COR)
//        others - reserved
// 0x3c : Data signal of m1
//        bit 31~0 - m1[31:0] (Read)
// 0x40 : Control signal of m1
//        bit 0  - m1_ap_vld (Read/COR)
//        others - reserved
// 0x4c : Data signal of m2
//        bit 31~0 - m2[31:0] (Read)
// 0x50 : Control signal of m2
//        bit 0  - m2_ap_vld (Read/COR)
//        others - reserved
// 0x5c : Data signal of m3
//        bit 31~0 - m3[31:0] (Read)
// 0x60 : Control signal of m3
//        bit 0  - m3_ap_vld (Read/COR)
//        others - reserved
// 0x6c : Data signal of m4
//        bit 31~0 - m4[31:0] (Read)
// 0x70 : Control signal of m4
//        bit 0  - m4_ap_vld (Read/COR)
//        others - reserved
// 0x7c : Data signal of m5
//        bit 31~0 - m5[31:0] (Read)
// 0x80 : Control signal of m5
//        bit 0  - m5_ap_vld (Read/COR)
//        others - reserved
// 0x8c : Data signal of m6
//        bit 31~0 - m6[31:0] (Read)
// 0x90 : Control signal of m6
//        bit 0  - m6_ap_vld (Read/COR)
//        others - reserved
// (SC = Self Clear, COR = Clear on Read, TOW = Toggle on Write, COH = Clear on Handshake)

#define XLEVY_MOMENTS_HLS_CONTROL_ADDR_AP_CTRL          0x00
#define XLEVY_MOMENTS_HLS_CONTROL_ADDR_GIE              0x04
#define XLEVY_MOMENTS_HLS_CONTROL_ADDR_IER              0x08
#define XLEVY_MOMENTS_HLS_CONTROL_ADDR_ISR              0x0c
#define XLEVY_MOMENTS_HLS_CONTROL_ADDR_AP_RETURN        0x10
#define XLEVY_MOMENTS_HLS_CONTROL_BITS_AP_RETURN        32
#define XLEVY_MOMENTS_HLS_CONTROL_ADDR_SAMPLES_DATA     0x18
#define XLEVY_MOMENTS_HLS_CONTROL_BITS_SAMPLES_DATA     64
#define XLEVY_MOMENTS_HLS_CONTROL_ADDR_POINT_COUNT_DATA 0x24
#define XLEVY_MOMENTS_HLS_CONTROL_BITS_POINT_COUNT_DATA 32
#define XLEVY_MOMENTS_HLS_CONTROL_ADDR_M0_DATA          0x2c
#define XLEVY_MOMENTS_HLS_CONTROL_BITS_M0_DATA          32
#define XLEVY_MOMENTS_HLS_CONTROL_ADDR_M0_CTRL          0x30
#define XLEVY_MOMENTS_HLS_CONTROL_ADDR_M1_DATA          0x3c
#define XLEVY_MOMENTS_HLS_CONTROL_BITS_M1_DATA          32
#define XLEVY_MOMENTS_HLS_CONTROL_ADDR_M1_CTRL          0x40
#define XLEVY_MOMENTS_HLS_CONTROL_ADDR_M2_DATA          0x4c
#define XLEVY_MOMENTS_HLS_CONTROL_BITS_M2_DATA          32
#define XLEVY_MOMENTS_HLS_CONTROL_ADDR_M2_CTRL          0x50
#define XLEVY_MOMENTS_HLS_CONTROL_ADDR_M3_DATA          0x5c
#define XLEVY_MOMENTS_HLS_CONTROL_BITS_M3_DATA          32
#define XLEVY_MOMENTS_HLS_CONTROL_ADDR_M3_CTRL          0x60
#define XLEVY_MOMENTS_HLS_CONTROL_ADDR_M4_DATA          0x6c
#define XLEVY_MOMENTS_HLS_CONTROL_BITS_M4_DATA          32
#define XLEVY_MOMENTS_HLS_CONTROL_ADDR_M4_CTRL          0x70
#define XLEVY_MOMENTS_HLS_CONTROL_ADDR_M5_DATA          0x7c
#define XLEVY_MOMENTS_HLS_CONTROL_BITS_M5_DATA          32
#define XLEVY_MOMENTS_HLS_CONTROL_ADDR_M5_CTRL          0x80
#define XLEVY_MOMENTS_HLS_CONTROL_ADDR_M6_DATA          0x8c
#define XLEVY_MOMENTS_HLS_CONTROL_BITS_M6_DATA          32
#define XLEVY_MOMENTS_HLS_CONTROL_ADDR_M6_CTRL          0x90


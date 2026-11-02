# ==============================================================
# Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2026.1 (64-bit)
# Tool Version Limit: 2026.06
# Copyright 1986-2022 Xilinx, Inc. All Rights Reserved.
# Copyright 2022-2026 Advanced Micro Devices, Inc. All Rights Reserved.
# 
# ==============================================================
source -notrace "D:/Vivado26.1/2026.1/Vitis/common/scripts/ipxhls.tcl"
set ip_out_dir "D:/Vivado26.1/Embeddedrace_v_4_2/WS_HLS/Levy_2/levy_moments_hls/hls/impl/ip"
set data_file "D:/Vivado26.1/Embeddedrace_v_4_2/WS_HLS/Levy_2/levy_moments_hls/hls/hls_data.json"
set src_dir [file dir $data_file]
set ip_types {vitis sysgen}
set ippack_opts_dict {}
if { [catch {::ipx::utils::package_hls_ip $ip_out_dir $data_file $ip_types $src_dir $ippack_opts_dict} res] } {
  puts "Caught error:\n$::errorInfo"
  error $res
}

`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
// Company: DMU
// Engineer: Zengyh
// 
// Create Date: 2026/07/16 10:36:25
// Design Name: FIR
// Module Name: accumulator_fir
//////////////////////////////////////////////////////////////////////////////////


module accumulator_fir(
    input wire  sys_clk,
    input wire  sys_rst_n,
    input wire  [11:0] x_data,
    
    output wire [13:0] yout,
    output wire        dacclk,
    output wire        WRT,
    
    input	wire		    s_axis_config_tvalid   ,
	output	wire		   s_axis_config_tready   ,
	input	wire    [7:0]   s_axis_config_tdata    ,
	
	input 	wire		       s_axis_reload_tvalid   ,
	output 	wire		   s_axis_reload_tready   ,
	input 	wire		       s_axis_reload_tlast    ,
	input  wire	 [15:0]	   s_axis_reload_tdata    
    );

wire                      s_axis_data_tvalid=1;
wire                      s_axis_data_tready;
wire  signed    [15:0]    s_axis_data_tdata=x_data-2048;
//wire                      clk_12M;
wire  signed   [31:0] m_axis_data_tdata;

assign yout=((m_axis_data_tdata/2048)<<2)+8192;

assign dacclk=~sys_clk;

assign WRT=dacclk;


fir_compiler_0 fir_compiler_inst1(
  .aclk(sys_clk),                              // input wire aclk
  .s_axis_data_tvalid(s_axis_data_tvalid),  // input wire s_axis_data_tvalid
  .s_axis_data_tready(s_axis_data_tready),  // output wire s_axis_data_tready
  .s_axis_data_tdata(s_axis_data_tdata),    // input wire [15 : 0] s_axis_data_tdata
  //.m_axis_data_tvalid(m_axis_data_tvalid),  // output wire m_axis_data_tvalid
  .m_axis_data_tdata(m_axis_data_tdata),    // output wire [31 : 0] m_axis_data_tdata
  
  .s_axis_config_tvalid(s_axis_config_tvalid),                        // input wire s_axis_config_tvalid
  .s_axis_config_tready(s_axis_config_tready),                        // output wire s_axis_config_tready
  .s_axis_config_tdata(s_axis_config_tdata),                          // input wire [7 : 0] s_axis_config_tdata
  .s_axis_reload_tvalid(s_axis_reload_tvalid),                        // input wire s_axis_reload_tvalid
  .s_axis_reload_tready(s_axis_reload_tready),                        // output wire s_axis_reload_tready
  .s_axis_reload_tlast(s_axis_reload_tlast),                          // input wire s_axis_reload_tlast
  .s_axis_reload_tdata(s_axis_reload_tdata)  
); 

endmodule

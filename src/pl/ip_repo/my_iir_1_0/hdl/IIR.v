`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
// Company: DMU
// Engineer: Zengyh
// 
// Create Date: 2026/07/21 14:04:38
// Design Name: 
// Module Name: IIR
//////////////////////////////////////////////////////////////////////////////////
module IIR(
    input  wire  sys_rst,
    input  wire  sys_clk,
    
    input  wire  [11:0] din,
    output wire  [13:0] dout
    );
    
    
wire signed [11:0] din_mid=din-2048;
wire signed [11:0] dout_mid;
wire signed [20:0] Xout;

ZeroParallel ZeroParallel_inst
(
    .sys_rst(sys_rst),          
    .sys_clk(sys_clk),          
                  
    .Xin(din_mid),
    .Xout(Xout)
); 
 
wire signed [11:0] Yin;
wire signed [23:0] Yout;

PoleParallel PoleParallel_inst
(
    .sys_rst(sys_rst),          
    .sys_clk(sys_clk),          
                  
    .Yin(Yin),
    .Yout(Yout)
); 
 
wire signed [24:0] Ysum;

assign Ysum=Xout-Yout;

wire signed [24:0] Ydiv;

assign Ydiv={{9{Ysum[23]}},Ysum[23:9]};

assign Yin=(~sys_rst)?8'd0:Ydiv[11:0];

assign dout_mid=Yin;

assign dout=(dout_mid*4)+8192;

endmodule

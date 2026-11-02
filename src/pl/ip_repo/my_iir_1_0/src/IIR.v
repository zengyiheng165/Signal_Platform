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
    input  wire               sys_rst,
    input  wire               sys_clk,
    input  wire signed [11:0] Xcoe0,
    input  wire signed [11:0] Xcoe1,
    input  wire signed [11:0] Xcoe2,
    input  wire signed [11:0] Xcoe3,
    input  wire signed [11:0] Xcoe4,
    input  wire signed [11:0] Xcoe5,
    input  wire signed [11:0] Xcoe6,
    input  wire signed [11:0] Xcoe7,
    input  wire signed [11:0] Ycoe0,
    input  wire signed [11:0] Ycoe1,
    input  wire signed [11:0] Ycoe2,
    input  wire signed [11:0] Ycoe3,
    input  wire signed [11:0] Ycoe4,
    input  wire signed [11:0] Ycoe5,
    input  wire signed [11:0] Ycoe6,
    input  wire signed [11:0] Ycoe7,
    
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
    .Xcoe0(Xcoe0),
    .Xcoe1(Xcoe1),
    .Xcoe2(Xcoe2),
    .Xcoe3(Xcoe3),
    .Xcoe4(Xcoe4),
    .Xcoe5(Xcoe5),
    .Xcoe6(Xcoe6),
    .Xcoe7(Xcoe7), 
                  
    .Xin(din_mid),
    .Xout(Xout)
); 
 
wire signed [11:0] Yin;
wire signed [23:0] Yout;

PoleParallel PoleParallel_inst
(
    .sys_rst(sys_rst),          
    .sys_clk(sys_clk),          
    .Ycoe0(Ycoe0),
    .Ycoe1(Ycoe1),
    .Ycoe2(Ycoe2),
    .Ycoe3(Ycoe3),
    .Ycoe4(Ycoe4),
    .Ycoe5(Ycoe5),
    .Ycoe6(Ycoe6),
    .Ycoe7(Ycoe7),            
    .Yin(Yin),
    .Yout(Yout)
); 
 
wire signed [24:0] Ysum;

assign Ysum=Xout-Yout;

wire signed [24:0] Ydiv;

assign Ydiv=(Ycoe0)?Ysum/Ycoe0:0;

assign Yin=(~sys_rst)?8'd0:Ydiv[11:0];

assign dout_mid=Yin;

assign dout=(dout_mid*4)+8192;

endmodule

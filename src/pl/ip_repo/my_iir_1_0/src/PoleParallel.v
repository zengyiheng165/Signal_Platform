`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
// Company: DMU
// Engineer: ZengYH
// 
// Create Date: 2026/07/21 13:18:27
// Design Name: 
// Module Name: PoleParallel
//////////////////////////////////////////////////////////////////////////////////


module PoleParallel(
    input  wire  sys_rst,
    input  wire  sys_clk,
    input  wire signed [11:0] Ycoe0,
    input  wire signed [11:0] Ycoe1,
    input  wire signed [11:0] Ycoe2,
    input  wire signed [11:0] Ycoe3,
    input  wire signed [11:0] Ycoe4,
    input  wire signed [11:0] Ycoe5,
    input  wire signed [11:0] Ycoe6,
    input  wire signed [11:0] Ycoe7,

    input  wire signed [11:0]   Yin,
    output wire signed [23:0]  Yout
    );
    
reg signed [11:0]  Yin_Reg[6:0];
reg [3:0]         i=0;
reg [3:0]         j=0;

always@(posedge sys_clk or negedge sys_rst)
    if(!sys_rst)begin
        for(i=0;i<7;i=i+1)
            Yin_Reg[i]<=12'd0;
    end
    else begin
         for(j=0;j<6;j=j+1)
            Yin_Reg[j+1]<=Yin_Reg[j];
          Yin_Reg[0]<=Yin;
    end

wire signed [23:0] Mult_Reg[6:0];


assign Mult_Reg[0]=Ycoe1*Yin_Reg[0];
assign Mult_Reg[1]=Ycoe2*Yin_Reg[1];
assign Mult_Reg[2]=Ycoe3*Yin_Reg[2];
assign Mult_Reg[3]=Ycoe4*Yin_Reg[3];
assign Mult_Reg[4]=Ycoe5*Yin_Reg[4];
assign Mult_Reg[5]=Ycoe6*Yin_Reg[5];
assign Mult_Reg[6]=Ycoe7*Yin_Reg[6];

assign Yout=Mult_Reg[0]+Mult_Reg[1]+Mult_Reg[2]+Mult_Reg[3]+Mult_Reg[4]+Mult_Reg[5]+Mult_Reg[6];

endmodule


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

wire signed [11:0] Ycoe [7:0];
wire signed [23:0] Mult_Reg[6:0];

assign Ycoe[0]=12'd512;
assign Ycoe[1]=-12'd922;
assign Ycoe[2]=12'd1163;
assign Ycoe[3]=-12'd811;
assign Ycoe[4]=12'd412;
assign Ycoe[5]=-12'd122;
assign Ycoe[6]=12'd24;
assign Ycoe[7]=-12'd2;

assign Mult_Reg[0]=Ycoe[1]*Yin_Reg[0];
assign Mult_Reg[1]=Ycoe[2]*Yin_Reg[1];
assign Mult_Reg[2]=Ycoe[3]*Yin_Reg[2];
assign Mult_Reg[3]=Ycoe[4]*Yin_Reg[3];
assign Mult_Reg[4]=Ycoe[5]*Yin_Reg[4];
assign Mult_Reg[5]=Ycoe[6]*Yin_Reg[5];
assign Mult_Reg[6]=Ycoe[7]*Yin_Reg[6];

assign Yout=Mult_Reg[0]+Mult_Reg[1]+Mult_Reg[2]+Mult_Reg[3]+Mult_Reg[4]+Mult_Reg[5]+Mult_Reg[6];

endmodule


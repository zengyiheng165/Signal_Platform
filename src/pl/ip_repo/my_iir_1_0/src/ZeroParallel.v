`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
// Company: DMU
// Engineer: Zengyh
// 
// Create Date: 2026/07/21 11:37:04
// Design Name: IIR ZeroParallel
// Module Name: ZeroParallel
//////////////////////////////////////////////////////////////////////////////////


module ZeroParallel(
    input  wire  sys_rst,
    input  wire  sys_clk,
    input wire signed [11:0] Xcoe0,
    input wire signed [11:0] Xcoe1,
    input wire signed [11:0] Xcoe2,
    input wire signed [11:0] Xcoe3,
    input wire signed [11:0] Xcoe4,
    input wire signed [11:0] Xcoe5,
    input wire signed [11:0] Xcoe6,
    input wire signed [11:0] Xcoe7,
    
    input  wire  signed [11:0]  Xin,
    output wire  signed [20:0] Xout
    );
    
reg signed [11:0]  Xin_Reg[7:0];
reg [3:0]         i=0;
reg [3:0]         j=0;

always@(posedge sys_clk or negedge sys_rst)
    if(!sys_rst)begin
        for(i=0;i<8;i=i+1)
            Xin_Reg[i]<=12'd0;
    end
    else begin
         for(j=0;j<6;j=j+1)
            Xin_Reg[j+1]<=Xin_Reg[j];
          Xin_Reg[0]<=Xin;
    end

wire signed [12:0] Add_Reg[3:0];


/*
assign Add_Reg[0]=Xin_Reg[0]+Xin_Reg[7];
assign Add_Reg[1]=Xin_Reg[1]+Xin_Reg[6];
assign Add_Reg[2]=Xin_Reg[2]+Xin_Reg[5];
assign Add_Reg[3]=Xin_Reg[3]+Xin_Reg[4];
*/

wire signed [20:0] Mult_Reg[7:0];


assign Mult_Reg[0]=Xin_Reg[0]*Xcoe0; //*7
assign Mult_Reg[1]=Xin_Reg[1]*Xcoe1; //*21
assign Mult_Reg[2]=Xin_Reg[2]*Xcoe2; //*42
assign Mult_Reg[3]=Xin_Reg[3]*Xcoe3; //*56
assign Mult_Reg[4]=Xin_Reg[4]*Xcoe4; //*7
assign Mult_Reg[5]=Xin_Reg[5]*Xcoe5; //*21
assign Mult_Reg[6]=Xin_Reg[6]*Xcoe6; //*42
assign Mult_Reg[7]=Xin_Reg[7]*Xcoe7; //*56


assign Xout=Mult_Reg[0]+Mult_Reg[1]+Mult_Reg[2]+Mult_Reg[3]+Mult_Reg[4]+Mult_Reg[5]+Mult_Reg[6]+Mult_Reg[7];

endmodule

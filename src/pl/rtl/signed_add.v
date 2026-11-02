`timescale 1ns / 1ps
module signed_add(
    input wire         sys_clk,
    input wire         sys_rst_n,
    input wire  [13:0] signal1,
    input wire  [13:0] signal2,
    input wire  [13:0] signal3,
    input wire  [13:0] signal4,
    input wire  [13:0] signal5,
    
    output wire  [13:0] addsignal
    );
    
wire signed [14:0] signed_signal1={1'b0,signal1}-8191;

wire signed [14:0] signed_signal2={1'b0,signal2}-8191; 

wire signed [14:0] signed_signal3={1'b0,signal3}-8191; 

wire signed [14:0] signed_signal4={1'b0,signal4}-8191; 

wire signed [14:0] signed_signal5={1'b0,signal5}-8191; 

reg signed [14:0] signed_addsignal;

assign addsignal=(signed_addsignal[13:0]+8191);

always@(posedge sys_clk or negedge sys_rst_n) 
    begin
        if(!sys_rst_n)
        begin
            signed_addsignal<=0;
        end
        else
        begin
           signed_addsignal<=(signed_signal1+signed_signal2+signed_signal3+signed_signal4+signed_signal5); 
        end 
    end   
    
    
endmodule

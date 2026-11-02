`timescale 1ns / 1ps

module chaifen(
    input  [3:0]  input_data,
    
    output              output_data1,
    output             output_data2,
    output   [1:0]     output_data3
    );
    
assign output_data1=input_data[0];
assign output_data2=input_data[1];
assign output_data3[0]=input_data[2];
assign output_data3[1]=input_data[3];


endmodule

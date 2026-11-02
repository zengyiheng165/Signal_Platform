`timescale 1ns / 1ps

module wave_sel(
    input  [1:0]  sel,
    input  [13:0] data_1,
    input         data_1_c,
    input  [13:0] data_2,
    input         data_2_c,
    input  [13:0] data_3,
    input         data_3_c,
    input  [13:0] data_4,
    input         data_4_c,
    
    output  [13:0] output_data1,
    output          output_data_c
    );
    
assign output_data1=(sel==2'b00)?data_1:(sel==2'b01)?data_2:(sel==2'b10)?data_3:data_4;
assign output_data_c=(sel==2'b00)?data_1_c:(sel==2'b01)?data_2_c:(sel==2'b10)?data_3_c:data_4_c;


endmodule

`timescale 1ns / 1ps

module adc(
    input wire            ad_clk  ,
    input wire            sys_rst_n,
    input wire    [11:0]  ad_data,
    
    output wire          sign,
    output wire   signed [15:0] volt,
    output wire          clk_12M
    //output wire          ad_clk
    );

//reg    [10:0]   ad_cnt;
//reg    [17:0]   data_sum;
//reg             sum_en;
//wire   [11:0]    data_median;
//wire   [27:0]   data_n;
//wire   [27:0]   data_p;
reg  signed  [15:0]   volt_reg;
//wire   [15:0] volt;
          
localparam [11:0]  data_median=12'd2047;

//localparam data_n=20000;

//localparam data_p=20019;
/*
always @(posedge ad_clk or negedge sys_rst_n) 
    if(!sys_rst_n)
        volt_reg<=28'd0;
    else
        if(ad_data < data_median)
           volt_reg<=(data_n*(data_median-ad_data)>>13); 
        else if(ad_data > data_median)
           volt_reg<=(data_p*(ad_data-data_median)>>13);
    else
         volt_reg<=0;
*/     

 always @(posedge ad_clk or negedge sys_rst_n) 
    if(!sys_rst_n)
        volt_reg<=28'd0;
    else begin
           volt_reg<=ad_data-2047; 
   end 
       
assign   clk_12M =~ad_clk;     

assign   volt=volt_reg[15:0];

assign   sign=(ad_data >= data_median)?1'b0:1'b1;

endmodule

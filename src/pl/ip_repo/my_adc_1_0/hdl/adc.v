`timescale 1ns / 1ps

module adc(
    input wire            sys_clk  ,
    input wire            sys_rst_n,
    input wire    [11:0]  ad_data,
    
    //output wire          sign,
    //output wire   [15:0] volt,
    output wire          clk_12M,
    output wire          ad_clk
    );

//reg    [10:0]   ad_cnt;
//reg    [17:0]   data_sum;
//reg             sum_en;
wire   [11:0]    data_median;
wire   [27:0]   data_n;
wire   [27:0]   data_p;
reg    [27:0]   volt_reg;
wire            locked;
//wire          ad_clk;
wire   [15:0] volt;
          
assign data_median=2047;

assign data_n=8192_0000/((data_median)*2);

assign data_p=8192_0000/((4095-data_median)*2);

always @(posedge ad_clk or negedge sys_rst_n) 
    if(!sys_rst_n || !locked)
        volt_reg<=28'd0;
    else
        if(ad_data < data_median)
           volt_reg<=(data_n*(data_median-ad_data)>>13); //+1 YES OR NO
        else if(ad_data > data_median)
           volt_reg<=(data_p*(ad_data-data_median)>>13);
  //  else
         //volt_reg<=0;
         
assign   clk_12M =~ad_clk;     

assign   volt=volt_reg;

assign   sign=(ad_data >= data_median)?1'b0:1'b1;

pll pll_inst
(
    // Clock out ports
    .clk_out1(ad_clk),     // output clk_out1
    // Status and control signals
    .reset(~sys_rst_n), // input reset
   // Clock in ports
    .locked(locked),  
    .clk_in1(sys_clk)      // input clk_in1
);

ila_0 ila_0_inst (
	.clk(sys_clk), // input wire clk
	
	.probe0(volt), // input wire [15:0]  probe0  
	.probe1(data_median), // input wire [7:0]  probe1
	.probe2(ad_data)
);
endmodule

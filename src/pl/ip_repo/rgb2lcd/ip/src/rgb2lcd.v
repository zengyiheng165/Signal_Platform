module rgb2lcd #(

)(
    //VID_OUT
    input  [23:0]                 rgb_data ,
    input                         rgb_vde  ,
    input                         rgb_hsync,
    input                         rgb_vsync,

    input                         pixel_clk,
   
    //RGB LCD
    output                        lcd_pclk ,
    output                        lcd_hs   ,
    output                        lcd_vs   ,
    output                        lcd_de   ,
    output                        lcd_bl   ,
    output  [15:0]                lcd_data
);

//*****************************************************
//**                  main code
//*****************************************************

//LCDÐÅºÅ¸³Öµ
assign lcd_pclk = pixel_clk;
assign lcd_de = rgb_vde  ;
assign lcd_hs = rgb_hsync;
assign lcd_vs = rgb_vsync;
assign lcd_bl = 1'b1;
assign lcd_rst = 1'b1;

assign lcd_data[15:11] = rgb_data[15:11];
assign lcd_data[10:5] = rgb_data[10:5];
assign lcd_data[4:0] = rgb_data[4:0];

endmodule

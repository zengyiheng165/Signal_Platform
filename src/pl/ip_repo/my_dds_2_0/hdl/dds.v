
module dds(
    input  wire          sys_clk,   
    input  wire          sys_rst_n, 
    input  wire   [3:0]  wave_sel, 
    input  wire   [31:0] F_WORD,
    input  wire          wave_en,
    input  wire   [7:0]  amplitude,
    input  wire   [11:0]  P_WORD,
  
    output reg   [13:0]  dac_data_1,
    output wire          dac_clk,
    output wire          WRT                                
);    


//parameter P_WORD=12'd1024;

reg  [23:0]  mult_stage1;
reg  [31:0]  mult_stage2;
reg  [31:0]  fre_add;
reg  [11:0]  rom_addr_reg;
reg  [13:0]  rom_addr;
reg  [13:0]  rom_dout_r;   // amplitude pipeline S1: latch ROM output
reg  [7:0]   amp_r;        // amplitude pipeline S1: latch amplitude (aligned to sample)
reg  [23:0]  mult_tmp;     // amplitude pipeline S2: (sample-8191)*amplitude
wire  [15:0]  dac_data_mid_reg;
wire  [13:0]   dac_data_mid;

assign dac_clk=~sys_clk;

assign WRT=dac_clk;

always@(posedge sys_clk or negedge sys_rst_n)
    if(sys_rst_n==0 || wave_en==0) 
        fre_add<=32'd0;
    else
        fre_add<=fre_add+F_WORD;
        
always@(posedge sys_clk or negedge sys_rst_n)
    if(sys_rst_n==0 || wave_en==0)
        rom_addr_reg<=12'd0;
    else
        rom_addr_reg<=fre_add[31:20]+P_WORD;
        
always@(posedge sys_clk or negedge sys_rst_n)
      if(sys_rst_n==0 || wave_en==0)
            rom_addr<=14'd0;
      else
           case(wave_sel)
                4'b0001:rom_addr<=rom_addr_reg;
                4'b0010:rom_addr<=rom_addr_reg+4096;
                4'b0100:rom_addr<=rom_addr_reg+8192;
                4'b1000:rom_addr<=rom_addr_reg+12288;
           default:rom_addr<=0;
           endcase
//===========================================================================
// Amplitude scaling - pipelined for timing closure.
//
// Functional equivalence to the previous implementation:
//   old branch A: mult_stage1 = (dac_data_mid - 8191)*amplitude + 1048576
//   old branch B: mult_stage1 = 1048576 - (8191 - dac_data_mid)*amplitude
//   Branch B expands algebraically to exactly branch A
//   (1048576 + (dac_data_mid - 8191)*amplitude), so under the 24-bit
//   wrap-around (mod 2^24) of the original unsigned arithmetic both branches
//   always produce the identical value. The branch decision
//   (mult_stage1 >= 8192) is therefore redundant and is removed together
//   with its duplicate 24-bit arithmetic and 2:1 mux.
//   The remaining subtract->multiply->add chain is split over two registered
//   stages so that no single clock cycle contains the whole path:
//       S1: capture ROM output   (breaks the BRAM douta -> multiplier path)
//       S2: (sample - 8191) * amplitude                 (mod 2^24)
//       S3: + 1048576 (DC center offset 8192*128)       (mod 2^24)
//       S4: >> 7  (pure shift, divide by 128)
//       S5: saturate to 14-bit DAC output
//   The output stream is delayed by two extra sys_clk cycles (uniform
//   latency; harmless for a free-running DDS/DAC sample stream).
//===========================================================================
always @(posedge sys_clk or negedge sys_rst_n) begin
    if (!sys_rst_n) begin
        rom_dout_r  <= 14'd0;
        amp_r       <= 8'd0;
        mult_tmp    <= 24'd0;
        mult_stage1 <= 24'd0;
        mult_stage2 <= 32'd0;
        dac_data_1  <= 14'd0;
    end else begin
        // S1: register the ROM sample and the amplitude together
        rom_dout_r <= dac_data_mid;
        amp_r      <= amplitude;
        // S2: difference vs. DC centre (8191) scaled by amplitude, mod 2^24
        mult_tmp   <= (rom_dout_r - 8191) * amp_r;
        // S3: re-insert the DC centre offset 1048576 (= 8192*128), mod 2^24
        mult_stage1 <= mult_tmp + 1048576;
        // S4: divide by 128 (logical shift of the 24-bit value)
        mult_stage2 <= mult_stage1 >> 7;
        // S5: saturate to 14-bit DAC output
        if (mult_stage2 > 32'd16383)
            dac_data_1 <= 14'd16383;
        else
            dac_data_1 <= mult_stage2[13:0];
    end
end
rom_wave rom_wave_inst (
  .clka(sys_clk),                   // input wire clka
  .addra(rom_addr),                 // input wire [13 : 0] addra
  .douta(dac_data_mid)  // output wire [7 : 0] douta
);
endmodule

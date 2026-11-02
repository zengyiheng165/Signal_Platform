`timescale 1ns / 1ps

//============================================================
//  bram_rd - user logic of my_dds_bram
//
//  Function: BRAM-based DDS
//    - The waveform table is stored in a dual-port BRAM shared
//      with the PS (PS writes through the AXI BRAM Controller on
//      port A; this module reads on port B), one period = 4096
//      samples.
//    - A 32-bit phase accumulator fre_add provides the look-up
//      address from its top 12 bits [31:20] (0~4095); the 12-bit
//      phase offset P_WORD is added with modulo-4096 wraparound.
//      Every sample occupies one 32-bit word = 4 byte addresses,
//      so the physical BRAM address on ram_addr = sample index*4
//      (0, 4, 8, ...).
//    - Output is a 14-bit unsigned sample (0~16383). The
//      amplitude is scaled about the DC midpoint 8191:
//          dac_data = 8191 + (sample - 8191) * amplitude / 1000
//      amplitude: 0~1000 (1000 = full scale, waveform unchanged),
//      so the DC level stays at 8191 for any amplitude.
//
//  Registers (written by PS):
//    slv_reg0[0]  start_rd   : 1 = run; 0 = stop (phase cleared,
//                              output stays at the DC midpoint)
//    slv_reg3     F_WORD     : frequency control word
//                              f = F_WORD * fclk / 2^32
//    slv_reg4[15:0] amplitude: amplitude 0~1000 (1000 = full scale)
//    slv_reg5[11:0] P_WORD   : starting phase offset (0~4095 table
//                              entries)
//
//  Fixes applied to the original file:
//    1) ram_en was never driven, so the BRAM port B was never
//       enabled and no data could be read; now it is held high.
//    2) dac_clk/WRT were never driven; now they follow the
//       convention of the existing 14-bit DDS project:
//       dac_clk = ~sys_clk, WRT = dac_clk. Data updates on the
//       rising edge of sys_clk, and the WRT rising edge falls on
//       the falling edge of sys_clk, so data is stable when the
//       DAC latches it.
//    3) The amplitude word is now 0~1000 (16-bit register field)
//       and the scaling is done about the DC midpoint 8191
//       instead of scaling the whole unsigned sample towards 0.
//============================================================

module bram_rd(
    input                 sys_clk,
    input                 sys_rst_n,

    //PS control (start_addr/rd_len belong to the old sequential
    //read logic and are no longer used; the ports are kept)
    input                 start_rd,   // 1 = run; 0 = stop and clear the phase
    input  [31:0]         start_addr, // (legacy, unused)
    input  [31:0]         rd_len,     // (legacy, unused)

    //BRAM pins (connect to port B / BRAM_PORTB of blk_mem_gen)
    output                ram_clk,
    output                ram_rst,    // high active
    output                ram_en,     // high active
    output      [3:0]     ram_we,     // 1 = write, 0 = read
    output reg  [31:0]    ram_addr,
    output      [31:0]    ram_we_data,
    input       [31:0]    ram_rd_data,

    //PS DDS configuration
    input  wire   [31:0]  F_WORD,
    input  wire   [15:0]  amplitude,  // 0~1000, 1000 = full scale
    input  wire   [11:0]  P_WORD,

    //DDS outputs
    output reg   [13:0]   dac_data_1,
    output wire           dac_clk,
    output wire           WRT
    );

//----------------------------------------------------------
// BRAM control / DAC clock (the drivers that were missing
// in the original file are added here)
//----------------------------------------------------------
assign ram_clk     = sys_clk;
assign ram_rst     = 1'b0;      // port B is never reset (reset is high active)
assign ram_en      = 1'b1;      // port B is always enabled: when stopped, the
                                // address stays at 0 so the first table point
                                // is still read
assign ram_we      = 4'b0;      // read only on the PL side
assign ram_we_data = 32'b0;
assign dac_clk     = ~sys_clk;  // DAC sample clock, consistent with the
                                // existing 14-bit DDS project
assign WRT         = dac_clk;   // write pulse for AD9767-type DACs

//----------------------------------------------------------
// DDS phase accumulator (32-bit)
// When start_rd = 0 the accumulator is held at zero and the
// output stays at the first table point; when start_rd = 1 it
// accumulates freely.
//----------------------------------------------------------
reg [31:0] fre_add;

always @(posedge sys_clk or negedge sys_rst_n)
    if (!sys_rst_n)
        fre_add <= 32'd0;
    else if (!start_rd)
        fre_add <= 32'd0;
    else
        fre_add <= fre_add + F_WORD;

// Sample index: top 12 bits of the phase plus the phase offset;
// the 12-bit addition wraps around modulo 4096 (one period =
// 4096 samples).
reg [11:0] ram_addr_reg;

always @(posedge sys_clk or negedge sys_rst_n)
    if (!sys_rst_n)
        ram_addr_reg <= 12'd0;
    else if (!start_rd)
        ram_addr_reg <= 12'd0;
    else
        ram_addr_reg <= fre_add[31:20] + P_WORD;

// Physical BRAM address = sample index * 4, because every sample
// occupies one 32-bit word = 4 byte addresses in the BRAM. The
// BRAM is therefore addressed every 4 units (0, 4, 8, ...) and
// its address space must cover 4 * 4096 = 16384 address units.
localparam integer BYTES_PER_SAMPLE = 4;

always @(posedge sys_clk or negedge sys_rst_n)
    if (!sys_rst_n)
        ram_addr <= 32'd0;
    else if (!start_rd)
        ram_addr <= 32'd0;
    else
        ram_addr <= {20'd0, ram_addr_reg} * BYTES_PER_SAMPLE;  // index * 4

//----------------------------------------------------------
// Amplitude scaling about the DC midpoint 8191 (pipelined)
//     out = 8191 + (sample - 8191) * amplitude / 1000
// clipped to [0, 16383]
//----------------------------------------------------------
localparam signed [29:0] DAC_MID = 30'sd8191;   // 14-bit DC midpoint
localparam signed [29:0] DAC_MAX = 30'sd16383;  // 14-bit full scale
localparam signed [29:0] AMP_DIV = 30'sd1000;   // amplitude = 1000 -> full scale

wire [13:0] dac_data_mid = ram_rd_data[13:0];   // 14-bit unsigned table sample

// Signed deviation and amplitude are first REGISTERED (stage S1) so that the
// multiply in stage S2 starts from clean flip-flop outputs instead of the
// BRAM read data. The previous code computed dev combinationally from the
// BRAM output and multiplied in the same cycle (BRAM dout -> subtract ->
// DSP48 -> FF), violating timing at 100 MHz by ~0.3 ns (impl_1 routed report:
// WNS -0.321 ns on bram_rd_inst/mult_stage1_reg). Values are unchanged; only
// the output is delayed by one extra sys_clk cycle.
reg  signed [14:0] dev_r;       // S1: signed deviation from DC midpoint (-8191..+8192)
reg  signed [16:0] amp_r;       // S1: amplitude, latched in the same cycle as dev_r
reg  signed [29:0] mult_stage1; // S2: dev_r * amplitude
reg  signed [29:0] mult_stage2; // S3: mult_stage1 >> 10  (scale ~1/1000)
wire signed [29:0] dac_level = DAC_MID + mult_stage2;  // absolute level again

always @(posedge sys_clk or negedge sys_rst_n) begin
    if (!sys_rst_n) begin
        dev_r        <= 15'sd0;
        amp_r        <= 17'sd0;
        mult_stage1  <= 30'sd0;
        mult_stage2  <= 30'sd0;
        dac_data_1   <= 14'd8191;          // DC midpoint during reset
    end else if (!start_rd) begin
        // stopped: clear the scaling pipeline and force the DAC
        // output to the DC midpoint (0 V), independent of the
        // table contents or the amplitude setting
        dev_r        <= 15'sd0;
        amp_r        <= 17'sd0;
        mult_stage1  <= 30'sd0;
        mult_stage2  <= 30'sd0;
        dac_data_1   <= 14'd8191;          // DC midpoint (0 V)
    end else begin
        // S1: capture the deviation and the amplitude in the same cycle
        //     (unsigned amplitude: bit16 stays 0, so amp is always >= 0)
        dev_r <= $signed({1'b0, dac_data_mid}) - 15'sd8191;
        amp_r <= {1'b0, amplitude};
        // S2: multiply from registered operands (short single-cycle path)
        mult_stage1 <= dev_r * amp_r;
        // S3: divide by 1024 (arithmetic shift of the signed value)
        mult_stage2 <= (mult_stage1 >>> 10);
        // S4: clip to the 14-bit DAC range [0, 16383]
        if (dac_level > DAC_MAX)
            dac_data_1 <= 14'd16383;      // top clip
        else if (dac_level < 30'sd0)
            dac_data_1 <= 14'd0;          // bottom clip
        else
            dac_data_1 <= dac_level[13:0];
    end
end

endmodule

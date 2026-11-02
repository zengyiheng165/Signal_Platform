`timescale 1ns / 1ps

module fft_init(
    input   wire     sys_clk,
    input   wire     sys_rst_n,
    input   wire     m_axis_config_tready,
    
    output  reg          m_axis_config_tvalid,
    output  reg  [7:0]  m_axis_config_tdata
    );
    
localparam IDLE = 0, SEND_CONFIG = 1, WAIT_GAP = 2; 

reg [2:0]  state;
//reg [31:0] config_data;

always @(posedge sys_clk or negedge sys_rst_n) begin
  if (!sys_rst_n) 
  begin
    state <= IDLE;
    m_axis_config_tvalid <= 1'b0;
    m_axis_config_tdata <= 32'b0;
  end else begin
    case (state)
      IDLE: begin
        m_axis_config_tvalid <= 1'b1;
       // config_data <= 32'h05555501; 
        m_axis_config_tdata <= 8'b1;
        state <= SEND_CONFIG;
      end
      SEND_CONFIG: begin
        if (m_axis_config_tready) begin 
          m_axis_config_tvalid <= 1'b0;
          state <= WAIT_GAP;
        end
      end
      WAIT_GAP: begin 
           m_axis_config_tvalid <= 1'b0;
      end
    endcase
  end
end
 
endmodule

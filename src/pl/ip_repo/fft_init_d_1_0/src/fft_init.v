`timescale 1ns / 1ps

module fft_init(
    input   wire           sys_clk,
    input   wire           sys_rst_n,
    input   wire           m_axis_config_tready,
    input   wire   [31:0]  point_data,
    input   wire           fft_flag,
    
    output  reg            m_axis_config_tvalid,
    output  reg    [15:0]  m_axis_config_tdata
    );
    
localparam IDLE = 0, SEND_CONFIG = 1, WAIT_GAP = 2; 

reg [2:0]  state;
reg [2:0]  cnt;
wire [15:0]  m_axis_config_tdata_mid;
//reg [31:0] config_data;

assign  m_axis_config_tdata_mid={7'b0,point_data[5],3'b0,point_data[4:0]};

always @(posedge sys_clk or negedge sys_rst_n) begin
  if (!sys_rst_n) 
  begin
    state <= IDLE;
    m_axis_config_tvalid <= 1'b0;
    m_axis_config_tdata <= 16'h0;
  end else begin
    case (state)
      IDLE: begin
      if(m_axis_config_tdata_mid!=m_axis_config_tdata)
          begin
            m_axis_config_tvalid <= 1'b1;
           // config_data <= 32'h05555501; 
            m_axis_config_tdata <= m_axis_config_tdata_mid;
            state <= SEND_CONFIG;
          end
     else
        begin
            m_axis_config_tvalid <= 1'b0;
          end
      end
      SEND_CONFIG: begin
        if (m_axis_config_tready) begin 
          m_axis_config_tvalid <= 1'b0;
          state <= WAIT_GAP;
        end
      end
      WAIT_GAP: begin
       if(cnt<4) begin
           m_axis_config_tvalid <= 1'b0;
           cnt<=cnt+1;
       end
       else begin
           m_axis_config_tvalid <= 1'b0;
           cnt<=0;
           state<=IDLE;
         end
      end
    endcase
  end
end
 
endmodule

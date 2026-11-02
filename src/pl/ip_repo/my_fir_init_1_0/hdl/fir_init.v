`timescale 1ns / 1ps

module fft_init(
    input   wire               sys_clk,
    input   wire               sys_rst_n,
    input   wire  [11:0]       cofficients1,
    input   wire  [11:0]       cofficients2,
    input   wire  [11:0]       cofficients3,
    input   wire  [11:0]       cofficients4,
    input   wire  [11:0]       cofficients5,
    input   wire  [11:0]       cofficients6,
    input   wire  [11:0]       cofficients7,
    input   wire  [11:0]       cofficients8,
    input   wire  [11:0]       cofficients9,
    input   wire  [11:0]       cofficients10,
    
    input   wire               reload_en,
    
    output	reg		       m_axis_config_tvalid   ,
	input	wire		   m_axis_config_tready   ,
	output	reg    [7:0]   m_axis_config_tdata    ,
                               
    output 	reg		       m_axis_reload_tvalid   ,
	input 	wire		   m_axis_reload_tready   ,
	output 	reg		       m_axis_reload_tlast    ,
	output  reg	 [11:0]	   m_axis_reload_tdata    
    );
    
localparam IDLE = 0, SEND_STREAM = 1, END = 2; 

reg  [2:0]   state;
reg  [4:0]   cnt_stream;
//reg  [319:0] cofficients_mid; 
reg          reaload_en_delay;


parameter  [7:0]  COFFICIENTS_NUMBER=10;

always @(posedge sys_clk or negedge sys_rst_n)begin
    if (!sys_rst_n) 
        cnt_stream<=0;
    else
        if(m_axis_reload_tvalid==1)
            cnt_stream<=cnt_stream+1;
        else
            cnt_stream<=0;
end

always @(posedge sys_clk or negedge sys_rst_n)begin
    if (!sys_rst_n) 
        reaload_en_delay<=0;
    else
        reaload_en_delay<=reload_en;
end

always @(posedge sys_clk or negedge sys_rst_n) begin
  if (!sys_rst_n) 
  begin
    state <= IDLE;
    m_axis_config_tvalid <= 1'b0;
    m_axis_config_tdata <= 8'b0;
    m_axis_reload_tdata <= 319'b0;
    m_axis_reload_tlast <= 1'b0;
    m_axis_reload_tvalid<= 1'b0;
  end else begin
    case (state)
      IDLE: begin
      if(reload_en && (!reaload_en_delay) && m_axis_reload_tready)
          begin
            m_axis_reload_tvalid <= 1'b1;
           // config_data <= 32'h05555501; 
           //cofficients_mid <= cofficients;
            state <= SEND_STREAM;
          end
     else
        begin
            m_axis_reload_tdata <= 1'b0;
          end
      end
      SEND_STREAM: begin
        if (cnt_stream < COFFICIENTS_NUMBER-1) begin 
          case(cnt_stream)
          0:
          m_axis_reload_tdata<=cofficients1;
          1:
          m_axis_reload_tdata<=cofficients2;
          2:
          m_axis_reload_tdata<=cofficients3;
          3:
          m_axis_reload_tdata<=cofficients4;
          4:
          m_axis_reload_tdata<=cofficients5;
          5:
          m_axis_reload_tdata<=cofficients6;
          6:
          m_axis_reload_tdata<=cofficients7;
          7:
          m_axis_reload_tdata<=cofficients8;
          8:
          m_axis_reload_tdata<=cofficients9;
          endcase
        end
        else if(cnt_stream==COFFICIENTS_NUMBER-1) begin
           m_axis_reload_tdata<=cofficients10;
           m_axis_reload_tlast<=1'b1;
           end
           else begin
           m_axis_reload_tlast<=1'b0; 
           m_axis_reload_tvalid<=1'b0;
           m_axis_config_tvalid<=1'b1;
           state <= END;
           end      
      end
      END: begin
            m_axis_config_tvalid<=0;
      end
    endcase
  end
end
 
endmodule
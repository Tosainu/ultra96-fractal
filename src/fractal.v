module fractal #(
  localparam integer S_AXI_DATA_WIDTH = 32,
  localparam integer S_AXI_ADDR_WIDTH = 4,
  localparam integer M_AXIS_TDATA_WIDTH = 8
) (
  input  wire aclk,
  input  wire aresetn,

  input  wire [S_AXI_ADDR_WIDTH - 1:0] s_axi_awaddr,
  input  wire [2:0] s_axi_awprot,
  input  wire s_axi_awvalid,
  output wire s_axi_awready,
  input  wire [S_AXI_DATA_WIDTH - 1:0] s_axi_wdata,
  input  wire [(S_AXI_DATA_WIDTH / 8) - 1:0] s_axi_wstrb,
  input  wire s_axi_wvalid,
  output wire s_axi_wready,
  output wire [1:0] s_axi_bresp,
  output wire s_axi_bvalid,
  input  wire s_axi_bready,
  input  wire [S_AXI_ADDR_WIDTH - 1:0] s_axi_araddr,
  input  wire [2:0] s_axi_arprot,
  input  wire s_axi_arvalid,
  output wire s_axi_arready,
  output wire [S_AXI_DATA_WIDTH - 1:0] s_axi_rdata,
  output wire [1:0] s_axi_rresp,
  output wire s_axi_rvalid,
  input  wire s_axi_rready,

  output wire m_axis_tvalid,
  output wire [M_AXIS_TDATA_WIDTH - 1:0] m_axis_tdata,
  output wire [(M_AXIS_TDATA_WIDTH / 8) - 1:0] m_axis_tstrb,
  output wire [0:0] m_axis_tuser,
  output wire m_axis_tlast,
  input  wire m_axis_tready
);

endmodule

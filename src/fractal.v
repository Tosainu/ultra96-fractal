module fractal #(
  localparam integer S_AXI_DATA_WIDTH = 32,
  localparam integer S_AXI_ADDR_WIDTH = 6,
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

wire [((2**S_AXI_ADDR_WIDTH) * 8) - 1:0] registers;

fractal_axi #(
  .S_AXI_DATA_WIDTH(S_AXI_DATA_WIDTH),
  .S_AXI_ADDR_WIDTH(S_AXI_ADDR_WIDTH)
) s_axi(
  .registers(registers),
  .S_AXI_ACLK(aclk),
  .S_AXI_ARESETN(aresetn),
  .S_AXI_AWADDR(s_axi_awaddr),
  .S_AXI_AWPROT(s_axi_awprot),
  .S_AXI_AWVALID(s_axi_awvalid),
  .S_AXI_AWREADY(s_axi_awready),
  .S_AXI_WDATA(s_axi_wdata),
  .S_AXI_WSTRB(s_axi_wstrb),
  .S_AXI_WVALID(s_axi_wvalid),
  .S_AXI_WREADY(s_axi_wready),
  .S_AXI_BRESP(s_axi_bresp),
  .S_AXI_BVALID(s_axi_bvalid),
  .S_AXI_BREADY(s_axi_bready),
  .S_AXI_ARADDR(s_axi_araddr),
  .S_AXI_ARPROT(s_axi_arprot),
  .S_AXI_ARVALID(s_axi_arvalid),
  .S_AXI_ARREADY(s_axi_arready),
  .S_AXI_RDATA(s_axi_rdata),
  .S_AXI_RRESP(s_axi_rresp),
  .S_AXI_RVALID(s_axi_rvalid),
  .S_AXI_RREADY(s_axi_rready)
);

wire  [7:0] generator_ctrl = registers[0+:8];
wire signed [31:0] generator_x0 = registers[('h10 * 8)+:32];
wire signed [31:0] generator_y0 = registers[('h18 * 8)+:32];
wire signed [31:0] generator_dx = registers[('h20 * 8)+:32];
wire signed [31:0] generator_dy = registers[('h28 * 8)+:32];
wire signed [31:0] generator_cr = registers[('h30 * 8)+:32];
wire signed [31:0] generator_ci = registers[('h38 * 8)+:32];

wire generator_resetn = aresetn && generator_ctrl[0];

fractal_generator #(
) generator(
  .clk(aclk),
  .resetn(generator_resetn),
  .width(16'd1920),  // TODO:
  .height(16'd1080), // TODO:
  .cr(generator_cr),
  .ci(generator_ci),
  .dx(generator_dx),
  .dy(generator_dy),
  .x0(generator_x0),
  .y0(generator_y0),
  .data(m_axis_tdata),
  .frame_start(m_axis_tuser),
  .line_end(m_axis_tlast),
  .data_enable(m_axis_tvalid)
);

assign m_axis_tstrb = {(M_AXIS_TDATA_WIDTH / 8){1'b1}};

endmodule

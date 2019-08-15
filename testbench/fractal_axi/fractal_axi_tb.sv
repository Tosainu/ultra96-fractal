`timescale 1ns / 1ps

import axi_vip_pkg::*;
import fractal_axi_bd_axi_vip_0_0_pkg::*;

module fractal_axi_tb();

bit aclk = 0, aresetn = 0;

wire [127:0] registers;

fractal_axi_bd_wrapper u0(
  .aclk(aclk),
  .aresetn(aresetn),
  .registers(registers)
);

always #10ns aclk = ~aclk;

xil_axi_resp_t  resp;
fractal_axi_bd_axi_vip_0_0_mst_t agent;

bit [31:0] data;

initial begin
  agent = new("agent", fractal_axi_tb.u0.fractal_axi_bd_i.axi_vip_0.inst.IF);
  agent.start_master();

  #100ns aresetn = 1;

  #200ns;

  agent.AXI4LITE_WRITE_BURST(32'h00, 0, 32'hf1f2f3f4, resp);
  agent.AXI4LITE_WRITE_BURST(32'h04, 0, 32'hf5f6f7f8, resp);
  agent.AXI4LITE_WRITE_BURST(32'h08, 0, 32'hf9fafbfc, resp);
  agent.AXI4LITE_WRITE_BURST(32'h0c, 0, 32'hfdfefff0, resp);

  #200ns;

  agent.AXI4LITE_READ_BURST(32'h0c, 0, data, resp);
  #40ns;
  agent.AXI4LITE_READ_BURST(32'h08, 0, data, resp);
  #40ns;
  agent.AXI4LITE_READ_BURST(32'h04, 0, data, resp);
  #40ns;
  agent.AXI4LITE_READ_BURST(32'h00, 0, data, resp);

  #40ns $finish();
end

endmodule

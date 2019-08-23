`timescale 1ns / 1ps

import axi_vip_pkg::*;
import fractal_all_bd_axi_vip_0_0_pkg::*;

module fractal_all_tb();

bit aclk = 0, aresetn = 0;

bit tlast;
bit tvalid;
bit tuser;

logic [23:0] tdata;


fractal_all_bd_wrapper u0(
  .aclk(aclk),
  .aresetn(aresetn),
  .m_axis_tdata(tdata),
  .m_axis_tlast(tlast),
  .m_axis_tuser(tuser),
  .m_axis_tvalid(tvalid)
);

always #2500ps aclk = ~aclk;

xil_axi_resp_t  resp;
fractal_all_bd_axi_vip_0_0_mst_t agent;

initial begin
  agent = new("agent", fractal_all_tb.u0.fractal_all_bd_i.axi_vip_0.inst.IF);
  agent.start_master();

  #100ns aresetn = 1;

  #200ns;

  agent.AXI4LITE_WRITE_BURST(32'h10, 0, 32'h10000000, resp); // x0
  agent.AXI4LITE_WRITE_BURST(32'h18, 0, 32'h09000000, resp); // y0
  agent.AXI4LITE_WRITE_BURST(32'h20, 0, 32'h00155555, resp); // dx
  agent.AXI4LITE_WRITE_BURST(32'h28, 0, 32'h00155555, resp); // dy
  agent.AXI4LITE_WRITE_BURST(32'h30, 0, 32'hf9999999, resp); // cr
  agent.AXI4LITE_WRITE_BURST(32'h38, 0, 32'h09999999, resp); // ci

  agent.AXI4LITE_WRITE_BURST(32'h00, 0, 32'h00000700, resp); // ctrl

  #200ns;

  agent.AXI4LITE_WRITE_BURST(32'h00, 0, 32'h00000701, resp); // ctrl

  wait (tuser == 1'b1);
  #100ns;

  wait (tuser == 1'b1);
  #20ns aresetn = 0;

  #100ns $finish();
end

integer file;
integer img_writing = 1, img_start = 0;
initial begin
  file = $fopen("out.ppm", "w");
  $fwrite(file, "P6\n");
  $fwrite(file, "%0d %0d\n", 384, 216);
  $fwrite(file, "%0d\n", 2**8-1);

  while (img_writing == 1) begin
    @(posedge aclk)
    #1ns;
    if (img_start == 1 && tuser == 1'b1)
      img_writing = 0;
    else begin
      if (tuser == 1'b1)
        img_start = 1;

      if (img_start == 1 && tvalid == 1'b1)
        $fwrite(file, "%c%c%c", tdata[16+:8], tdata[0+:8], tdata[8+:8]);
    end
  end

  $fclose(file);
  $display("Image written");
end

endmodule

`timescale 1ns / 1ps

module fractal_generator_tb();

bit clk = 0;
bit resetn = 0;

bit frame_start;
bit line_end;
bit data_enable;

logic [15:0] width = 384;
logic [15:0] height = 216;

logic signed [31:0] cr = 32'hf9999999; // 0.4
logic signed [31:0] ci = 32'h09999999; // -0.6
logic signed [31:0] dx = 32'h00155555;
logic signed [31:0] dy = 32'h00155555;
logic signed [31:0] x0 = 32'h10000000;
logic signed [31:0] y0 = 32'h09000000;

logic [7:0] data;

fractal_generator u_0(
  .clk(clk),
  .resetn(resetn),
  .width(width),
  .height(height),
  .cr(cr),
  .ci(ci),
  .dx(dx),
  .dy(dy),
  .x0(x0),
  .y0(y0),
  .data(data),
  .frame_start(frame_start),
  .line_end(line_end),
  .data_enable(data_enable)
);

always #2500ps clk = ~clk;

initial begin
  #150ns resetn = 1;

  wait (frame_start == 1'b1);
  #100ns;

  width = width + width;
  height = height + height;
  x0 = x0 + dx * 20;
  y0 = y0 + dy * 20;

  wait (frame_start == 1'b1);
  #20ns resetn = 0;

  #100ns $finish();
end

integer file;
integer img_writing = 1, img_start = 0;
initial begin
  file = $fopen("out.pgm", "w");
  $fwrite(file, "P5\n");
  $fwrite(file, "%0d %0d\n", width, height);
  $fwrite(file, "%0d\n", 2**8-1);

  while (img_writing == 1) begin
    @(posedge clk)
    #1ns;
    if (img_start == 1 && frame_start == 1'b1)
      img_writing = 0;
    else begin
      if (frame_start == 1'b1)
        img_start = 1;

      if (img_start == 1 && data_enable == 1'b1)
        $fwrite(file, "%c", data);
    end
  end

  $fclose(file);
  $display("Image written");
end

endmodule

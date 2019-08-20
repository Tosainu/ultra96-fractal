`timescale 1ns / 1ps

module fractal_colorizer_tb();

bit clk = 0, resetn = 0;

bit frame_start_in = 'b0;
bit line_end_in = 'b0;
bit data_enable_in = 'b0;
logic [7:0] data_in = 'h7f;

wire frame_start_out;
wire line_end_out;
wire data_enable_out;
wire [23:0] data_out;

bit [3:0] mode = 'h7;

fractal_colorizer u_0(
  .clk(clk),
  .resetn(resetn),
  .mode(mode),
  .data_in(data_in),
  .frame_start_in(frame_start_in),
  .line_end_in(line_end_in),
  .data_enable_in(data_enable_in),
  .data_out(data_out),
  .frame_start_out(frame_start_out),
  .line_end_out(line_end_out),
  .data_enable_out(data_enable_out)
);

always #5ns clk = ~clk;

initial begin
  #5ns;
  while (1) begin
    for (integer i = 0; i < 16; i++) begin
      if (data_in >= 'h7f)
        #20ns data_in = 'b0;
      else
        #20ns data_in = data_in + 'h2;
    end
  end
end

initial begin
  resetn = 1;

  #45ns frame_start_in = 'b1;
  #10ns frame_start_in = 'b0;

  #100ns $finish();
end

endmodule

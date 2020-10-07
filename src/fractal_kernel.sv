module fractal_kernel #(
  parameter integer MUL_PIPELINE_DEPTH = 7,
  parameter integer INPUT_DATA_WIDTH = 32,
  parameter integer INPUT_FRACTIONAL_WIDTH = 28
) (
  input         clk,

  input         inc_enabled,

  input  signed [INPUT_DATA_WIDTH - 1:0] zr_in,
  input  signed [INPUT_DATA_WIDTH - 1:0] zi_in,
  input  signed [INPUT_DATA_WIDTH - 1:0] cr_in,
  input  signed [INPUT_DATA_WIDTH - 1:0] ci_in,
  input   [7:0] iter_in,
  input         finished_in,

  output signed [INPUT_DATA_WIDTH - 1:0] zr_out,
  output signed [INPUT_DATA_WIDTH - 1:0] zi_out,
  output  [7:0] iter_out,
  output        finished_out
);

localparam PIPELINE_DEPTH = 1 + MUL_PIPELINE_DEPTH;

localparam MUL_OUTPUT_DATA_WIDTH = INPUT_DATA_WIDTH * 2;
localparam MUL_OUTPUT_FRACTIONAL_WIDTH = INPUT_FRACTIONAL_WIDTH * 2;

localparam MAX_ITER = 255;

logic signed [INPUT_DATA_WIDTH - 1:0] zr;  // Re(z)
logic signed [INPUT_DATA_WIDTH - 1:0] zi;  // Im(z)

logic signed [INPUT_DATA_WIDTH - 1:0] cr;  // Re(c)
logic signed [INPUT_DATA_WIDTH - 1:0] ci;  // Im(c)

logic signed [MUL_OUTPUT_DATA_WIDTH - 1:0] zr2[0:MUL_PIPELINE_DEPTH - 1]; // Re(z) * Re(z)
logic signed [MUL_OUTPUT_DATA_WIDTH - 1:0] zi2[0:MUL_PIPELINE_DEPTH - 1]; // Im(z) * Im(z)
logic signed [MUL_OUTPUT_DATA_WIDTH - 1:0] zri[0:MUL_PIPELINE_DEPTH - 1]; // Re(z) * Im(z)

// Re(z^2)
wire signed [MUL_OUTPUT_DATA_WIDTH - 1:0] zz_r = zr2[MUL_PIPELINE_DEPTH - 1] - zi2[MUL_PIPELINE_DEPTH - 1];
// Im(z^2)
wire signed [MUL_OUTPUT_DATA_WIDTH - 1:0] zz_i = zri[MUL_PIPELINE_DEPTH - 1] + zri[MUL_PIPELINE_DEPTH - 1];
// Re(z)^2 * Im(z)^2
wire signed [MUL_OUTPUT_DATA_WIDTH - 1:0] z_sq = zr2[MUL_PIPELINE_DEPTH - 1] + zi2[MUL_PIPELINE_DEPTH - 1];
// Re(z^2 + c)
wire signed [INPUT_DATA_WIDTH - 1:0] zz_c_r = zz_r[28+:32] + cr;
// Im(z^2 + c)
wire signed [INPUT_DATA_WIDTH - 1:0] zz_c_i = zz_i[28+:32] + ci;

logic [7:0] iter[PIPELINE_DEPTH - 1:0];
bit         finished[PIPELINE_DEPTH - 1:0];

always_ff @(posedge clk) begin
  zr <= zr_in;
  zi <= zi_in;
  cr <= cr_in;
  ci <= ci_in;

  finished[0] <= finished_in;
  if (~inc_enabled || finished_in)
    iter[0] <= iter_in;
  else
    iter[0] <= iter_in + 'h1;
end

always_ff @(posedge clk) begin
  zr2[0] <= zr * zr;
  zi2[0] <= zi * zi;
  zri[0] <= zr * zi;

  for (integer i = 1; i < MUL_PIPELINE_DEPTH; i++) begin
    zr2[i] <= zr2[i - 1];
    zi2[i] <= zi2[i - 1];
    zri[i] <= zri[i - 1];
  end
end

always_ff @(posedge clk) begin
  for (integer i = 1; i < PIPELINE_DEPTH; i++) begin
    iter[i] <= iter[i - 1];
    finished[i] <= finished[i - 1];
  end
end

assign zr_out = zz_c_r;
assign zi_out = zz_c_i;

assign iter_out = iter[PIPELINE_DEPTH - 1];

assign finished_out = finished[PIPELINE_DEPTH - 1] ||
                      iter[PIPELINE_DEPTH - 1] == MAX_ITER ||
                      z_sq > {8'h4, 56'h0};

endmodule

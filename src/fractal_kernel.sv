module fractal_kernel #(
  parameter integer PIPELINE_DEPTH = 8,
  parameter integer INPUT_DATA_WIDTH = 32
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
  output signed [INPUT_DATA_WIDTH - 1:0] cr_out,
  output signed [INPUT_DATA_WIDTH - 1:0] ci_out,
  output  [7:0] iter_out,
  output        finished_out
);

localparam MUL_PIPELINE_DEPTH = PIPELINE_DEPTH - 1;
localparam MUL_OUTPUT_DATA_WIDTH = INPUT_DATA_WIDTH * 2;

localparam MAX_ITER = 255;

logic signed [INPUT_DATA_WIDTH - 1:0] zr;  // Re(z)
logic signed [INPUT_DATA_WIDTH - 1:0] zi;  // Im(z)

logic signed [INPUT_DATA_WIDTH - 1:0] cr[PIPELINE_DEPTH - 1:0];  // Re(c)
logic signed [INPUT_DATA_WIDTH - 1:0] ci[PIPELINE_DEPTH - 1:0];  // Im(c)

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
wire signed [INPUT_DATA_WIDTH - 1:0] zz_c_r = zz_r[28+:32] + cr[PIPELINE_DEPTH - 1];
// Im(z^2 + c)
wire signed [INPUT_DATA_WIDTH - 1:0] zz_c_i = zz_i[28+:32] + ci[PIPELINE_DEPTH - 1];

logic [7:0] iter[PIPELINE_DEPTH - 1:0];
bit         finished[PIPELINE_DEPTH - 1:0];

always_ff @(posedge clk) begin
  zr <= zr_in;
  zi <= zi_in;

  cr[0] <= cr_in;
  ci[0] <= ci_in;

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
    cr[i] <= cr[i - 1];
    ci[i] <= ci[i - 1];

    iter[i] <= iter[i - 1];
    finished[i] <= finished[i - 1];
  end
end

assign zr_out = zz_c_r;
assign zi_out = zz_c_i;

assign cr_out = cr[PIPELINE_DEPTH - 1];
assign ci_out = ci[PIPELINE_DEPTH - 1];

assign iter_out = iter[PIPELINE_DEPTH - 1];

assign finished_out = finished[PIPELINE_DEPTH - 1] ||
                      iter[PIPELINE_DEPTH - 1] == MAX_ITER ||
                      z_sq > {8'h4, 56'h0};

endmodule

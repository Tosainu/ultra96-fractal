module fractal_generator #(
  parameter integer NUM_PARALLELS = 24
) (
  input         clk,
  input         resetn,
  input  [15:0] width_in,
  input  [15:0] height_in,
  input  [31:0] cr_in,
  input  [31:0] ci_in,
  input  [31:0] dx_in,
  input  [31:0] dy_in,
  input  [31:0] x0_in,
  input  [31:0] y0_in,
  output  [7:0] data,         // tdata
  output        frame_start,  // tuser
  output        line_end,     // tlast
  output        data_enable   // tvalid
);

localparam MAX_ITER = 255;
localparam NUM_STAGES = 9;
localparam NUM_LOOPS = (MAX_ITER + NUM_PARALLELS - 1) / NUM_PARALLELS;

const bit [NUM_STAGES - 1:0] state0_begin = 'b1;
const bit [NUM_STAGES - 1:0] state0_end = state0_begin << (NUM_STAGES - 1);
bit       [NUM_STAGES - 1:0] state0 = state0_end;
always_ff @(posedge clk) begin
  if (~resetn)
    state0 <= state0_end;
  else begin
    if (state0[NUM_STAGES - 1])
      state0 <= state0_begin;
    else
      state0 <= state0 << 1;
  end
end

const bit [NUM_PARALLELS - 1:0] state1_begin = 'b1;
const bit [NUM_PARALLELS - 1:0] state1_end = state1_begin << (NUM_PARALLELS - 1);
bit       [NUM_PARALLELS - 1:0] state1 = state1_end;
always_ff @(posedge clk) begin
  if (~resetn)
    state1 <= state1_end;
  else begin
    if (state0[NUM_STAGES - 1]) begin
      if (state1[NUM_PARALLELS - 1])
        state1 <= state1_begin;
      else
        state1 <= state1 << 1;
    end
    else
      state1 <= state1;
  end
end

const bit [NUM_LOOPS - 1:0] state2_begin = 'b1;
const bit [NUM_LOOPS - 1:0] state2_end = state2_begin << (NUM_LOOPS - 1);
bit       [NUM_LOOPS - 1:0] state2 = state2_end;
always_ff @(posedge clk) begin
  if (~resetn)
    state2 <= state2_end;
  else begin
    if (state1[NUM_PARALLELS - 1] && state0[NUM_STAGES - 1]) begin
      if (state2[NUM_LOOPS - 1])
        state2 <= state2_begin;
      else
        state2 <= state2 << 1;
    end
    else
      state2 <= state2;
  end
end

bit ready = 1'b0;
assign data_enable = ready && state2[0];
always_ff @(posedge clk) begin
  if (~resetn)
    ready <= 1'b0;
  else begin
    if (~ready &&
        state2[0] &&
        state1[NUM_PARALLELS - 1] &&
        state0[NUM_STAGES - 1])
      ready <= 1'b1;
  end
end

logic signed [15:0] width;
logic signed [15:0] height;
always_ff @(posedge clk) begin
  // changing width or height needs to reset.
  if (~resetn) begin
    width <= width_in;
    height <= height_in;
  end
end

logic signed [31:0] cr_next;
logic signed [31:0] ci_next;
logic signed [31:0] dx_next;
logic signed [31:0] dy_next;
logic signed [31:0] x0_next;
logic signed [31:0] y0_next;

always_ff @(posedge clk) begin
  cr_next <= cr_in;
  ci_next <= ci_in;
  dx_next <= dx_in;
  dy_next <= dy_in;
  x0_next <= x0_in;
  y0_next <= y0_in;
end

logic signed [15:0] x;
logic signed [15:0] y;
logic signed [31:0] z0_r;
logic signed [31:0] z0_i;

logic signed [31:0] cr_current;
logic signed [31:0] ci_current;
logic signed [31:0] dx_current;
logic signed [31:0] dy_current;
logic signed [31:0] x0_current;
logic signed [31:0] y0_current;

always_ff @(posedge clk) begin
  if (~resetn) begin
    x <= -16'h1;
    y <= -16'h1;
    z0_r <= 'h0;
    z0_i <= 'h0;

    cr_current <= cr_in;
    ci_current <= ci_in;
    dx_current <= dx_in;
    dy_current <= dy_in;
    x0_current <= x0_in;
    y0_current <= y0_in;
  end
  else begin
    if ((state2[NUM_LOOPS - 1] &&
         state1[NUM_PARALLELS - 1] &&
         state0[NUM_STAGES - 1]) ||
        (state2[0] &&
         !(state1[NUM_PARALLELS - 1] &&
           state0[NUM_STAGES - 1]))) begin
      if (x == -16'h1 || x == width - 1) begin
        if (y == -16'h1 || y == height - 1) begin
          x <= 16'h00;
          y <= 16'h00;
          z0_r <= -x0_next;
          z0_i <= -y0_next;

          cr_current <= cr_next;
          ci_current <= ci_next;
          dx_current <= dx_next;
          dy_current <= dy_next;
          x0_current <= x0_next;
          y0_current <= y0_next;
        end
        else begin
          x <= 16'h00;
          y <= y + 16'h01;
          z0_r <= -x0_current;
          z0_i <= z0_i + dy_current;
        end
      end
      else begin
        x <= x + 16'h01;
        z0_r <= z0_r + dx_current;
      end
    end
  end
end

wire signed [31:0] zr[NUM_PARALLELS - 1:0];
wire signed [31:0] zi[NUM_PARALLELS - 1:0];

wire signed [31:0] cr[NUM_PARALLELS - 1:0];
wire signed [31:0] ci[NUM_PARALLELS - 1:0];

wire [7:0] iter[NUM_PARALLELS - 1:0];
wire       finished[NUM_PARALLELS - 1:0];

logic signed [31:0] zr_u_0;
logic signed [31:0] zi_u_0;
logic signed [31:0] cr_u_0;
logic signed [31:0] ci_u_0;
logic         [7:0] iter_u_0;
bit                 finished_u_0;
bit                 inc_enabled_u_0;

always_comb begin
  if (state2[0]) begin
    zr_u_0 = z0_r;
    zi_u_0 = z0_i;
    cr_u_0 = cr_current;
    ci_u_0 = ci_current;
    iter_u_0 = 'h0;
    finished_u_0 = 'b0;
    inc_enabled_u_0 = 'b0;
  end
  else begin
    zr_u_0 = zr[NUM_PARALLELS - 1];
    zi_u_0 = zi[NUM_PARALLELS - 1];
    cr_u_0 = cr[NUM_PARALLELS - 1];
    ci_u_0 = ci[NUM_PARALLELS - 1];
    iter_u_0 = iter[NUM_PARALLELS - 1];
    finished_u_0 = finished[NUM_PARALLELS - 1];
    inc_enabled_u_0 = 'b1;
  end
end

fractal_kernel #(
  .PIPELINE_DEPTH(NUM_STAGES)
) u_0(
  .clk(clk),
  .inc_enabled(inc_enabled_u_0),
  .zr_in(zr_u_0),
  .zi_in(zi_u_0),
  .cr_in(cr_u_0),
  .ci_in(ci_u_0),
  .iter_in(iter_u_0),
  .finished_in(finished_u_0),
  .zr_out(zr[0]),
  .zi_out(zi[0]),
  .cr_out(cr[0]),
  .ci_out(ci[0]),
  .iter_out(iter[0]),
  .finished_out(finished[0])
);

for (genvar i = 1; i < NUM_PARALLELS; i++) begin
  fractal_kernel #(
    .PIPELINE_DEPTH(NUM_STAGES)
  ) u_i(
    .clk(clk),
    .inc_enabled('b1),
    .zr_in(zr[i - 1]),
    .zi_in(zi[i - 1]),
    .cr_in(cr[i - 1]),
    .ci_in(ci[i - 1]),
    .iter_in(iter[i - 1]),
    .finished_in(finished[i - 1]),
    .zr_out(zr[i]),
    .zi_out(zi[i]),
    .cr_out(cr[i]),
    .ci_out(ci[i]),
    .iter_out(iter[i]),
    .finished_out(finished[i])
  );
end

assign data = iter[NUM_PARALLELS - 1];

logic [15:0] out_x = 'b0;
logic [15:0] out_y = 'b0;
always_ff @(posedge clk) begin
  if (~resetn) begin
    out_x <= 'b0;
    out_y <= 'b0;
  end
  else begin
    if (data_enable) begin
      if (out_x == width - 1) begin
        out_x <= 'b0;
        if (out_y == height - 1)
          out_y <= 'b0;
        else
          out_y <= out_y + 1;
      end
      else
        out_x <= out_x + 1;
    end
  end
end

assign frame_start = data_enable && out_x == 'b0 && out_y == 'b0;
assign line_end = data_enable && out_x == width - 1;

endmodule

module fractal_generator #(
  parameter integer NUM_PARALLELS = 24
) (
  input         clk,
  input         resetn,
  input  [15:0] width,
  input  [15:0] height,
  input  [31:0] cr,
  input  [31:0] ci,
  input  [31:0] dx,
  input  [31:0] dy,
  input  [31:0] x0,
  input  [31:0] y0,
  output  [7:0] data,         // tdata
  output        frame_start,  // tuser
  output        line_end,     // tlast
  output        data_enable   // tvalid
);

localparam MAX_ITER = 255;
localparam NUM_PRE_STAGES = 0;
localparam NUM_MULTIPLIER_STAGES = 7;
localparam NUM_STAGES = 1 + NUM_MULTIPLIER_STAGES;
localparam NUM_LOOPS = (MAX_ITER + NUM_PARALLELS - 1) / NUM_PARALLELS;

const bit [NUM_STAGES - 1:0] state0_begin = 'b1;
const bit [NUM_STAGES - 1:0] state0_init = state0_begin << (NUM_STAGES - 1 - NUM_PRE_STAGES);
const bit [NUM_STAGES - 1:0] state0_end = state0_begin << (NUM_STAGES - 1);
bit       [NUM_STAGES - 1:0] state0 = state0_end;
always_ff @(posedge clk) begin
  if (~resetn)
    state0 <= state0_init;
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

logic signed [15:0] x;
logic signed [15:0] y;
logic signed [31:0] z0_r;
logic signed [31:0] z0_i;

logic signed [15:0] width_i;
logic signed [15:0] height_i;
always_ff @(posedge clk) begin
  // changing width or height needs to reset.
  if (~resetn) begin
    width_i <= width;
    height_i <= height;
  end
end

logic signed [31:0] dx_i;
logic signed [31:0] dy_i;
logic signed [31:0] x0_i;
logic signed [31:0] y0_i;
always_ff @(posedge clk) begin
  if (~resetn || (x == 'b0 && y == 'b0)) begin
    dx_i <= dx;
    dy_i <= dy;
    x0_i <= x0;
    y0_i <= y0;
  end
end

logic signed [31:0] cr_i;
logic signed [31:0] ci_i;
always_ff @(posedge clk) begin
  if (~resetn || (x == 'b0 && y == 'b0)) begin
    cr_i <= cr;
    ci_i <= ci;
  end
end

always_ff @(posedge clk) begin
  if (~resetn) begin
    x <= -16'h1;
    y <= -16'h1;
    z0_r <= 'h0;
    z0_i <= 'h0;
  end
  else begin
    if ((state2[NUM_LOOPS - 1] &&
         state1[NUM_PARALLELS - 1] &&
         state0 >= state0_init) ||
        (state2[0] &&
         !(state1[NUM_PARALLELS - 1] &&
           state0 >= state0_init))) begin
      if (x == -16'h1 || x == width_i - 1) begin
        x <= 16'h00;
        z0_r <= -x0_i;
        if (y == -16'h1 || y == height_i - 1) begin
          y <= 16'h00;
          z0_i <= -y0_i;
        end
        else begin
          y <= y + 16'h01;
          z0_i <= z0_i + dy_i;
        end
      end
      else begin
        x <= x + 16'h01;
        z0_r <= z0_r + dx_i;
      end
    end
    else begin
      x <= x;
      y <= y;
      z0_r <= z0_r;
      z0_i <= z0_i;
    end
  end
end

wire signed [31:0] zr[NUM_PARALLELS - 1:0];
wire signed [31:0] zi[NUM_PARALLELS - 1:0];

wire signed [31:0] cr2[NUM_PARALLELS - 1:0];
wire signed [31:0] ci2[NUM_PARALLELS - 1:0];

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
    cr_u_0 = cr_i;
    ci_u_0 = ci_i;
    iter_u_0 = 'h0;
    finished_u_0 = 'b0;
    inc_enabled_u_0 = 'b0;
  end
  else begin
    zr_u_0 = zr[NUM_PARALLELS - 1];
    zi_u_0 = zi[NUM_PARALLELS - 1];
    cr_u_0 = cr2[NUM_PARALLELS - 1];
    ci_u_0 = ci2[NUM_PARALLELS - 1];
    iter_u_0 = iter[NUM_PARALLELS - 1];
    finished_u_0 = finished[NUM_PARALLELS - 1];
    inc_enabled_u_0 = 'b1;
  end
end

fractal_kernel u_0(
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
  .cr_out(cr2[0]),
  .ci_out(ci2[0]),
  .iter_out(iter[0]),
  .finished_out(finished[0])
);

for (genvar i = 1; i < NUM_PARALLELS; i++) begin
  fractal_kernel u_i(
    .clk(clk),
    .inc_enabled('b1),
    .zr_in(zr[i - 1]),
    .zi_in(zi[i - 1]),
    .cr_in(cr2[i - 1]),
    .ci_in(ci2[i - 1]),
    .iter_in(iter[i - 1]),
    .finished_in(finished[i - 1]),
    .zr_out(zr[i]),
    .zi_out(zi[i]),
    .cr_out(cr2[i]),
    .ci_out(ci2[i]),
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
      if (out_x == width_i - 1) begin
        out_x <= 'b0;
        if (out_y == height_i - 1)
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
assign line_end = data_enable && out_x == width_i - 1;

endmodule

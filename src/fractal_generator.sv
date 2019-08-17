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
logic [7:0] iter[NUM_PARALLELS - 1:0][NUM_STAGES - 1:0];

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

logic signed [31:0] cr_i[NUM_PARALLELS - 1:0];
logic signed [31:0] ci_i[NUM_PARALLELS - 1:0];
for (genvar i = 0; i < NUM_PARALLELS; i++) begin
  always_ff @(posedge clk) begin
    if (~resetn || (x == 'b0 && y == 'b0)) begin
      cr_i[i] <= cr;
      ci_i[i] <= ci;
    end
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

logic signed [31:0] zr[NUM_PARALLELS - 1:0];  // Re(z)
logic signed [31:0] zi[NUM_PARALLELS - 1:0];  // Im(z)

logic signed [63:0] zr2[NUM_PARALLELS - 1:0][0:NUM_MULTIPLIER_STAGES - 1]; // Re(z) * Re(z)
logic signed [63:0] zi2[NUM_PARALLELS - 1:0][0:NUM_MULTIPLIER_STAGES - 1]; // Im(z) * Im(z)
logic signed [63:0] zri[NUM_PARALLELS - 1:0][0:NUM_MULTIPLIER_STAGES - 1]; // Re(z) * Im(z)

wire signed [63:0] zz_r[NUM_PARALLELS - 1:0]; // Re(z^2)
wire signed [63:0] zz_i[NUM_PARALLELS - 1:0]; // Im(z^2)
wire signed [63:0] z_sq[NUM_PARALLELS - 1:0]; // Re(z)^2 * Im(z)^2
wire signed [31:0] zz_c_r[NUM_PARALLELS - 1:0]; // Re(z^2 + c)
wire signed [31:0] zz_c_i[NUM_PARALLELS - 1:0]; // Im(z^2 + c)

for (genvar i = 0; i < NUM_PARALLELS; i++) begin
  assign zz_r[i] = zr2[i][NUM_MULTIPLIER_STAGES - 1] - zi2[i][NUM_MULTIPLIER_STAGES - 1];
  assign zz_i[i] = zri[i][NUM_MULTIPLIER_STAGES - 1] + zri[i][NUM_MULTIPLIER_STAGES - 1];
  assign z_sq[i] = zr2[i][NUM_MULTIPLIER_STAGES - 1] + zi2[i][NUM_MULTIPLIER_STAGES - 1];
  assign zz_c_r[i] = zz_r[i][28+:32] + cr_i[i];
  assign zz_c_i[i] = zz_i[i][28+:32] + ci_i[i];
end

bit done[NUM_PARALLELS - 1:0][NUM_STAGES - 1:0];
wire done_next[NUM_PARALLELS - 1:0];
for (genvar i = 0; i < NUM_PARALLELS; i++)
  assign done_next[i] = done[i][NUM_STAGES - 1] || z_sq[i] > {8'h4, 56'h0};

// stage 0
always_ff @(posedge clk) begin
  if (state2[0]) begin
    zr[0] <= z0_r;
    zi[0] <= z0_i;
    done[0][0] <= 1'b0;
    iter[0][0] <= 8'h0;
  end
  else begin
    zr[0] <= zz_c_r[NUM_PARALLELS - 1];
    zi[0] <= zz_c_i[NUM_PARALLELS - 1];
    done[0][0] <= done_next[NUM_PARALLELS - 1];
    if (done_next[NUM_PARALLELS - 1] || iter[NUM_PARALLELS - 1][NUM_STAGES - 1] == MAX_ITER)
      iter[0][0] <= iter[NUM_PARALLELS - 1][NUM_STAGES - 1];
    else
      iter[0][0] <= iter[NUM_PARALLELS - 1][NUM_STAGES - 1] + 8'h1;
  end
end
for (genvar i = 1; i < NUM_PARALLELS; i++) begin
  always_ff @(posedge clk) begin
    zr[i] <= zz_c_r[i - 1];
    zi[i] <= zz_c_i[i - 1];
    done[i][0] <= done_next[i - 1];
    if (done_next[i - 1] || iter[i - 1][NUM_STAGES - 1] == MAX_ITER)
      iter[i][0] <= iter[i - 1][NUM_STAGES - 1];
    else
      iter[i][0] <= iter[i - 1][NUM_STAGES - 1] + 8'h1;
  end
end

// stage 1
for (genvar i = 0; i < NUM_PARALLELS; i++) begin
  always_ff @(posedge clk) begin
    zr2[i][0] <= zr[i] * zr[i];
    zi2[i][0] <= zi[i] * zi[i];
    zri[i][0] <= zr[i] * zi[i];
  end
end

// stage 2 ~ NUM_STAGES - 1
for (genvar i = 0; i < NUM_PARALLELS; i++) begin
  for (genvar j = 1; j < NUM_MULTIPLIER_STAGES; j++) begin
    always_ff @(posedge clk) begin
      zr2[i][j] <= zr2[i][j - 1];
      zi2[i][j] <= zi2[i][j - 1];
      zri[i][j] <= zri[i][j - 1];
    end
  end
end

for (genvar i = 0; i < NUM_PARALLELS; i++) begin
  for (genvar j = 1; j < NUM_STAGES; j++) begin
    always_ff @(posedge clk) begin
      done[i][j] <= done[i][j - 1];
      iter[i][j] <= iter[i][j - 1];
    end
  end
end

assign data = iter[NUM_PARALLELS - 1][NUM_STAGES - 1];

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

module fractal_colorizer(
  input               clk,
  input               resetn,
  input         [3:0] mode,
  input         [7:0] data_in,
  input               frame_start_in,
  input               line_end_in,
  input               data_enable_in,
  output logic [23:0] data_out,
  output logic        frame_start_out,
  output logic        line_end_out,
  output logic        data_enable_out
);

const bit [3:0] MODE_GRAY    = 'h0;
const bit [3:0] MODE_RED     = 'h1;
const bit [3:0] MODE_GREEN   = 'h2;
const bit [3:0] MODE_BLUE    = 'h3;
const bit [3:0] MODE_YELLOW  = 'h4;
const bit [3:0] MODE_CYAN    = 'h5;
const bit [3:0] MODE_MAGENTA = 'h6;
const bit [3:0] MODE_COLOR1  = 'h7;

const bit [3:0] MODE_DEFAULT = MODE_GRAY;

bit [3:0] current_mode = MODE_DEFAULT;

always_ff @(posedge clk) begin
  if (frame_start_in)
    current_mode <= mode;
end

logic  [7:0] data;

always_ff @(posedge clk) begin
  frame_start_out <= frame_start_in;
  line_end_out <= line_end_in;
  data_enable_out <= data_enable_in;
  data <= data_in;
end

logic [23:0] rom_out;
xpm_memory_sprom #(
  .ADDR_WIDTH_A(8),
  .AUTO_SLEEP_TIME(0),
  .CASCADE_HEIGHT(0),
  .ECC_MODE("no_ecc"),
  .MEMORY_INIT_FILE("color_table.mem"),
  .MEMORY_INIT_PARAM("0"),
  .MEMORY_OPTIMIZATION("true"),
  .MEMORY_PRIMITIVE("auto"),
  .MEMORY_SIZE(6144),   // 24 * 256 [bits]
  .MESSAGE_CONTROL(0),
  .READ_DATA_WIDTH_A(24),
  .READ_LATENCY_A(1),
  .READ_RESET_VALUE_A("0"),
  .RST_MODE_A("SYNC"),
  .SIM_ASSERT_CHK(1),
  .USE_MEM_INIT(1),
  .WAKEUP_TIME("disable_sleep")
) xpm_memory_sprom_inst(
  .clka(clk),
  .rsta(~resetn),
  .addra(data_in),
  .douta(rom_out),

  .dbiterra(),
  .ena('b1),
  .injectdbiterra('b0),
  .injectsbiterra('b0),
  .regcea('b1),
  .sbiterra(),
  .sleep('b0)
);

always_comb begin
  case (current_mode)
    MODE_GRAY:    data_out = {3{data}};
    MODE_RED:     data_out = {data, 16'b0};
    MODE_GREEN:   data_out = {16'b0, data};
    MODE_BLUE:    data_out = {8'b0, data, 8'b0};
    MODE_YELLOW:  data_out = {data, 8'b0, data};
    MODE_CYAN:    data_out = {8'b0, data, data};
    MODE_MAGENTA: data_out = {data, data, 8'b0};
    MODE_COLOR1:  data_out = rom_out;
    default:      data_out = {3{data}};
  endcase
end

endmodule

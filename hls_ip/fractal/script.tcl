set script_dir [file dirname [info script]]

open_project fractal
set_top fractal
add_files "$script_dir/fractal.cc" -cflags "-std=c++11"
add_files -tb "$script_dir/fractal_tb.cc" -cflags "-std=c++11"
open_solution "solution1"
set_part {xczu3eg-sbva484-1-e} -tool vivado
create_clock -period 10.00 -name default
# csim_design -ldflags {-B/usr/lib/x86_64-linux-gnu/} -O
csynth_design
# cosim_design -ldflags {-B/usr/lib/x86_64-linux-gnu/}
export_design -rtl verilog -format ip_catalog

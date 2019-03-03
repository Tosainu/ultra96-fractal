set script_dir [file dirname [info script]]

open_project data_width_converter
set_top data_width_converter
add_files "$script_dir/data_width_converter.cc" -cflags "-std=c++11"
open_solution "solution1"
set_part {xczu3eg-sbva484-1-e} -tool vivado
create_clock -period 6.67 -name default
# csim_design -ldflags {-B/usr/lib/x86_64-linux-gnu/} -O
csynth_design
# cosim_design -ldflags {-B/usr/lib/x86_64-linux-gnu/}
export_design -rtl verilog -format ip_catalog

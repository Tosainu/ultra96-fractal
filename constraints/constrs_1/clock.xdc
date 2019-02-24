# pixel clock for 1920x1080 @ 60 Hz = 148.5 MHz
create_clock -period 6.734 -name dp_video_clk -waveform {0.000 3.367} [get_pins system_i/zynq_ultra_ps_e_0/inst/dp_video_ref_clk]

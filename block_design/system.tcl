# Proc to create BD system
proc cr_bd_system { srcset {parentCell ""} } {
# The design that will be created by this Tcl proc contains the following 
# module references:
# fractal



  # CHANGE DESIGN NAME HERE
  set design_name system

  common::send_gid_msg -ssname BD::TCL -id 2010 -severity "INFO" "Currently there is no design <$design_name> in project, so creating one..."

  create_bd_design -srcset $srcset $design_name

  set bCheckIPsPassed 1
  ##################################################################
  # CHECK IPs
  ##################################################################
  set bCheckIPs 1
  if { $bCheckIPs == 1 } {
     set list_check_ips "\ 
  xilinx.com:ip:axi_vdma:6.3\
  xilinx.com:ip:axis_clock_converter:1.1\
  xilinx.com:ip:axis_data_fifo:2.0\
  xilinx.com:ip:axis_subset_converter:1.1\
  xilinx.com:ip:clk_wiz:6.0\
  xilinx.com:ip:proc_sys_reset:5.0\
  xilinx.com:ip:smartconnect:1.0\
  xilinx.com:ip:v_axi4s_vid_out:4.0\
  xilinx.com:ip:v_tc:6.2\
  xilinx.com:ip:xlconcat:2.1\
  xilinx.com:ip:zynq_ultra_ps_e:3.4\
  xilinx.com:ip:xlconstant:1.1\
  xilinx.com:ip:xlslice:1.0\
  "

   set list_ips_missing ""
   common::send_gid_msg -ssname BD::TCL -id 2011 -severity "INFO" "Checking if the following IPs exist in the project's IP catalog: $list_check_ips ."

   foreach ip_vlnv $list_check_ips {
      set ip_obj [get_ipdefs -all $ip_vlnv]
      if { $ip_obj eq "" } {
         lappend list_ips_missing $ip_vlnv
      }
   }

   if { $list_ips_missing ne "" } {
      catch {common::send_gid_msg -ssname BD::TCL -id 2012 -severity "ERROR" "The following IPs are not found in the IP Catalog:\n  $list_ips_missing\n\nResolution: Please add the repository containing the IP(s) to the project." }
      set bCheckIPsPassed 0
   }

  }

  ##################################################################
  # CHECK Modules
  ##################################################################
  set bCheckModules 1
  if { $bCheckModules == 1 } {
     set list_check_mods "\ 
  fractal\
  "

   set list_mods_missing ""
   common::send_gid_msg -ssname BD::TCL -id 2020 -severity "INFO" "Checking if the following modules exist in the project's sources: $list_check_mods ."

   foreach mod_vlnv $list_check_mods {
      if { [can_resolve_reference $mod_vlnv] == 0 } {
         lappend list_mods_missing $mod_vlnv
      }
   }

   if { $list_mods_missing ne "" } {
      catch {common::send_gid_msg -ssname BD::TCL -id 2021 -severity "ERROR" "The following module(s) are not found in the project: $list_mods_missing" }
      common::send_gid_msg -ssname BD::TCL -id 2022 -severity "INFO" "Please add source files for the missing module(s) above."
      set bCheckIPsPassed 0
   }
}

  if { $bCheckIPsPassed != 1 } {
    common::send_gid_msg -ssname BD::TCL -id 2023 -severity "WARNING" "Will not continue with creation of design due to the error(s) above."
    return 3
  }

  
# Hierarchical cell: to_live_video
proc create_hier_cell_to_live_video { parentCell nameHier } {

  variable script_folder

  if { $parentCell eq "" || $nameHier eq "" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2092 -severity "ERROR" "create_hier_cell_to_live_video() - Empty argument(s)!"}
     return
  }

  # Get object for parentCell
  set parentObj [get_bd_cells $parentCell]
  if { $parentObj == "" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2090 -severity "ERROR" "Unable to find parent cell <$parentCell>!"}
     return
  }

  # Make sure parentObj is hier blk
  set parentType [get_property TYPE $parentObj]
  if { $parentType ne "hier" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2091 -severity "ERROR" "Parent <$parentObj> has TYPE = <$parentType>. Expected to be <hier>."}
     return
  }

  # Save current instance; Restore later
  set oldCurInst [current_bd_instance .]

  # Set parent object as current
  current_bd_instance $parentObj

  # Create cell and set as current instance
  set hier_obj [create_bd_cell -type hier $nameHier]
  current_bd_instance $hier_obj

  # Create interface pins

  # Create pins
  create_bd_pin -dir I -from 23 -to 0 Din
  create_bd_pin -dir O -from 35 -to 0 dout

  # Create instance: xlconcat_0, and set properties
  set xlconcat_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:xlconcat:2.1 xlconcat_0 ]
  set_property -dict [ list \
   CONFIG.NUM_PORTS {6} \
 ] $xlconcat_0

  # Create instance: xlconstant_0, and set properties
  set xlconstant_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:xlconstant:1.1 xlconstant_0 ]
  set_property -dict [ list \
   CONFIG.CONST_VAL {0} \
   CONFIG.CONST_WIDTH {4} \
 ] $xlconstant_0

  # Create instance: xlslice_b, and set properties
  set xlslice_b [ create_bd_cell -type ip -vlnv xilinx.com:ip:xlslice:1.0 xlslice_b ]
  set_property -dict [ list \
   CONFIG.DIN_FROM {23} \
   CONFIG.DIN_TO {16} \
   CONFIG.DIN_WIDTH {24} \
   CONFIG.DOUT_WIDTH {8} \
 ] $xlslice_b

  # Create instance: xlslice_g, and set properties
  set xlslice_g [ create_bd_cell -type ip -vlnv xilinx.com:ip:xlslice:1.0 xlslice_g ]
  set_property -dict [ list \
   CONFIG.DIN_FROM {15} \
   CONFIG.DIN_TO {8} \
   CONFIG.DIN_WIDTH {24} \
   CONFIG.DOUT_WIDTH {8} \
 ] $xlslice_g

  # Create instance: xlslice_r, and set properties
  set xlslice_r [ create_bd_cell -type ip -vlnv xilinx.com:ip:xlslice:1.0 xlslice_r ]
  set_property -dict [ list \
   CONFIG.DIN_FROM {7} \
   CONFIG.DIN_TO {0} \
   CONFIG.DIN_WIDTH {24} \
   CONFIG.DOUT_WIDTH {8} \
 ] $xlslice_r

  # Create port connections
  connect_bd_net -net Net [get_bd_pins Din] [get_bd_pins xlslice_b/Din] [get_bd_pins xlslice_g/Din] [get_bd_pins xlslice_r/Din]
  connect_bd_net -net xlconcat_0_dout [get_bd_pins dout] [get_bd_pins xlconcat_0/dout]
  connect_bd_net -net xlconstant_0_dout [get_bd_pins xlconcat_0/In0] [get_bd_pins xlconcat_0/In2] [get_bd_pins xlconcat_0/In4] [get_bd_pins xlconstant_0/dout]
  connect_bd_net -net xlslice_0_Dout [get_bd_pins xlconcat_0/In3] [get_bd_pins xlslice_g/Dout]
  connect_bd_net -net xlslice_b_Dout [get_bd_pins xlconcat_0/In1] [get_bd_pins xlslice_b/Dout]
  connect_bd_net -net xlslice_r_Dout [get_bd_pins xlconcat_0/In5] [get_bd_pins xlslice_r/Dout]

  # Restore current instance
  current_bd_instance $oldCurInst
}
  variable script_folder

  if { $parentCell eq "" } {
     set parentCell [get_bd_cells /]
  }

  # Get object for parentCell
  set parentObj [get_bd_cells $parentCell]
  if { $parentObj == "" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2090 -severity "ERROR" "Unable to find parent cell <$parentCell>!"}
     return
  }

  # Make sure parentObj is hier blk
  set parentType [get_property TYPE $parentObj]
  if { $parentType ne "hier" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2091 -severity "ERROR" "Parent <$parentObj> has TYPE = <$parentType>. Expected to be <hier>."}
     return
  }

  # Save current instance; Restore later
  set oldCurInst [current_bd_instance .]

  # Set parent object as current
  current_bd_instance $parentObj


  # Create interface ports

  # Create ports

  # Create instance: axi_interconnect_hpm0, and set properties
  set axi_interconnect_hpm0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_interconnect:2.1 axi_interconnect_hpm0 ]
  set_property -dict [ list \
   CONFIG.ENABLE_ADVANCED_OPTIONS {0} \
   CONFIG.NUM_MI {1} \
 ] $axi_interconnect_hpm0

  # Create instance: axi_interconnect_hpm1, and set properties
  set axi_interconnect_hpm1 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_interconnect:2.1 axi_interconnect_hpm1 ]

  # Create instance: axi_vdma_0, and set properties
  set axi_vdma_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_vdma:6.3 axi_vdma_0 ]
  set_property -dict [ list \
   CONFIG.c_m_axis_mm2s_tdata_width {32} \
   CONFIG.c_mm2s_genlock_mode {3} \
   CONFIG.c_mm2s_linebuffer_depth {512} \
   CONFIG.c_mm2s_max_burst_length {32} \
   CONFIG.c_s2mm_genlock_mode {2} \
   CONFIG.c_s2mm_linebuffer_depth {512} \
   CONFIG.c_s2mm_max_burst_length {32} \
 ] $axi_vdma_0

  # Create instance: axis_clock_converter_0, and set properties
  set axis_clock_converter_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_clock_converter:1.1 axis_clock_converter_0 ]

  # Create instance: axis_data_fifo_0, and set properties
  set axis_data_fifo_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_data_fifo:2.0 axis_data_fifo_0 ]

  # Create instance: axis_subset_converter_0, and set properties
  set axis_subset_converter_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_subset_converter:1.1 axis_subset_converter_0 ]
  set_property -dict [ list \
   CONFIG.M_HAS_TKEEP {0} \
   CONFIG.M_HAS_TSTRB {1} \
   CONFIG.M_TDATA_NUM_BYTES {4} \
   CONFIG.S_HAS_TKEEP {0} \
   CONFIG.S_HAS_TSTRB {1} \
   CONFIG.S_TDATA_NUM_BYTES {3} \
   CONFIG.TDATA_REMAP {8'b11111111,tdata[15:8],tdata[7:0],tdata[23:16]} \
   CONFIG.TKEEP_REMAP {1'b0} \
   CONFIG.TSTRB_REMAP {1'b1,tstrb[2:0]} \
 ] $axis_subset_converter_0

  # Create instance: axis_subset_converter_1, and set properties
  set axis_subset_converter_1 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_subset_converter:1.1 axis_subset_converter_1 ]
  set_property -dict [ list \
   CONFIG.M_TDATA_NUM_BYTES {3} \
   CONFIG.S_TDATA_NUM_BYTES {4} \
   CONFIG.TDATA_REMAP {tdata[23:0]} \
 ] $axis_subset_converter_1

  # Create instance: clk_wiz_0, and set properties
  set clk_wiz_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:clk_wiz:6.0 clk_wiz_0 ]
  set_property -dict [ list \
   CONFIG.AUTO_PRIMITIVE {PLL} \
   CONFIG.CLKOUT1_DRIVES {Buffer} \
   CONFIG.CLKOUT1_JITTER {111.879} \
   CONFIG.CLKOUT1_PHASE_ERROR {105.461} \
   CONFIG.CLKOUT1_REQUESTED_OUT_FREQ {300} \
   CONFIG.CLKOUT2_DRIVES {Buffer} \
   CONFIG.CLKOUT2_JITTER {127.220} \
   CONFIG.CLKOUT2_PHASE_ERROR {105.461} \
   CONFIG.CLKOUT2_REQUESTED_OUT_FREQ {150} \
   CONFIG.CLKOUT2_USED {true} \
   CONFIG.CLKOUT3_DRIVES {Buffer} \
   CONFIG.CLKOUT4_DRIVES {Buffer} \
   CONFIG.CLKOUT5_DRIVES {Buffer} \
   CONFIG.CLKOUT6_DRIVES {Buffer} \
   CONFIG.CLKOUT7_DRIVES {Buffer} \
   CONFIG.FEEDBACK_SOURCE {FDBK_AUTO} \
   CONFIG.MMCM_BANDWIDTH {OPTIMIZED} \
   CONFIG.MMCM_CLKFBOUT_MULT_F {9} \
   CONFIG.MMCM_CLKOUT0_DIVIDE_F {3} \
   CONFIG.MMCM_CLKOUT1_DIVIDE {6} \
   CONFIG.MMCM_COMPENSATION {AUTO} \
   CONFIG.MMCM_DIVCLK_DIVIDE {1} \
   CONFIG.NUM_OUT_CLKS {2} \
   CONFIG.PRIMITIVE {Auto} \
   CONFIG.USE_LOCKED {true} \
   CONFIG.USE_RESET {false} \
 ] $clk_wiz_0

  # Create instance: fractal_0, and set properties
  set block_name fractal
  set block_cell_name fractal_0
  if { [catch {set fractal_0 [create_bd_cell -type module -reference $block_name $block_cell_name] } errmsg] } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2095 -severity "ERROR" "Unable to add referenced block <$block_name>. Please add the files for ${block_name}'s definition into the project."}
     return 1
   } elseif { $fractal_0 eq "" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2096 -severity "ERROR" "Unable to referenced block <$block_name>. Please add the files for ${block_name}'s definition into the project."}
     return 1
   }
    set_property -dict [ list \
   CONFIG.NUM_PARALLELS {29} \
 ] $fractal_0

  # Create instance: rst_clk_wiz_0_150M, and set properties
  set rst_clk_wiz_0_150M [ create_bd_cell -type ip -vlnv xilinx.com:ip:proc_sys_reset:5.0 rst_clk_wiz_0_150M ]

  # Create instance: rst_clk_wiz_0_300M, and set properties
  set rst_clk_wiz_0_300M [ create_bd_cell -type ip -vlnv xilinx.com:ip:proc_sys_reset:5.0 rst_clk_wiz_0_300M ]

  # Create instance: smartconnect_hp0, and set properties
  set smartconnect_hp0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:smartconnect:1.0 smartconnect_hp0 ]
  set_property -dict [ list \
   CONFIG.NUM_SI {1} \
 ] $smartconnect_hp0

  # Create instance: smartconnect_hp1, and set properties
  set smartconnect_hp1 [ create_bd_cell -type ip -vlnv xilinx.com:ip:smartconnect:1.0 smartconnect_hp1 ]
  set_property -dict [ list \
   CONFIG.NUM_SI {1} \
 ] $smartconnect_hp1

  # Create instance: to_live_video
  create_hier_cell_to_live_video [current_bd_instance .] to_live_video

  # Create instance: v_axi4s_vid_out_0, and set properties
  set v_axi4s_vid_out_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:v_axi4s_vid_out:4.0 v_axi4s_vid_out_0 ]
  set_property -dict [ list \
   CONFIG.C_ADDR_WIDTH {12} \
   CONFIG.C_HAS_ASYNC_CLK {1} \
   CONFIG.C_VTG_MASTER_SLAVE {1} \
 ] $v_axi4s_vid_out_0

  # Create instance: v_tc_0, and set properties
  set v_tc_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:v_tc:6.2 v_tc_0 ]
  set_property -dict [ list \
   CONFIG.GEN_F0_VBLANK_HEND {2008} \
   CONFIG.GEN_F0_VBLANK_HSTART {2008} \
   CONFIG.GEN_F0_VFRAME_SIZE {1125} \
   CONFIG.GEN_F0_VSYNC_HEND {2008} \
   CONFIG.GEN_F0_VSYNC_HSTART {2008} \
   CONFIG.GEN_F0_VSYNC_VEND {1088} \
   CONFIG.GEN_F0_VSYNC_VSTART {1083} \
   CONFIG.GEN_F1_VBLANK_HEND {2008} \
   CONFIG.GEN_F1_VBLANK_HSTART {2008} \
   CONFIG.GEN_F1_VFRAME_SIZE {1125} \
   CONFIG.GEN_F1_VSYNC_HEND {2008} \
   CONFIG.GEN_F1_VSYNC_HSTART {2008} \
   CONFIG.GEN_F1_VSYNC_VEND {1088} \
   CONFIG.GEN_F1_VSYNC_VSTART {1083} \
   CONFIG.GEN_HACTIVE_SIZE {1920} \
   CONFIG.GEN_HFRAME_SIZE {2200} \
   CONFIG.GEN_HSYNC_END {2052} \
   CONFIG.GEN_HSYNC_START {2008} \
   CONFIG.GEN_VACTIVE_SIZE {1080} \
   CONFIG.VIDEO_MODE {1080p} \
   CONFIG.enable_detection {false} \
 ] $v_tc_0

  # Create instance: xlconcat_0, and set properties
  set xlconcat_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:xlconcat:2.1 xlconcat_0 ]
  set_property -dict [ list \
   CONFIG.NUM_PORTS {3} \
 ] $xlconcat_0

  # Create instance: zynq_ultra_ps_e_0, and set properties
  set zynq_ultra_ps_e_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:zynq_ultra_ps_e:3.4 zynq_ultra_ps_e_0 ]
  apply_bd_automation -rule xilinx.com:bd_rule:zynq_ultra_ps_e -config {apply_board_preset "1"} $zynq_ultra_ps_e_0
  set_property -dict [list \
    CONFIG.PSU__USE__M_AXI_GP0 {1} \
    CONFIG.PSU__USE__M_AXI_GP1 {1} \
    CONFIG.PSU__USE__S_AXI_GP2 {1} \
    CONFIG.PSU__USE__S_AXI_GP3 {1} \
    CONFIG.PSU__USE__VIDEO {1} \
  ] $zynq_ultra_ps_e_0

  # Create interface connections
  connect_bd_intf_net -intf_net axi_interconnect_0_M00_AXI1 [get_bd_intf_pins axi_interconnect_hpm1/M00_AXI] [get_bd_intf_pins v_tc_0/ctrl]
  connect_bd_intf_net -intf_net axi_interconnect_0_M01_AXI [get_bd_intf_pins axi_interconnect_hpm1/M01_AXI] [get_bd_intf_pins axi_vdma_0/S_AXI_LITE]
  connect_bd_intf_net -intf_net axi_interconnect_hpm0_M00_AXI [get_bd_intf_pins axi_interconnect_hpm0/M00_AXI] [get_bd_intf_pins fractal_0/s_axi]
  connect_bd_intf_net -intf_net axi_vdma_0_M_AXIS_MM2S [get_bd_intf_pins axi_vdma_0/M_AXIS_MM2S] [get_bd_intf_pins axis_subset_converter_1/S_AXIS]
  connect_bd_intf_net -intf_net axi_vdma_0_M_AXI_MM2S [get_bd_intf_pins axi_vdma_0/M_AXI_MM2S] [get_bd_intf_pins smartconnect_hp1/S00_AXI]
  connect_bd_intf_net -intf_net axi_vdma_0_M_AXI_S2MM [get_bd_intf_pins axi_vdma_0/M_AXI_S2MM] [get_bd_intf_pins smartconnect_hp0/S00_AXI]
  connect_bd_intf_net -intf_net axis_clock_converter_0_M_AXIS [get_bd_intf_pins axis_clock_converter_0/M_AXIS] [get_bd_intf_pins axis_subset_converter_0/S_AXIS]
  connect_bd_intf_net -intf_net axis_data_fifo_0_M_AXIS [get_bd_intf_pins axis_clock_converter_0/S_AXIS] [get_bd_intf_pins axis_data_fifo_0/M_AXIS]
  connect_bd_intf_net -intf_net axis_subset_converter_0_M_AXIS [get_bd_intf_pins axi_vdma_0/S_AXIS_S2MM] [get_bd_intf_pins axis_subset_converter_0/M_AXIS]
  connect_bd_intf_net -intf_net axis_subset_converter_1_M_AXIS [get_bd_intf_pins axis_subset_converter_1/M_AXIS] [get_bd_intf_pins v_axi4s_vid_out_0/video_in]
  connect_bd_intf_net -intf_net fractal_0_m_axis [get_bd_intf_pins axis_data_fifo_0/S_AXIS] [get_bd_intf_pins fractal_0/m_axis]
  connect_bd_intf_net -intf_net smartconnect_hp0_M00_AXI [get_bd_intf_pins smartconnect_hp0/M00_AXI] [get_bd_intf_pins zynq_ultra_ps_e_0/S_AXI_HP0_FPD]
  connect_bd_intf_net -intf_net smartconnect_hp1_M00_AXI [get_bd_intf_pins smartconnect_hp1/M00_AXI] [get_bd_intf_pins zynq_ultra_ps_e_0/S_AXI_HP1_FPD]
  connect_bd_intf_net -intf_net v_tc_0_vtiming_out [get_bd_intf_pins v_axi4s_vid_out_0/vtiming_in] [get_bd_intf_pins v_tc_0/vtiming_out]
  connect_bd_intf_net -intf_net zynq_ultra_ps_e_0_M_AXI_HPM0_FPD [get_bd_intf_pins axi_interconnect_hpm0/S00_AXI] [get_bd_intf_pins zynq_ultra_ps_e_0/M_AXI_HPM0_FPD]
  connect_bd_intf_net -intf_net zynq_ultra_ps_e_0_M_AXI_HPM1_FPD [get_bd_intf_pins axi_interconnect_hpm1/S00_AXI] [get_bd_intf_pins zynq_ultra_ps_e_0/M_AXI_HPM1_FPD]

  # Create port connections
  connect_bd_net -net axi_vdma_0_mm2s_introut [get_bd_pins axi_vdma_0/mm2s_introut] [get_bd_pins xlconcat_0/In2]
  connect_bd_net -net axi_vdma_0_s2mm_introut [get_bd_pins axi_vdma_0/s2mm_introut] [get_bd_pins xlconcat_0/In0]
  connect_bd_net -net clk_wiz_0_clk_out1 [get_bd_pins axi_interconnect_hpm0/ACLK] [get_bd_pins axi_interconnect_hpm0/M00_ACLK] [get_bd_pins axi_interconnect_hpm0/S00_ACLK] [get_bd_pins axis_clock_converter_0/s_axis_aclk] [get_bd_pins axis_data_fifo_0/s_axis_aclk] [get_bd_pins clk_wiz_0/clk_out1] [get_bd_pins fractal_0/aclk] [get_bd_pins rst_clk_wiz_0_300M/slowest_sync_clk] [get_bd_pins zynq_ultra_ps_e_0/maxihpm0_fpd_aclk]
  connect_bd_net -net clk_wiz_0_clk_out2 [get_bd_pins axi_interconnect_hpm1/ACLK] [get_bd_pins axi_interconnect_hpm1/M00_ACLK] [get_bd_pins axi_interconnect_hpm1/M01_ACLK] [get_bd_pins axi_interconnect_hpm1/S00_ACLK] [get_bd_pins axi_vdma_0/m_axi_mm2s_aclk] [get_bd_pins axi_vdma_0/m_axi_s2mm_aclk] [get_bd_pins axi_vdma_0/m_axis_mm2s_aclk] [get_bd_pins axi_vdma_0/s_axi_lite_aclk] [get_bd_pins axi_vdma_0/s_axis_s2mm_aclk] [get_bd_pins axis_clock_converter_0/m_axis_aclk] [get_bd_pins axis_subset_converter_0/aclk] [get_bd_pins axis_subset_converter_1/aclk] [get_bd_pins clk_wiz_0/clk_out2] [get_bd_pins rst_clk_wiz_0_150M/slowest_sync_clk] [get_bd_pins smartconnect_hp0/aclk] [get_bd_pins smartconnect_hp1/aclk] [get_bd_pins v_axi4s_vid_out_0/aclk] [get_bd_pins v_tc_0/s_axi_aclk] [get_bd_pins zynq_ultra_ps_e_0/maxihpm1_fpd_aclk] [get_bd_pins zynq_ultra_ps_e_0/saxihp0_fpd_aclk] [get_bd_pins zynq_ultra_ps_e_0/saxihp1_fpd_aclk]
  connect_bd_net -net clk_wiz_0_locked [get_bd_pins clk_wiz_0/locked] [get_bd_pins rst_clk_wiz_0_150M/dcm_locked] [get_bd_pins rst_clk_wiz_0_300M/dcm_locked]
  connect_bd_net -net rst_clk_wiz_0_150M_interconnect_aresetn [get_bd_pins axi_interconnect_hpm1/ARESETN] [get_bd_pins rst_clk_wiz_0_150M/interconnect_aresetn]
  connect_bd_net -net rst_clk_wiz_0_150M_peripheral_aresetn [get_bd_pins axi_interconnect_hpm1/M00_ARESETN] [get_bd_pins axi_interconnect_hpm1/M01_ARESETN] [get_bd_pins axi_interconnect_hpm1/S00_ARESETN] [get_bd_pins axi_vdma_0/axi_resetn] [get_bd_pins axis_clock_converter_0/m_axis_aresetn] [get_bd_pins axis_subset_converter_0/aresetn] [get_bd_pins axis_subset_converter_1/aresetn] [get_bd_pins rst_clk_wiz_0_150M/peripheral_aresetn] [get_bd_pins smartconnect_hp0/aresetn] [get_bd_pins smartconnect_hp1/aresetn] [get_bd_pins v_axi4s_vid_out_0/aresetn] [get_bd_pins v_tc_0/s_axi_aresetn]
  connect_bd_net -net rst_clk_wiz_0_300M_interconnect_aresetn [get_bd_pins axi_interconnect_hpm0/ARESETN] [get_bd_pins rst_clk_wiz_0_300M/interconnect_aresetn]
  connect_bd_net -net rst_clk_wiz_0_300M_peripheral_aresetn [get_bd_pins axi_interconnect_hpm0/M00_ARESETN] [get_bd_pins axi_interconnect_hpm0/S00_ARESETN] [get_bd_pins axis_clock_converter_0/s_axis_aresetn] [get_bd_pins axis_data_fifo_0/s_axis_aresetn] [get_bd_pins fractal_0/aresetn] [get_bd_pins rst_clk_wiz_0_300M/peripheral_aresetn]
  connect_bd_net -net to_live_video_dout [get_bd_pins to_live_video/dout] [get_bd_pins zynq_ultra_ps_e_0/dp_live_video_in_pixel1]
  connect_bd_net -net v_axi4s_vid_out_0_vid_active_video [get_bd_pins v_axi4s_vid_out_0/vid_active_video] [get_bd_pins zynq_ultra_ps_e_0/dp_live_video_in_de]
  connect_bd_net -net v_axi4s_vid_out_0_vid_data [get_bd_pins to_live_video/Din] [get_bd_pins v_axi4s_vid_out_0/vid_data]
  connect_bd_net -net v_axi4s_vid_out_0_vid_hsync [get_bd_pins v_axi4s_vid_out_0/vid_hsync] [get_bd_pins zynq_ultra_ps_e_0/dp_live_video_in_hsync]
  connect_bd_net -net v_axi4s_vid_out_0_vid_vsync [get_bd_pins v_axi4s_vid_out_0/vid_vsync] [get_bd_pins zynq_ultra_ps_e_0/dp_live_video_in_vsync]
  connect_bd_net -net v_axi4s_vid_out_0_vtg_ce [get_bd_pins v_axi4s_vid_out_0/vtg_ce] [get_bd_pins v_tc_0/gen_clken]
  connect_bd_net -net v_tc_0_irq [get_bd_pins v_tc_0/irq] [get_bd_pins xlconcat_0/In1]
  connect_bd_net -net xlconcat_0_dout [get_bd_pins xlconcat_0/dout] [get_bd_pins zynq_ultra_ps_e_0/pl_ps_irq0]
  connect_bd_net -net zynq_ultra_ps_e_0_dp_video_ref_clk [get_bd_pins v_axi4s_vid_out_0/vid_io_out_clk] [get_bd_pins v_tc_0/clk] [get_bd_pins zynq_ultra_ps_e_0/dp_video_in_clk] [get_bd_pins zynq_ultra_ps_e_0/dp_video_ref_clk]
  connect_bd_net -net zynq_ultra_ps_e_0_pl_clk0 [get_bd_pins clk_wiz_0/clk_in1] [get_bd_pins zynq_ultra_ps_e_0/pl_clk0]
  connect_bd_net -net zynq_ultra_ps_e_0_pl_resetn0 [get_bd_pins rst_clk_wiz_0_150M/ext_reset_in] [get_bd_pins rst_clk_wiz_0_300M/ext_reset_in] [get_bd_pins zynq_ultra_ps_e_0/pl_resetn0]

  # Create address segments
  assign_bd_address -offset 0x00000000 -range 0x80000000 -target_address_space [get_bd_addr_spaces axi_vdma_0/Data_MM2S] [get_bd_addr_segs zynq_ultra_ps_e_0/SAXIGP3/HP1_DDR_LOW] -force
  assign_bd_address -offset 0xFF000000 -range 0x01000000 -target_address_space [get_bd_addr_spaces axi_vdma_0/Data_MM2S] [get_bd_addr_segs zynq_ultra_ps_e_0/SAXIGP3/HP1_LPS_OCM] -force
  assign_bd_address -offset 0x00000000 -range 0x80000000 -target_address_space [get_bd_addr_spaces axi_vdma_0/Data_S2MM] [get_bd_addr_segs zynq_ultra_ps_e_0/SAXIGP2/HP0_DDR_LOW] -force
  assign_bd_address -offset 0xFF000000 -range 0x01000000 -target_address_space [get_bd_addr_spaces axi_vdma_0/Data_S2MM] [get_bd_addr_segs zynq_ultra_ps_e_0/SAXIGP2/HP0_LPS_OCM] -force
  assign_bd_address -offset 0xB0000000 -range 0x00001000 -target_address_space [get_bd_addr_spaces zynq_ultra_ps_e_0/Data] [get_bd_addr_segs axi_vdma_0/S_AXI_LITE/Reg] -force
  assign_bd_address -offset 0xA0000000 -range 0x00001000 -target_address_space [get_bd_addr_spaces zynq_ultra_ps_e_0/Data] [get_bd_addr_segs fractal_0/s_axi/reg0] -force
  assign_bd_address -offset 0xB0010000 -range 0x00010000 -target_address_space [get_bd_addr_spaces zynq_ultra_ps_e_0/Data] [get_bd_addr_segs v_tc_0/ctrl/Reg] -force


  # Restore current instance
  current_bd_instance $oldCurInst

  validate_bd_design
  save_bd_design
  close_bd_design $design_name 
}
# End of cr_bd_system()

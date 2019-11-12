# Proc to create BD fractal_all_bd
proc cr_bd_fractal_all_bd { srcset {parentCell ""} } {
# The design that will be created by this Tcl proc contains the following 
# module references:
# fractal



  # CHANGE DESIGN NAME HERE
  set design_name fractal_all_bd

  common::send_msg_id "BD_TCL-003" "INFO" "Currently there is no design <$design_name> in project, so creating one..."

  if { $srcset eq "" } {
    create_bd_design $design_name
  } else {
    create_bd_design -srcset $srcset $design_name
  }

  set bCheckIPsPassed 1
  ##################################################################
  # CHECK IPs
  ##################################################################
  set bCheckIPs 1
  if { $bCheckIPs == 1 } {
     set list_check_ips "\ 
  xilinx.com:ip:axi_vip:1.1\
  "

   set list_ips_missing ""
   common::send_msg_id "BD_TCL-006" "INFO" "Checking if the following IPs exist in the project's IP catalog: $list_check_ips ."

   foreach ip_vlnv $list_check_ips {
      set ip_obj [get_ipdefs -all $ip_vlnv]
      if { $ip_obj eq "" } {
         lappend list_ips_missing $ip_vlnv
      }
   }

   if { $list_ips_missing ne "" } {
      catch {common::send_msg_id "BD_TCL-115" "ERROR" "The following IPs are not found in the IP Catalog:\n  $list_ips_missing\n\nResolution: Please add the repository containing the IP(s) to the project." }
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
   common::send_msg_id "BD_TCL-006" "INFO" "Checking if the following modules exist in the project's sources: $list_check_mods ."

   foreach mod_vlnv $list_check_mods {
      if { [can_resolve_reference $mod_vlnv] == 0 } {
         lappend list_mods_missing $mod_vlnv
      }
   }

   if { $list_mods_missing ne "" } {
      catch {common::send_msg_id "BD_TCL-115" "ERROR" "The following module(s) are not found in the project: $list_mods_missing" }
      common::send_msg_id "BD_TCL-008" "INFO" "Please add source files for the missing module(s) above."
      set bCheckIPsPassed 0
   }
}

  if { $bCheckIPsPassed != 1 } {
    common::send_msg_id "BD_TCL-1003" "WARNING" "Will not continue with creation of design due to the error(s) above."
    return 3
  }

  variable script_folder

  if { $parentCell eq "" } {
     set parentCell [get_bd_cells /]
  }

  # Get object for parentCell
  set parentObj [get_bd_cells $parentCell]
  if { $parentObj == "" } {
     catch {common::send_msg_id "BD_TCL-100" "ERROR" "Unable to find parent cell <$parentCell>!"}
     return
  }

  # Make sure parentObj is hier blk
  set parentType [get_property TYPE $parentObj]
  if { $parentType ne "hier" } {
     catch {common::send_msg_id "BD_TCL-101" "ERROR" "Parent <$parentObj> has TYPE = <$parentType>. Expected to be <hier>."}
     return
  }

  # Save current instance; Restore later
  set oldCurInst [current_bd_instance .]

  # Set parent object as current
  current_bd_instance $parentObj


  # Create interface ports

  # Create ports
  set aclk [ create_bd_port -dir I -type clk -freq_hz 100000000 aclk ]
  set aresetn [ create_bd_port -dir I -type rst aresetn ]
  set m_axis_tdata [ create_bd_port -dir O -from 23 -to 0 m_axis_tdata ]
  set m_axis_tlast [ create_bd_port -dir O m_axis_tlast ]
  set m_axis_tuser [ create_bd_port -dir O -from 0 -to 0 m_axis_tuser ]
  set m_axis_tvalid [ create_bd_port -dir O m_axis_tvalid ]

  # Create instance: axi_vip_0, and set properties
  set axi_vip_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_vip:1.1 axi_vip_0 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {32} \
   CONFIG.ARUSER_WIDTH {0} \
   CONFIG.AWUSER_WIDTH {0} \
   CONFIG.BUSER_WIDTH {0} \
   CONFIG.DATA_WIDTH {32} \
   CONFIG.HAS_BRESP {1} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_CACHE {0} \
   CONFIG.HAS_LOCK {0} \
   CONFIG.HAS_PROT {1} \
   CONFIG.HAS_QOS {0} \
   CONFIG.HAS_REGION {0} \
   CONFIG.HAS_RRESP {1} \
   CONFIG.HAS_WSTRB {1} \
   CONFIG.ID_WIDTH {0} \
   CONFIG.INTERFACE_MODE {MASTER} \
   CONFIG.PROTOCOL {AXI4LITE} \
   CONFIG.READ_WRITE_MODE {READ_WRITE} \
   CONFIG.RUSER_BITS_PER_BYTE {0} \
   CONFIG.RUSER_WIDTH {0} \
   CONFIG.SUPPORTS_NARROW {0} \
   CONFIG.WUSER_BITS_PER_BYTE {0} \
   CONFIG.WUSER_WIDTH {0} \
 ] $axi_vip_0

  # Create instance: fractal_0, and set properties
  set block_name fractal
  set block_cell_name fractal_0
  if { [catch {set fractal_0 [create_bd_cell -type module -reference $block_name $block_cell_name] } errmsg] } {
     catch {common::send_msg_id "BD_TCL-105" "ERROR" "Unable to add referenced block <$block_name>. Please add the files for ${block_name}'s definition into the project."}
     return 1
   } elseif { $fractal_0 eq "" } {
     catch {common::send_msg_id "BD_TCL-106" "ERROR" "Unable to referenced block <$block_name>. Please add the files for ${block_name}'s definition into the project."}
     return 1
   }
    set_property -dict [ list \
   CONFIG.NUM_PARALLELS {29} \
   CONFIG.OUTPUT_HEIGHT {216} \
   CONFIG.OUTPUT_WIDTH {384} \
 ] $fractal_0

  # Create interface connections
  connect_bd_intf_net -intf_net axi_vip_0_M_AXI [get_bd_intf_pins axi_vip_0/M_AXI] [get_bd_intf_pins fractal_0/s_axi]

  # Create port connections
  connect_bd_net -net Net [get_bd_ports aclk] [get_bd_pins axi_vip_0/aclk] [get_bd_pins fractal_0/aclk]
  connect_bd_net -net Net1 [get_bd_ports aresetn] [get_bd_pins axi_vip_0/aresetn] [get_bd_pins fractal_0/aresetn]
  connect_bd_net -net fractal_0_m_axis_tdata [get_bd_ports m_axis_tdata] [get_bd_pins fractal_0/m_axis_tdata]
  connect_bd_net -net fractal_0_m_axis_tlast [get_bd_ports m_axis_tlast] [get_bd_pins fractal_0/m_axis_tlast]
  connect_bd_net -net fractal_0_m_axis_tuser [get_bd_ports m_axis_tuser] [get_bd_pins fractal_0/m_axis_tuser]
  connect_bd_net -net fractal_0_m_axis_tvalid [get_bd_ports m_axis_tvalid] [get_bd_pins fractal_0/m_axis_tvalid]

  # Create address segments
  assign_bd_address -offset 0x44A00000 -range 0x00010000 -target_address_space [get_bd_addr_spaces axi_vip_0/Master_AXI] [get_bd_addr_segs fractal_0/s_axi/reg0] -force


  # Restore current instance
  current_bd_instance $oldCurInst

  validate_bd_design
  save_bd_design
  close_bd_design $design_name 
}
# End of cr_bd_fractal_all_bd()

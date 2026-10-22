# Run only on a BACKUP/COPY of the user's existing soc_system.qsys.
# The component folder capture_ip must be next to this file and discoverable
# in the Qsys IP search path. See QSYS_STEPS.md for the GUI equivalent.
package require qsys
load_system soc_system.qsys
if {[lsearch -exact [get_instances] hps_snapshot_0] >= 0} {
    error "hps_snapshot_0 already exists. This script is for the original Qsys file; do not add it twice."
}

# Extra elasticity for the existing DDR -> CVO video path.
set_instance_parameter_value alt_vip_cl_cvo_0 FIFO_DEPTH 8192
set_instance_parameter_value alt_vip_cl_cvo_0 THRESHOLD 4095
set_instance_parameter_value alt_vip_cl_vfb_0 READ_FIFO_DEPTH 1024

# Keep HPS DDR settings, PLLs, existing game PIO addresses and video links.
add_instance hps_snapshot_0 hps_snapshot 1.0
set_instance_parameter_value hps_snapshot_0 SAMPLE_LOG2 2
add_connection pll_0.outclk2 hps_snapshot_0.clock
add_connection clk_50.clk_reset hps_snapshot_0.reset
add_connection hps_ddr3.h2f_reset hps_snapshot_0.reset
add_connection hps_ddr3.hps_h2f_lw_axi_master hps_snapshot_0.csr
set_connection_parameter_value hps_ddr3.hps_h2f_lw_axi_master/hps_snapshot_0.csr baseAddress 0x0100
add_connection hps_snapshot_0.master hps_ddr3.hps_f2h_sdram0_data
set_connection_parameter_value hps_snapshot_0.master/hps_ddr3.hps_f2h_sdram0_data baseAddress 0x0000
set_connection_parameter_value hps_snapshot_0.master/hps_ddr3.hps_f2h_sdram0_data arbitrationPriority 1
add_interface snapshot_video_clk clock end
set_interface_property snapshot_video_clk EXPORT_OF hps_snapshot_0.video_clk
add_interface snapshot_video conduit end
set_interface_property snapshot_video EXPORT_OF hps_snapshot_0.video
save_system soc_system.qsys

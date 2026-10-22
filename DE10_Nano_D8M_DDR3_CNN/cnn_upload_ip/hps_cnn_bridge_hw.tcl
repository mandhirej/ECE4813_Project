package require qsys
set_module_property NAME hps_cnn_bridge
set_module_property VERSION 1.0
set_module_property DISPLAY_NAME "HPS CNN crop upload"
set_module_property GROUP "User IP"
set_module_property INSTANTIATE_IN_SYSTEM_MODULE true
add_fileset QUARTUS_SYNTH QUARTUS_SYNTH "" ""
set_fileset_property QUARTUS_SYNTH TOP_LEVEL hps_cnn_bridge
add_fileset_file hps_cnn_bridge.v VERILOG PATH hps_cnn_bridge.v TOP_LEVEL_FILE
add_interface clock clock end
set_interface_property clock clockRate 50000000
add_interface_port clock clk clk Input 1
add_interface reset reset end
set_interface_property reset associatedClock clock
set_interface_property reset synchronousEdges DEASSERT
add_interface_port reset reset reset Input 1
add_interface csr avalon end
set_interface_property csr associatedClock clock
set_interface_property csr associatedReset reset
set_interface_property csr addressUnits WORDS
set_interface_property csr readLatency 0
set_interface_property csr maximumPendingReadTransactions 0
add_interface_port csr csr_address address Input 4
add_interface_port csr csr_read read Input 1
add_interface_port csr csr_write write Input 1
add_interface_port csr csr_byteenable byteenable Input 4
add_interface_port csr csr_writedata writedata Input 32
add_interface_port csr csr_readdata readdata Output 32
add_interface_port csr csr_waitrequest waitrequest Output 1
add_interface cnn conduit end
set_interface_property cnn associatedClock clock
set_interface_property cnn associatedReset reset
add_interface_port cnn pixel_clear pixel_clear Output 1
add_interface_port cnn pixel_we pixel_we Output 1
add_interface_port cnn pixel_addr pixel_addr Output 12
add_interface_port cnn pixel_data pixel_data Output 8
add_interface_port cnn start start Output 1
add_interface_port cnn core_reset_n core_reset_n Output 1
add_interface_port cnn core_busy core_busy Input 1
add_interface_port cnn core_done core_done Input 1
add_interface_port cnn core_class core_class Input 6
add_interface_port cnn image_hash image_hash Input 32
add_interface_port cnn image_count image_count Input 13
add_interface_port cnn bbox_valid bbox_valid Output 1
add_interface_port cnn bbox_min_x bbox_min_x Output 11
add_interface_port cnn bbox_max_x bbox_max_x Output 11
add_interface_port cnn bbox_min_y bbox_min_y Output 11
add_interface_port cnn bbox_max_y bbox_max_y Output 11

package require qsys
set_module_property NAME hps_snapshot
set_module_property VERSION 1.0
set_module_property DISPLAY_NAME "HPS one-shot RGB snapshot"
set_module_property GROUP "User IP"
set_module_property DESCRIPTION "One-shot, non-blocking CVO RGB capture into HPS DDR"
set_module_property INSTANTIATE_IN_SYSTEM_MODULE true
set_module_property EDITABLE true
add_fileset QUARTUS_SYNTH QUARTUS_SYNTH "" ""
set_fileset_property QUARTUS_SYNTH TOP_LEVEL hps_snapshot
add_fileset_file hps_snapshot.v VERILOG PATH hps_snapshot.v TOP_LEVEL_FILE
add_parameter SAMPLE_LOG2 INTEGER 2
set_parameter_property SAMPLE_LOG2 DISPLAY_NAME "Sampling shift (2 = 480x270)"
set_parameter_property SAMPLE_LOG2 ALLOWED_RANGES {0 1 2}
set_parameter_property SAMPLE_LOG2 HDL_PARAMETER true
add_interface clock clock end
add_interface_port clock clk clk Input 1
add_interface reset reset end
set_interface_property reset associatedClock clock
set_interface_property reset synchronousEdges DEASSERT
add_interface_port reset reset reset Input 1
add_interface video_clk clock end
add_interface_port video_clk video_clk clk Input 1
add_interface video conduit end
set_interface_property video associatedClock video_clk
add_interface_port video video_r r Input 8
add_interface_port video video_g g Input 8
add_interface_port video video_b b Input 8
add_interface_port video video_vs vs Input 1
add_interface_port video video_de de Input 1
add_interface csr avalon end
set_interface_property csr associatedClock clock
set_interface_property csr associatedReset reset
set_interface_property csr addressUnits WORDS
set_interface_property csr readLatency 0
set_interface_property csr maximumPendingReadTransactions 0
add_interface_port csr csr_address address Input 4
add_interface_port csr csr_read read Input 1
add_interface_port csr csr_write write Input 1
add_interface_port csr csr_writedata writedata Input 32
add_interface_port csr csr_readdata readdata Output 32
add_interface_port csr csr_waitrequest waitrequest Output 1
add_interface master avalon start
set_interface_property master associatedClock clock
set_interface_property master associatedReset reset
set_interface_property master addressUnits SYMBOLS
set_interface_property master maximumPendingReadTransactions 1
add_interface_port master master_address address Output 32
add_interface_port master master_writedata writedata Output 32
add_interface_port master master_byteenable byteenable Output 4
add_interface_port master master_write write Output 1
add_interface_port master master_read read Output 1
add_interface_port master master_waitrequest waitrequest Input 1
add_interface_port master master_readdata readdata Input 32
add_interface_port master master_readdatavalid readdatavalid Input 1

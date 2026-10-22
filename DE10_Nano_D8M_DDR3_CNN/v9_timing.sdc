# Add AFTER the project's existing clocks / generated clocks constraints.
# Keep the original HDMI output timing constraints. Do not cut HDMI paths.

# SHADER input coordinates and state are held for four 50-MHz cycles.
# RGB is captured only when render_phase==3; RAM writes happen next cycle.
set v9_shader_sources [get_registers {*DISPLAY|render_x[*] *DISPLAY|render_y[*] *DISPLAY|*_slow*}]
set v9_shader_dest [get_registers {*DISPLAY|*SHADER|rgb[*]}]
if {[get_collection_size $v9_shader_sources] && [get_collection_size $v9_shader_dest]} {
    set_multicycle_path -setup -from $v9_shader_sources -to $v9_shader_dest 4
    set_multicycle_path -hold -from $v9_shader_sources -to $v9_shader_dest 3
} else {
    post_message -type critical_warning "V9 shader multicycle endpoints not found: inspect post-synthesis register names."
}

# Single-bit handshake synchronizers: cut only the first metastability stage.
set v9_meta [get_registers {*DISPLAY|req_meta *DISPLAY|ack_meta}]
if {[get_collection_size $v9_meta]} {set_false_path -to $v9_meta}

# Bundled payloads are held until their request/acknowledgement crosses two
# synchronizers. Bound their routing delay below one destination clock period.
set v9_request_src [get_registers {*DISPLAY|requested_row[*]}]
set v9_request_dst [get_registers {*DISPLAY|render_y[*]}]
set v9_frame_src [get_registers {*DISPLAY|*_frame*}]
set v9_frame_dst [get_registers {*DISPLAY|*_slow*}]
set v9_done_src [get_registers {*DISPLAY|completed_row[*]}]
set v9_done_dst [get_registers {*DISPLAY|ready_row0[*] *DISPLAY|ready_row1[*] *DISPLAY|line_ready[*]}]
foreach pair [list [list $v9_request_src $v9_request_dst] [list $v9_frame_src $v9_frame_dst] [list $v9_done_src $v9_done_dst]] {
    set src [lindex $pair 0]
    set dst [lindex $pair 1]
    if {[get_collection_size $src] && [get_collection_size $dst]} {
        set_max_delay -from $src -to $dst 100
        set_min_delay -from $src -to $dst -100
        set_net_delay -from $src -to $dst -max 5.0
    }
}

# Optional snapshot IP CDC. Gray pointer routing must remain within the
# fastest clock period (~6.734 ns); do NOT replace this with broad false paths.
foreach pointer {wg rg} {
    set src [get_registers "*hps_snapshot_0*|${pointer}\[*\]"]
    set dst [get_registers "*hps_snapshot_0*|${pointer}_meta\[*\]"]
    if {[get_collection_size $src] && [get_collection_size $dst]} {
        set_max_delay -from $src -to $dst 100
        set_min_delay -from $src -to $dst -100
        set_net_delay -from $src -to $dst -max 5.0
        set_max_skew -from $src -to $dst 5.0
    }
}
set snapshot_meta [get_registers {*hps_snapshot_0*|req_meta *hps_snapshot_0*|done_meta}]
if {[get_collection_size $snapshot_meta]} {set_false_path -to $snapshot_meta}
set snapshot_err_src [get_registers {*hps_snapshot_0*|video_error[*]}]
set snapshot_err_dst [get_registers {*hps_snapshot_0*|error *hps_snapshot_0*|error_code[*]}]
if {[get_collection_size $snapshot_err_src] && [get_collection_size $snapshot_err_dst]} {
    set_max_delay -from $snapshot_err_src -to $snapshot_err_dst 100
    set_min_delay -from $snapshot_err_src -to $snapshot_err_dst -100
    set_net_delay -from $snapshot_err_src -to $snapshot_err_dst -max 5.0
}

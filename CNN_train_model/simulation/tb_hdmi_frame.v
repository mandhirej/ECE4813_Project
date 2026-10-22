`timescale 1ps/1ps
// Renders HDMI frames from memory_display_v9 (UI shader + camera thumbnail + grid
// overlay) and saves the third frame as frame.ppm.
// 1080p active area with short blanking; 148.5 MHz pixel clock, 50 MHz render clock.
module tb_hdmi_frame;
    localparam HTOT = 1960, VTOT = 1090;   // 1920x1080 active
    reg pixel_clk = 0, render_clk = 0, reset_n = 0;
    always #3367 pixel_clk = ~pixel_clk;     // 148.5 MHz
    always #10000 render_clk = ~render_clk;  // 50 MHz

    reg [10:0] hc = 0, vc = 0;
    wire de = hc < 1920 && vc < 1080;
    wire vs = vc >= 1083 && vc < 1088;
    wire hs = hc >= 1930 && hc < 1950;
    reg [23:0] cam [0:240*135-1];
    wire [23:0] cpix = cam[(vc >> 3) * 240 + (hc >> 3)];
    wire [7:0] in_r = de ? cpix[23:16] : 8'd0, in_g = de ? cpix[15:8] : 8'd0, in_b = de ? cpix[7:0] : 8'd0;

    reg [1791:0] ui_text_mem [0:0];
    wire [1791:0] ui_text = ui_text_mem[0];
    wire [7:0] out_r, out_g, out_b;
    wire out_hs, out_vs, out_de, ui_underrun;

    memory_display_v9 dut (
        .pixel_clk(pixel_clk), .render_clk(render_clk), .reset_n(reset_n),
        .in_r(in_r), .in_g(in_g), .in_b(in_b), .in_hs(hs), .in_vs(vs), .in_de(de),
        .board_cards(96'd0), .board_states(32'd0), .score(4'd2), .turn_number(8'd1),
        .remaining_cards(5'd16), .ui_action(3'd0), .ui_target_cell(4'd0), .ui_text(ui_text),
        .capture_busy(1'b0), .countdown_sec(3'd0), .capture_coord(4'd0), .class_id(6'd0),
        .class_valid(1'b0), .bbox_valid(1'b0), .bbox_min_x(11'd0), .bbox_max_x(11'd0),
        .bbox_min_y(11'd0), .bbox_max_y(11'd0),
        // D1 good, C1 bad, H1 alert, rest expected-empty; DRAW good
        .guide_cell_states(32'h0000_0039), .guide_draw_state(2'd1), .guide_mode(1'b1),
        .guide_move_mask(16'd0), .guide_target_active(1'b0),
        .turn_timer_valid(1'b1), .turn_timer_running(1'b1), .turn_timer_expired(1'b0),
        .turn_timer_opponent(1'b0), .turn_timer_seconds(7'd105),
        .out_r(out_r), .out_g(out_g), .out_b(out_b), .out_hs(out_hs), .out_vs(out_vs),
        .out_de(out_de), .ui_underrun(ui_underrun)
    );

    integer frame = 0, fd = 0, n = 0, underruns = 0;
    reg vs_d = 0, ovs_d = 0;
    always @(posedge pixel_clk) begin
        hc <= (hc == HTOT - 1) ? 11'd0 : hc + 11'd1;
        if (hc == HTOT - 1) vc <= (vc == VTOT - 1) ? 11'd0 : vc + 11'd1;
    end
    // count frames on the OUTPUT vsync so the saved frame is aligned with out_de
    always @(posedge pixel_clk) begin
        ovs_d <= out_vs;
        if (out_vs && !ovs_d) begin
            frame = frame + 1;
            $display("frame %0d starts at %0t ps, underrun flag %0d", frame, $time, ui_underrun);
            if (frame == 3) fd = $fopen("frame.ppm", "wb");
            if (frame == 3) $fwrite(fd, "P6\n1920 1080\n255\n");
            if (frame == 4) begin $fclose(fd); $display("saved frame.ppm (%0d pixels)", n); $finish; end
        end
        if (fd != 0 && frame == 3 && out_de) begin
            $fwrite(fd, "%c%c%c", out_r, out_g, out_b);
            n = n + 1;
        end
    end

    initial begin
        $readmemh("camera.hex", cam);
        $readmemh("ui_text.hex", ui_text_mem);
        #100000 reset_n = 1;
    end
endmodule

/*module cnn_accelerator_top (
    // System Clocks and Resets
    input  wire        clk_50mhz,     // FPGA System Clock
    input  wire        clk_camera,    // Camera Pixel Clock (e.g., 75MHz)
    input  wire        reset_n,
    
    // Camera Physical Pins
    input  wire        cam_vsync,
    input  wire        cam_hsync,
    input  wire [7:0]  cam_pixel_data,
    
    // Output to HPS (ARM Processor)
    output wire [5:0]  predicted_class,
    output wire        cnn_done
);

    // ===========================================================
    // 1. CROPPER & INGESTION
    // ===========================================================
    wire        crop_we;
    wire [11:0] crop_addr;
    wire [7:0]  crop_data;
    wire        frame_ready;
    wire        cnn_busy; // Declare the missing busy flag here

    hardware_cropper u_cropper (
        .clk(clk_camera),
        .reset_n(reset_n),
        .vsync(cam_vsync),
        .hsync(cam_hsync),
        // FIXED: Replaced pixel_in with the correct RGB and valid ports
        .data_valid(cam_hsync),       // Treat horizontal sync as data valid enable
        .pixel_R(cam_pixel_data),     // Feed grayscale into all channels
        .pixel_G(cam_pixel_data),
        .pixel_B(cam_pixel_data),
        .cnn_busy(cnn_busy),          // Route the busy flag
        .bram_we(crop_we),
        .bram_addr(crop_addr),
        .bram_data(crop_data),
        .frame_ready(frame_ready)
    );

    wire        l1_bram_re;
    wire [11:0] l1_bram_addr;
    wire [7:0]  bram_to_l1_data;

    cropper_bram u_image_ram (
        .clk_camera(clk_camera),
        .we(crop_we), .write_addr(crop_addr), .write_data(crop_data),
        .clk_cnn(clk_50mhz),
        .re(l1_bram_re), .read_addr(l1_bram_addr), .read_data(bram_to_l1_data)
    );

    // ===========================================================
    // 2. LAYER 1 (FSM, Line Buffer, MACs)
    // ===========================================================
    wire        l1_lb_valid_in, l1_window_valid, l1_ping_we;
    wire [7:0]  l1_lb_pixel_in;
    wire [9:0]  l1_ping_addr;
    wire [7:0]  l1_p11, l1_p12, l1_p13, l1_p21, l1_p22, l1_p23, l1_p31, l1_p32, l1_p33;
    
    // Hardcoded weights(Filters 0 through 7)
        // Filter 0
		wire signed [7:0] f0_w11 = 8'h07;
		wire signed [7:0] f0_w12 = 8'h0A;
		wire signed [7:0] f0_w13 = 8'hFE;
		wire signed [7:0] f0_w21 = 8'h0B;
		wire signed [7:0] f0_w22 = 8'hFC;
		wire signed [7:0] f0_w23 = 8'h01;
		wire signed [7:0] f0_w31 = 8'hFD;
		wire signed [7:0] f0_w32 = 8'h08;
		wire signed [7:0] f0_w33 = 8'h08;
		wire signed [31:0] f0_bias = 32'hFFFFEFFC;

		// Filter 1
		wire signed [7:0] f1_w11 = 8'hF8;
		wire signed [7:0] f1_w12 = 8'h0A;
		wire signed [7:0] f1_w13 = 8'h03;
		wire signed [7:0] f1_w21 = 8'h0C;
		wire signed [7:0] f1_w22 = 8'h01;
		wire signed [7:0] f1_w23 = 8'h07;
		wire signed [7:0] f1_w31 = 8'h02;
		wire signed [7:0] f1_w32 = 8'h09;
		wire signed [7:0] f1_w33 = 8'h00;
		wire signed [31:0] f1_bias = 32'hFFFFEE34;

		// Filter 2
		wire signed [7:0] f2_w11 = 8'hF9;
		wire signed [7:0] f2_w12 = 8'h06;
		wire signed [7:0] f2_w13 = 8'hFD;
		wire signed [7:0] f2_w21 = 8'hFA;
		wire signed [7:0] f2_w22 = 8'hF8;
		wire signed [7:0] f2_w23 = 8'h12;
		wire signed [7:0] f2_w31 = 8'hE6;
		wire signed [7:0] f2_w32 = 8'hF7;
		wire signed [7:0] f2_w33 = 8'h02;
		wire signed [31:0] f2_bias = 32'h00000F88;

		// Filter 3
		wire signed [7:0] f3_w11 = 8'hED;
		wire signed [7:0] f3_w12 = 8'h01;
		wire signed [7:0] f3_w13 = 8'hD6;
		wire signed [7:0] f3_w21 = 8'h25;
		wire signed [7:0] f3_w22 = 8'hDC;
		wire signed [7:0] f3_w23 = 8'h1C;
		wire signed [7:0] f3_w31 = 8'h09;
		wire signed [7:0] f3_w32 = 8'hF2;
		wire signed [7:0] f3_w33 = 8'h1A;
		wire signed [31:0] f3_bias = 32'h000004F5;

		// Filter 4
		wire signed [7:0] f4_w11 = 8'h00;
		wire signed [7:0] f4_w12 = 8'h0D;
		wire signed [7:0] f4_w13 = 8'hFD;
		wire signed [7:0] f4_w21 = 8'hFE;
		wire signed [7:0] f4_w22 = 8'h03;
		wire signed [7:0] f4_w23 = 8'hF8;
		wire signed [7:0] f4_w31 = 8'h09;
		wire signed [7:0] f4_w32 = 8'h14;
		wire signed [7:0] f4_w33 = 8'h03;
		wire signed [31:0] f4_bias = 32'hFFFFEF19;

		// Filter 5
		wire signed [7:0] f5_w11 = 8'hF8;
		wire signed [7:0] f5_w12 = 8'h1E;
		wire signed [7:0] f5_w13 = 8'h0D;
		wire signed [7:0] f5_w21 = 8'h24;
		wire signed [7:0] f5_w22 = 8'hDD;
		wire signed [7:0] f5_w23 = 8'hCF;
		wire signed [7:0] f5_w31 = 8'hF3;
		wire signed [7:0] f5_w32 = 8'hD6;
		wire signed [7:0] f5_w33 = 8'h2D;
		wire signed [31:0] f5_bias = 32'h00000B32;

		// Filter 6
		wire signed [7:0] f6_w11 = 8'h0B;
		wire signed [7:0] f6_w12 = 8'h15;
		wire signed [7:0] f6_w13 = 8'h0F;
		wire signed [7:0] f6_w21 = 8'hFF;
		wire signed [7:0] f6_w22 = 8'h21;
		wire signed [7:0] f6_w23 = 8'hDA;
		wire signed [7:0] f6_w31 = 8'h01;
		wire signed [7:0] f6_w32 = 8'hD7;
		wire signed [7:0] f6_w33 = 8'h06;
		wire signed [31:0] f6_bias = 32'hFFFFFCD2;

		// Filter 7
		wire signed [7:0] f7_w11 = 8'hF0;
		wire signed [7:0] f7_w12 = 8'h0E;
		wire signed [7:0] f7_w13 = 8'hF3;
		wire signed [7:0] f7_w21 = 8'h4D;
		wire signed [7:0] f7_w22 = 8'hD1;
		wire signed [7:0] f7_w23 = 8'hCA;
		wire signed [7:0] f7_w31 = 8'hC9;
		wire signed [7:0] f7_w32 = 8'h40;
		wire signed [7:0] f7_w33 = 8'h1A;
		wire signed [31:0] f7_bias = 32'h0000021E;

    layer1_state_machine u_l1_fsm (
        .clk(clk_50mhz), .reset_n(reset_n),
        .frame_ready(frame_ready), .cnn_busy(cnn_busy), // FIXED: Connect cnn_busy here
        .bram_en(l1_bram_re), .bram_addr(l1_bram_addr), .bram_data(bram_to_l1_data),
        .lb_valid_in(l1_lb_valid_in), .lb_pixel_in(l1_lb_pixel_in),
        .ping_we(l1_ping_we), .ping_addr(l1_ping_addr)
    );

    line_buffer_3x3 u_l1_lb (
        .clk(clk_50mhz), .valid_in(l1_lb_valid_in), .pixel_in(l1_lb_pixel_in), // FIXED: Removed .reset_n
        .p11(l1_p11), .p12(l1_p12), .p13(l1_p13),
        .p21(l1_p21), .p22(l1_p22), .p23(l1_p23),
        .p31(l1_p31), .p32(l1_p32), .p33(l1_p33),
        .window_valid(l1_window_valid)
    );

    // Layer 1 MAC Array (Instantiate 0 through 7)
    wire [7:0] l1_out_ch0, l1_out_ch1, l1_out_ch2, l1_out_ch3, l1_out_ch4, l1_out_ch5, l1_out_ch6, l1_out_ch7;
    
    conv_3x3_mac u_l1_mac0 (
        .clk(clk_50mhz), .reset_n(reset_n), .valid_in(l1_window_valid),
        .p11(l1_p11), .p12(l1_p12), .p13(l1_p13), .p21(l1_p21), .p22(l1_p22), .p23(l1_p23), .p31(l1_p31), .p32(l1_p32), .p33(l1_p33),
        .w11(f0_w11), .w12(f0_w12), .w13(f0_w13), .w21(f0_w21), .w22(f0_w22), .w23(f0_w23), .w31(f0_w31), .w32(f0_w32), .w33(f0_w33),
        .bias(f0_bias), .shift_amount(4'd3), .pixel_out(l1_out_ch0)
    );
    
     conv_3x3_mac u_l1_mac1 (
        .clk(clk_50mhz), .reset_n(reset_n), .valid_in(l1_window_valid),
        .p11(l1_p11), .p12(l1_p12), .p13(l1_p13), .p21(l1_p21), .p22(l1_p22), .p23(l1_p23), .p31(l1_p31), .p32(l1_p32), .p33(l1_p33),
        .w11(f1_w11), .w12(f1_w12), .w13(f1_w13), .w21(f1_w21), .w22(f1_w22), .w23(f1_w23), .w31(f1_w31), .w32(f1_w32), .w33(f1_w33),
        .bias(f1_bias), .shift_amount(4'd3), .pixel_out(l1_out_ch1)
    );
    
     conv_3x3_mac u_l1_mac2 (
        .clk(clk_50mhz), .reset_n(reset_n), .valid_in(l1_window_valid),
        .p11(l1_p11), .p12(l1_p12), .p13(l1_p13), .p21(l1_p21), .p22(l1_p22), .p23(l1_p23), .p31(l1_p31), .p32(l1_p32), .p33(l1_p33),
        .w11(f2_w11), .w12(f2_w12), .w13(f2_w13), .w21(f2_w21), .w22(f2_w22), .w23(f2_w23), .w31(f2_w31), .w32(f2_w32), .w33(f2_w33),
        .bias(f2_bias), .shift_amount(4'd3), .pixel_out(l1_out_ch2)
    );
    
     conv_3x3_mac u_l1_mac3 (
        .clk(clk_50mhz), .reset_n(reset_n), .valid_in(l1_window_valid),
        .p11(l1_p11), .p12(l1_p12), .p13(l1_p13), .p21(l1_p21), .p22(l1_p22), .p23(l1_p23), .p31(l1_p31), .p32(l1_p32), .p33(l1_p33),
        .w11(f3_w11), .w12(f3_w12), .w13(f3_w13), .w21(f3_w21), .w22(f3_w22), .w23(f3_w23), .w31(f3_w31), .w32(f3_w32), .w33(f3_w33),
        .bias(f3_bias), .shift_amount(4'd3), .pixel_out(l1_out_ch3)
    );
    
     conv_3x3_mac u_l1_mac4 (
        .clk(clk_50mhz), .reset_n(reset_n), .valid_in(l1_window_valid),
        .p11(l1_p11), .p12(l1_p12), .p13(l1_p13), .p21(l1_p21), .p22(l1_p22), .p23(l1_p23), .p31(l1_p31), .p32(l1_p32), .p33(l1_p33),
        .w11(f4_w11), .w12(f4_w12), .w13(f4_w13), .w21(f4_w21), .w22(f4_w22), .w23(f4_w23), .w31(f4_w31), .w32(f4_w32), .w33(f4_w33),
        .bias(f4_bias), .shift_amount(4'd3), .pixel_out(l1_out_ch4)
    );
    
     conv_3x3_mac u_l1_mac5 (
        .clk(clk_50mhz), .reset_n(reset_n), .valid_in(l1_window_valid),
        .p11(l1_p11), .p12(l1_p12), .p13(l1_p13), .p21(l1_p21), .p22(l1_p22), .p23(l1_p23), .p31(l1_p31), .p32(l1_p32), .p33(l1_p33),
        .w11(f5_w11), .w12(f5_w12), .w13(f5_w13), .w21(f5_w21), .w22(f5_w22), .w23(f5_w23), .w31(f5_w31), .w32(f5_w32), .w33(f5_w33),
        .bias(f5_bias), .shift_amount(4'd3), .pixel_out(l1_out_ch5)
    );
    
     conv_3x3_mac u_l1_mac6 (
        .clk(clk_50mhz), .reset_n(reset_n), .valid_in(l1_window_valid),
        .p11(l1_p11), .p12(l1_p12), .p13(l1_p13), .p21(l1_p21), .p22(l1_p22), .p23(l1_p23), .p31(l1_p31), .p32(l1_p32), .p33(l1_p33),
        .w11(f6_w11), .w12(f6_w12), .w13(f6_w13), .w21(f6_w21), .w22(f6_w22), .w23(f6_w23), .w31(f6_w31), .w32(f6_w32), .w33(f6_w33),
        .bias(f6_bias), .shift_amount(4'd3), .pixel_out(l1_out_ch6)
    );

    conv_3x3_mac u_l1_mac7 (
        .clk(clk_50mhz), .reset_n(reset_n), .valid_in(l1_window_valid),
        .p11(l1_p11), .p12(l1_p12), .p13(l1_p13), .p21(l1_p21), .p22(l1_p22), .p23(l1_p23), .p31(l1_p31), .p32(l1_p32), .p33(l1_p33),
        .w11(f7_w11), .w12(f7_w12), .w13(f7_w13), .w21(f7_w21), .w22(f7_w22), .w23(f7_w23), .w31(f7_w31), .w32(f7_w32), .w33(f7_w33),
        .bias(f7_bias), .shift_amount(4'd3), .pixel_out(l1_out_ch7)
    );

    // ===========================================================
    // 3. PING RAM (32x32)
    // ===========================================================
    wire        l2_ping_re;
    wire [9:0]  l2_ping_addr;
    wire [63:0] ping_to_l2_data;

    ping_ram u_ping_ram (
        .clk(clk_50mhz),
        .we(l1_ping_we), .write_addr(l1_ping_addr), 
        .ch0_in(l1_out_ch0), .ch1_in(l1_out_ch1), .ch2_in(l1_out_ch2),
          .ch3_in(l1_out_ch3), .ch4_in(l1_out_ch4), .ch5_in(l1_out_ch5), 
          .ch6_in(l1_out_ch6), .ch7_in(l1_out_ch7),
        .read_addr(l2_ping_addr), .read_data(ping_to_l2_data)
    );

    // ===========================================================
    // 4. LAYER 2 (FSM, Fat Line Buffer, Weight Loaders, MACs)
    // ===========================================================
    wire        l1_done, l2_done; 
    assign l1_done = l1_ping_we & (l1_ping_addr == 1023); // Pulse when L1 writes last pixel

    wire        l2_lb_valid_in, l2_window_valid, l2_pong_we, l2_load_en, l2_pass_num;
    wire [63:0] l2_lb_pixel_in;
    wire [8:0]  l2_pong_addr;
    wire [63:0] l2_p11, l2_p12, l2_p13, l2_p21, l2_p22, l2_p23, l2_p31, l2_p32, l2_p33;

    layer2_state_machine u_l2_fsm (
        .clk(clk_50mhz), .reset_n(reset_n),
        .layer1_done(l1_done), .layer2_done(l2_done),
        .load_weights_en(l2_load_en), .pass_number(l2_pass_num),
        .ping_re(l2_ping_re), .ping_addr(l2_ping_addr), .ping_data(ping_to_l2_data),
        .lb_valid_in(l2_lb_valid_in), .lb_pixel_in(l2_lb_pixel_in),
        .pong_we(l2_pong_we), .pong_addr(l2_pong_addr)
    );

    c8_line_buffer_3x3 u_l2_lb (
        .clk(clk_50mhz), .reset_n(reset_n), .valid_in(l2_lb_valid_in), .pixel_in(l2_lb_pixel_in),
        .p11(l2_p11), .p12(l2_p12), .p13(l2_p13), 
        .p21(l2_p21), .p22(l2_p22), .p23(l2_p23), 
        .p31(l2_p31), .p32(l2_p32), .p33(l2_p33),
        .window_valid(l2_window_valid)
    );
    
        // ===========================================
        // LAYER 2 BIASES
        // ===========================================
         wire signed [31:0] l2_m0_b0 = 32'h00001500;
			wire signed [31:0] l2_m0_b1 = 32'h00008BEE;
			wire signed [31:0] l2_m1_b0 = 32'h000002D7;
			wire signed [31:0] l2_m1_b1 = 32'h0000040B;
			wire signed [31:0] l2_m2_b0 = 32'h00007081;
			wire signed [31:0] l2_m2_b1 = 32'hFFFFF416;
			wire signed [31:0] l2_m3_b0 = 32'h00000092;
			wire signed [31:0] l2_m3_b1 = 32'h000060C0;
			wire signed [31:0] l2_m4_b0 = 32'h00004267;
			wire signed [31:0] l2_m4_b1 = 32'hFFFFF875;
			wire signed [31:0] l2_m5_b0 = 32'hFFFFEF1A;
			wire signed [31:0] l2_m5_b1 = 32'hFFFFE20A;
			wire signed [31:0] l2_m6_b0 = 32'hFFFFF9D6;
			wire signed [31:0] l2_m6_b1 = 32'h00001E89;
			wire signed [31:0] l2_m7_b0 = 32'hFFFFF4B7;
			wire signed [31:0] l2_m7_b1 = 32'hFFFFC661;

    // Layer 2 MAC 0 & Loader
    wire [63:0] m0_w11, m0_w12, m0_w13, m0_w21, m0_w22, m0_w23, m0_w31, m0_w32, m0_w33;
    wire [7:0]  l2_out_ch0;
    
    layer2_weight_manager #( .HEX_FILE("weights_mac0.hex") ) u_l2_ldr0 (
        .clk(clk_50mhz), .reset_n(reset_n), .state(l2_load_en), .pass_number(l2_pass_num),
        .w11(m0_w11), .w12(m0_w12), .w13(m0_w13), .w21(m0_w21), .w22(m0_w22), .w23(m0_w23), .w31(m0_w31), .w32(m0_w32), .w33(m0_w33)
    );
    
    conv_layer2_mac u_l2_mac0 (
        .clk(clk_50mhz), .reset_n(reset_n), .valid_in(l2_window_valid),
        .p11(l2_p11), .p12(l2_p12), .p13(l2_p13), .p21(l2_p21), .p22(l2_p22), .p23(l2_p23), .p31(l2_p31), .p32(l2_p32), .p33(l2_p33),
        .w11(m0_w11), .w12(m0_w12), .w13(m0_w13), .w21(m0_w21), .w22(m0_w22), .w23(m0_w23), .w31(m0_w31), .w32(m0_w32), .w33(m0_w33),
        .bias((l2_pass_num == 0) ? l2_m0_b0 : l2_m0_b1), .shift_amount(4'd3), .pixel_out(l2_out_ch0)
    );

     // Layer 2 MAC 1 & Loader
     wire [63:0] m1_w11, m1_w12, m1_w13, m1_w21, m1_w22, m1_w23, m1_w31, m1_w32, m1_w33;
    wire [7:0]  l2_out_ch1;
    layer2_weight_manager #( .HEX_FILE("weights_mac1.hex") ) u_l2_ldr1 (
        .clk(clk_50mhz), .reset_n(reset_n), .state(l2_load_en), .pass_number(l2_pass_num),
        .w11(m1_w11), .w12(m1_w12), .w13(m1_w13), .w21(m1_w21), .w22(m1_w22), .w23(m1_w23), .w31(m1_w31), .w32(m1_w32), .w33(m1_w33)
    );
    
    conv_layer2_mac u_l2_mac1 (
        .clk(clk_50mhz), .reset_n(reset_n), .valid_in(l2_window_valid),
        .p11(l2_p11), .p12(l2_p12), .p13(l2_p13), .p21(l2_p21), .p22(l2_p22), .p23(l2_p23), .p31(l2_p31), .p32(l2_p32), .p33(l2_p33),
        .w11(m1_w11), .w12(m1_w12), .w13(m1_w13), .w21(m1_w21), .w22(m1_w22), .w23(m1_w23), .w31(m1_w31), .w32(m1_w32), .w33(m1_w33),
        .bias((l2_pass_num == 0) ? l2_m1_b0 : l2_m1_b1), .shift_amount(4'd3), .pixel_out(l2_out_ch1)
    );

     // Layer 2 MAC 2 & Loader
     wire [63:0] m2_w11, m2_w12, m2_w13, m2_w21, m2_w22, m2_w23, m2_w31, m2_w32, m2_w33;
    wire [7:0]  l2_out_ch2;
    layer2_weight_manager #( .HEX_FILE("weights_mac2.hex") ) u_l2_ldr2 (
        .clk(clk_50mhz), .reset_n(reset_n), .state(l2_load_en), .pass_number(l2_pass_num),
        .w11(m2_w11), .w12(m2_w12), .w13(m2_w13), .w21(m2_w21), .w22(m2_w22), .w23(m2_w23), .w31(m2_w31), .w32(m2_w32), .w33(m2_w33)
    );
    
    conv_layer2_mac u_l2_mac2 (
        .clk(clk_50mhz), .reset_n(reset_n), .valid_in(l2_window_valid),
        .p11(l2_p11), .p12(l2_p12), .p13(l2_p13), .p21(l2_p21), .p22(l2_p22), .p23(l2_p23), .p31(l2_p31), .p32(l2_p32), .p33(l2_p33),
        .w11(m2_w11), .w12(m2_w12), .w13(m2_w13), .w21(m2_w21), .w22(m2_w22), .w23(m2_w23), .w31(m2_w31), .w32(m2_w32), .w33(m2_w33),
        .bias((l2_pass_num == 0) ? l2_m2_b0 : l2_m2_b1), .shift_amount(4'd3), .pixel_out(l2_out_ch2)
    );
    
     // Layer 2 MAC 3 & Loader
     wire [63:0] m3_w11, m3_w12, m3_w13, m3_w21, m3_w22, m3_w23, m3_w31, m3_w32, m3_w33;
    wire [7:0]  l2_out_ch3;
    layer2_weight_manager #( .HEX_FILE("weights_mac3.hex") ) u_l2_ldr3 (
        .clk(clk_50mhz), .reset_n(reset_n), .state(l2_load_en), .pass_number(l2_pass_num),
        .w11(m3_w11), .w12(m3_w12), .w13(m3_w13), .w21(m3_w21), .w22(m3_w22), .w23(m3_w23), .w31(m3_w31), .w32(m3_w32), .w33(m3_w33)
    );
    
    conv_layer2_mac u_l2_mac3 (
        .clk(clk_50mhz), .reset_n(reset_n), .valid_in(l2_window_valid),
        .p11(l2_p11), .p12(l2_p12), .p13(l2_p13), .p21(l2_p21), .p22(l2_p22), .p23(l2_p23), .p31(l2_p31), .p32(l2_p32), .p33(l2_p33),
        .w11(m3_w11), .w12(m3_w12), .w13(m3_w13), .w21(m3_w21), .w22(m3_w22), .w23(m3_w23), .w31(m3_w31), .w32(m3_w32), .w33(m3_w33),
        .bias((l2_pass_num == 0) ? l2_m3_b0 : l2_m3_b1), .shift_amount(4'd3), .pixel_out(l2_out_ch3)
    );
    
     // Layer 2 MAC 4 & Loader
     wire [63:0] m4_w11, m4_w12, m4_w13, m4_w21, m4_w22, m4_w23, m4_w31, m4_w32, m4_w33;
    wire [7:0]  l2_out_ch4;
    layer2_weight_manager #( .HEX_FILE("weights_mac4.hex") ) u_l2_ldr4 (
        .clk(clk_50mhz), .reset_n(reset_n), .state(l2_load_en), .pass_number(l2_pass_num),
        .w11(m4_w11), .w12(m4_w12), .w13(m4_w13), .w21(m4_w21), .w22(m4_w22), .w23(m4_w23), .w31(m4_w31), .w32(m4_w32), .w33(m4_w33)
    );
    
    conv_layer2_mac u_l2_mac4 (
        .clk(clk_50mhz), .reset_n(reset_n), .valid_in(l2_window_valid),
        .p11(l2_p11), .p12(l2_p12), .p13(l2_p13), .p21(l2_p21), .p22(l2_p22), .p23(l2_p23), .p31(l2_p31), .p32(l2_p32), .p33(l2_p33),
        .w11(m4_w11), .w12(m4_w12), .w13(m4_w13), .w21(m4_w21), .w22(m4_w22), .w23(m4_w23), .w31(m4_w31), .w32(m4_w32), .w33(m4_w33),
        .bias((l2_pass_num == 0) ? l2_m4_b0 : l2_m4_b1), .shift_amount(4'd3), .pixel_out(l2_out_ch4)
    );
    
     // Layer 2 MAC 5 & Loader
     wire [63:0] m5_w11, m5_w12, m5_w13, m5_w21, m5_w22, m5_w23, m5_w31, m5_w32, m5_w33;
    wire [7:0]  l2_out_ch5;
    layer2_weight_manager #( .HEX_FILE("weights_mac5.hex") ) u_l2_ldr5 (
        .clk(clk_50mhz), .reset_n(reset_n), .state(l2_load_en), .pass_number(l2_pass_num),
        .w11(m5_w11), .w12(m5_w12), .w13(m5_w13), .w21(m5_w21), .w22(m5_w22), .w23(m5_w23), .w31(m5_w31), .w32(m5_w32), .w33(m5_w33)
    );
    
    conv_layer2_mac u_l2_mac5 (
        .clk(clk_50mhz), .reset_n(reset_n), .valid_in(l2_window_valid),
        .p11(l2_p11), .p12(l2_p12), .p13(l2_p13), .p21(l2_p21), .p22(l2_p22), .p23(l2_p23), .p31(l2_p31), .p32(l2_p32), .p33(l2_p33),
        .w11(m5_w11), .w12(m5_w12), .w13(m5_w13), .w21(m5_w21), .w22(m5_w22), .w23(m5_w23), .w31(m5_w31), .w32(m5_w32), .w33(m5_w33),
        .bias((l2_pass_num == 0) ? l2_m5_b0 : l2_m5_b1), .shift_amount(4'd3), .pixel_out(l2_out_ch5)
    );
    
     // Layer 2 MAC 6 & Loader
     wire [63:0] m6_w11, m6_w12, m6_w13, m6_w21, m6_w22, m6_w23, m6_w31, m6_w32, m6_w33;
    wire [7:0]  l2_out_ch6;
    layer2_weight_manager #( .HEX_FILE("weights_mac6.hex") ) u_l2_ldr6 (
        .clk(clk_50mhz), .reset_n(reset_n), .state(l2_load_en), .pass_number(l2_pass_num),
        .w11(m6_w11), .w12(m6_w12), .w13(m6_w13), .w21(m6_w21), .w22(m6_w22), .w23(m6_w23), .w31(m6_w31), .w32(m6_w32), .w33(m6_w33)
    );
    
    conv_layer2_mac u_l2_mac6 (
        .clk(clk_50mhz), .reset_n(reset_n), .valid_in(l2_window_valid),
        .p11(l2_p11), .p12(l2_p12), .p13(l2_p13), .p21(l2_p21), .p22(l2_p22), .p23(l2_p23), .p31(l2_p31), .p32(l2_p32), .p33(l2_p33),
        .w11(m6_w11), .w12(m6_w12), .w13(m6_w13), .w21(m6_w21), .w22(m6_w22), .w23(m6_w23), .w31(m6_w31), .w32(m6_w32), .w33(m6_w33),
        .bias((l2_pass_num == 0) ? l2_m6_b0 : l2_m6_b1), .shift_amount(4'd3), .pixel_out(l2_out_ch6)
    );
    
     // Layer 2 MAC 7 & Loader
     wire [63:0] m7_w11, m7_w12, m7_w13, m7_w21, m7_w22, m7_w23, m7_w31, m7_w32, m7_w33;
    wire [7:0]  l2_out_ch7;
    layer2_weight_manager #( .HEX_FILE("weights_mac7.hex") ) u_l2_ldr7 (
        .clk(clk_50mhz), .reset_n(reset_n), .state(l2_load_en), .pass_number(l2_pass_num),
        .w11(m7_w11), .w12(m7_w12), .w13(m7_w13), .w21(m7_w21), .w22(m7_w22), .w23(m7_w23), .w31(m7_w31), .w32(m7_w32), .w33(m7_w33)
    );
    
    conv_layer2_mac u_l2_mac7 (
        .clk(clk_50mhz), .reset_n(reset_n), .valid_in(l2_window_valid),
        .p11(l2_p11), .p12(l2_p12), .p13(l2_p13), .p21(l2_p21), .p22(l2_p22), .p23(l2_p23), .p31(l2_p31), .p32(l2_p32), .p33(l2_p33),
        .w11(m7_w11), .w12(m7_w12), .w13(m7_w13), .w21(m7_w21), .w22(m7_w22), .w23(m7_w23), .w31(m7_w31), .w32(m7_w32), .w33(m7_w33),
        .bias((l2_pass_num == 0) ? l2_m7_b0 : l2_m7_b1), .shift_amount(4'd3), .pixel_out(l2_out_ch7)
    );
    
    // ===========================================================
    // 5. PONG RAM (16x16)
    // ===========================================================
    wire        lin_pong_re;
    wire [8:0]  lin_pong_addr;
    wire [63:0] pong_to_lin_data;

    pong_ram u_pong_ram (
        .clk(clk_50mhz),
        .we(l2_pong_we), .write_addr(l2_pong_addr),
        .ch0_in(l2_out_ch0), .ch1_in(l2_out_ch1), .ch2_in(l2_out_ch2),
          .ch3_in(l2_out_ch3), .ch4_in(l2_out_ch4), .ch5_in(l2_out_ch5),
          .ch6_in(l2_out_ch6), .ch7_in(l2_out_ch7), // Wires from all 8 L2 MACs
        .read_addr(lin_pong_addr), .read_data(pong_to_lin_data)
    );

    // ===========================================================
    // 6. LINEAR CLASSIFIER
    // ===========================================================
    wire        lin_rom_re;
    wire [14:0] lin_rom_addr;
    wire [63:0] lin_rom_data;

    // Generated via IP Catalog
    // FIXED: Removed the .rden port
    linear_weight_rom u_fc_rom (
        .clock(clk_50mhz),
        .address(lin_rom_addr),
        .q(lin_rom_data)
    );

    linear_classifier u_linear (
        .clk(clk_50mhz), .reset_n(reset_n),
        .layer2_done(l2_done),
        .cnn_done(cnn_done), .predicted_class(predicted_class),
        .pong_re(lin_pong_re), .pong_addr(lin_pong_addr), .pong_data(pong_to_lin_data),
        .rom_re(lin_rom_re), .rom_addr(lin_rom_addr), .rom_data(lin_rom_data)
    );

endmodule*/
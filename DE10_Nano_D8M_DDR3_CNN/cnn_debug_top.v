`timescale 1ns/1ps
`default_nettype none

module cnn_debug_top #(
    parameter INPUT_HEX = "debug_input_64x64.hex"
)(
    input  wire       FPGA_CLK1_50,
    input  wire [1:0] KEY,
    output wire [7:0] LED
);
    wire clk = FPGA_CLK1_50;
    wire reset_n = KEY[0];
    `include "cnn_quant_config.vh"

    reg [7:0] start_delay;
    reg started, debug_start;
    always @(posedge clk or negedge reset_n) begin
        if(!reset_n) begin start_delay<=0; started<=0; debug_start<=0; end
        else begin
            debug_start<=0;
            if(!started) begin
                if(start_delay==8'd40) begin debug_start<=1; started<=1; end
                else start_delay<=start_delay+1'b1;
            end
        end
    end

    wire crop_re; wire [11:0] crop_read_addr; wire [7:0] crop_read_data;
    debug_input_ram #(.INPUT_HEX(INPUT_HEX)) u_debug_input_ram(
        .clk(clk),.re(crop_re),.read_addr(crop_read_addr),.read_data(crop_read_data));

    (* keep = "true" *) wire l1_done,l1_mac_start,l1_mac_valid,l1_ping_we;
    (* keep = "true" *) wire [9:0] l1_ping_addr;
    (* keep = "true" *) wire [7:0] l1_p11,l1_p12,l1_p13,l1_p21,l1_p22,l1_p23,l1_p31,l1_p32,l1_p33;
    (* keep = "true" *) wire [7:0] l1_ch0,l1_ch1,l1_ch2,l1_ch3,l1_ch4,l1_ch5,l1_ch6,l1_ch7;

    layer1_controller u_l1_controller(
        .clk(clk),.reset_n(reset_n),.start(debug_start),.done(l1_done),
        .bram_en(crop_re),.bram_addr(crop_read_addr),.bram_data(crop_read_data),
        .mac_start(l1_mac_start),
        .p11(l1_p11),.p12(l1_p12),.p13(l1_p13),.p21(l1_p21),.p22(l1_p22),.p23(l1_p23),.p31(l1_p31),.p32(l1_p32),.p33(l1_p33),
        .mac_valid(l1_mac_valid),.ping_we(l1_ping_we),.ping_addr(l1_ping_addr));

    layer1_mac_bank #(.SHIFT(L1_SHIFT)) u_l1_bank(
        .clk(clk),.reset_n(reset_n),.start(l1_mac_start),
        .p11(l1_p11),.p12(l1_p12),.p13(l1_p13),.p21(l1_p21),.p22(l1_p22),.p23(l1_p23),.p31(l1_p31),.p32(l1_p32),.p33(l1_p33),
        .ch0(l1_ch0),.ch1(l1_ch1),.ch2(l1_ch2),.ch3(l1_ch3),.ch4(l1_ch4),.ch5(l1_ch5),.ch6(l1_ch6),.ch7(l1_ch7),.valid_out(l1_mac_valid));

    (* keep = "true" *) wire l2_ping_re; (* keep = "true" *) wire [9:0] l2_ping_addr; (* keep = "true" *) wire [63:0] ping_to_l2_data;
    ping_ram_fixed u_ping_ram(.clk(clk),.we(l1_ping_we),.write_addr(l1_ping_addr),
        .ch0_in(l1_ch0),.ch1_in(l1_ch1),.ch2_in(l1_ch2),.ch3_in(l1_ch3),.ch4_in(l1_ch4),.ch5_in(l1_ch5),.ch6_in(l1_ch6),.ch7_in(l1_ch7),
        .re(l2_ping_re),.read_addr(l2_ping_addr),.read_data(ping_to_l2_data));

    (* keep = "true" *) wire l2_done,l2_load_en,l2_pass_num,l2_mac_start,l2_mac_valid,l2_mac_busy,l2_pong_we;
    (* keep = "true" *) wire [8:0] l2_pong_addr;
    (* keep = "true" *) wire [63:0] l2_p11,l2_p12,l2_p13,l2_p21,l2_p22,l2_p23,l2_p31,l2_p32,l2_p33;
    (* keep = "true" *) wire [7:0] l2_ch0,l2_ch1,l2_ch2,l2_ch3,l2_ch4,l2_ch5,l2_ch6,l2_ch7;

    layer2_controller u_l2_controller(.clk(clk),.reset_n(reset_n),.start(l1_done),.done(l2_done),
        .load_en(l2_load_en),.pass_number(l2_pass_num),.ping_re(l2_ping_re),.ping_addr(l2_ping_addr),.ping_data(ping_to_l2_data),
        .mac_start(l2_mac_start),.p11(l2_p11),.p12(l2_p12),.p13(l2_p13),.p21(l2_p21),.p22(l2_p22),.p23(l2_p23),.p31(l2_p31),.p32(l2_p32),.p33(l2_p33),
        .mac_valid(l2_mac_valid),.pong_we(l2_pong_we),.pong_addr(l2_pong_addr));

    layer2_mac_bank #(.SHIFT(L2_SHIFT)) u_l2_bank(.clk(clk),.reset_n(reset_n),.load_en(l2_load_en),.pass_number(l2_pass_num),.start(l2_mac_start),
        .p11(l2_p11),.p12(l2_p12),.p13(l2_p13),.p21(l2_p21),.p22(l2_p22),.p23(l2_p23),.p31(l2_p31),.p32(l2_p32),.p33(l2_p33),
        .ch0(l2_ch0),.ch1(l2_ch1),.ch2(l2_ch2),.ch3(l2_ch3),.ch4(l2_ch4),.ch5(l2_ch5),.ch6(l2_ch6),.ch7(l2_ch7),.mac_valid(l2_mac_valid),.mac_busy(l2_mac_busy));

    (* keep = "true" *) wire lin_pong_re; (* keep = "true" *) wire [8:0] lin_pong_addr; (* keep = "true" *) wire [63:0] pong_to_lin_data;
    (* keep = "true" *) wire lin_rom_re; (* keep = "true" *) wire [14:0] lin_rom_addr; (* keep = "true" *) wire [63:0] lin_rom_data;
    pong_ram_fixed u_pong_ram(.clk(clk),.we(l2_pong_we),.write_addr(l2_pong_addr),
        .ch0_in(l2_ch0),.ch1_in(l2_ch1),.ch2_in(l2_ch2),.ch3_in(l2_ch3),.ch4_in(l2_ch4),.ch5_in(l2_ch5),.ch6_in(l2_ch6),.ch7_in(l2_ch7),
        .re(lin_pong_re),.read_addr(lin_pong_addr),.read_data(pong_to_lin_data));

    linear_weight_rom u_fc_rom(.clock(clk),.address(lin_rom_addr),.q(lin_rom_data));
    (* keep = "true" *) wire [5:0] predicted_class; (* keep = "true" *) wire cnn_done;
    linear_classifier_fixed u_linear(.clk(clk),.reset_n(reset_n),.layer2_done(l2_done),.class_lo(6'd0),.class_hi(6'd53),.cnn_done(cnn_done),.predicted_class(predicted_class),
        .pong_re(lin_pong_re),.pong_addr(lin_pong_addr),.pong_data(pong_to_lin_data),.rom_re(lin_rom_re),.rom_addr(lin_rom_addr),.rom_data(lin_rom_data));

    (* preserve *) reg [10:0] dbg_ping_writes;
    (* preserve *) reg [9:0] dbg_pong_writes;
    (* preserve *) reg dbg_l1_done_latched,dbg_l2_done_latched,dbg_cnn_done_latched;
    (* preserve *) reg [5:0] dbg_prediction_latched;
    always @(posedge clk or negedge reset_n) begin
        if(!reset_n) begin
            dbg_ping_writes<=0; dbg_pong_writes<=0; dbg_l1_done_latched<=0; dbg_l2_done_latched<=0; dbg_cnn_done_latched<=0; dbg_prediction_latched<=0;
        end else begin
            if(debug_start) begin dbg_ping_writes<=0; dbg_pong_writes<=0; dbg_l1_done_latched<=0; dbg_l2_done_latched<=0; dbg_cnn_done_latched<=0; end
            if(l1_ping_we) dbg_ping_writes<=dbg_ping_writes+1'b1;
            if(l2_pong_we) dbg_pong_writes<=dbg_pong_writes+1'b1;
            if(l1_done) dbg_l1_done_latched<=1;
            if(l2_done) dbg_l2_done_latched<=1;
            if(cnn_done) begin dbg_cnn_done_latched<=1; dbg_prediction_latched<=predicted_class; end
        end
    end
    assign LED={dbg_cnn_done_latched,dbg_l2_done_latched,dbg_prediction_latched};
endmodule

module debug_input_ram #(
    parameter INPUT_HEX="debug_input_64x64.hex"
)(input wire clk,input wire re,input wire [11:0] read_addr,output reg [7:0] read_data);
    (* ramstyle = "M10K" *) reg [7:0] memory [0:4095];
    initial $readmemh(INPUT_HEX,memory);
    always @(posedge clk) if(re) read_data<=memory[read_addr];
endmodule

`default_nettype wire

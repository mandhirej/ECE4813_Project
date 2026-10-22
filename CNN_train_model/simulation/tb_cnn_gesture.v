`timescale 1ns/1ps
// Drives cnn_accelerator_top exactly like the HPS bridge does (clear, 4096 pixel
// writes, bbox/mode registers, start) and prints one predicted class per image.
//   +NUM=<n>  number of images in images.hex / modes.hex
module tb_cnn_gesture;
    reg clk = 0, reset_n = 0;
    reg pixel_clear = 0, pixel_we = 0, start = 0;
    reg [11:0] pixel_addr = 0;
    reg [7:0] pixel_data = 0;
    reg bbox_valid = 0;
    reg [10:0] bbox_min_x = 0, bbox_max_x = 0, bbox_min_y = 0, bbox_max_y = 0;
    wire [5:0] predicted_class;
    wire cnn_done, busy;
    wire [31:0] image_hash;
    wire [12:0] image_count;
    wire rbv;
    wire [10:0] r0, r1, r2, r3;

    always #10 clk = ~clk;   // 50 MHz

    cnn_accelerator_top dut (
        .clk_50mhz(clk), .reset_n(reset_n),
        .pixel_clear(pixel_clear), .pixel_we(pixel_we), .pixel_addr(pixel_addr), .pixel_data(pixel_data),
        .start(start), .bbox_valid(bbox_valid),
        .bbox_min_x(bbox_min_x), .bbox_max_x(bbox_max_x), .bbox_min_y(bbox_min_y), .bbox_max_y(bbox_max_y),
        .predicted_class(predicted_class), .cnn_done(cnn_done), .busy(busy),
        .image_hash(image_hash), .image_count(image_count),
        .result_bbox_valid(rbv), .result_bbox_min_x(r0), .result_bbox_max_x(r1),
        .result_bbox_min_y(r2), .result_bbox_max_y(r3)
    );

    reg [7:0] images [0:40*4096-1];
    reg [3:0] modes [0:39];
    integer n, i, k, cycles, fd;

    initial begin
        if (!$value$plusargs("NUM=%d", n)) n = 1;
        $readmemh("images.hex", images);
        $readmemh("modes.hex", modes);
        fd = $fopen("rtl_out.txt", "w");
        repeat (5) @(posedge clk);
        reset_n = 1;
        repeat (5) @(posedge clk);
        for (k = 0; k < n; k = k + 1) begin
            @(posedge clk) pixel_clear <= 1;
            @(posedge clk) pixel_clear <= 0;
            for (i = 0; i < 4096; i = i + 1) begin
                @(posedge clk);
                pixel_we <= 1; pixel_addr <= i[11:0]; pixel_data <= images[k*4096 + i];
            end
            @(posedge clk) pixel_we <= 0;
            // mode request exactly as the HPS writes it: bbox_valid=0, min_x=0x7FF => gesture
            bbox_valid <= 0;
            bbox_min_x <= modes[k][0] ? 11'h7FF : 11'h000;
            @(posedge clk) start <= 1;
            @(posedge clk) start <= 0;
            cycles = 0;
            while (!cnn_done) begin @(posedge clk); cycles = cycles + 1; end
            $fdisplay(fd, "%0d %0d %0d", k, predicted_class, cycles);
            $display("image %0d mode %0d -> class %0d (%0d cycles)", k, modes[k], predicted_class, cycles);
            @(posedge clk);
        end
        $fclose(fd);
        $finish;
    end

    initial begin
        #2_000_000_000; // 2 s of simulated time; one card inference is ~15 ms
        $display("TIMEOUT");
        $finish;
    end
endmodule

// Behavioural stand-in for the Quartus ROM IP (altsyncram, registered address
// and registered output: q is valid two clocks after the address).
module linear_weight_rom (
    input wire [14:0] address,
    input wire clock,
    output reg [63:0] q
);
    reg [63:0] mem [0:32767];
    reg [14:0] addr_q;
    initial $readmemh("rom.hex", mem);
    always @(posedge clock) begin
        addr_q <= address;
        q <= mem[addr_q];
    end
endmodule

`timescale 1ns/1ps
// hps_snapshot V2 check: normal subsampled capture, window capture, normal again.
// Small source (64x48, SAMPLE_LOG2=2) so the run is short; the logic is the same
// as the 1920x1080 build. Pixel value = {x, y, A5} so every word can be checked.
module tb_snapshot_window;
    localparam SW = 64, SH = 48, HT = 80, VT = 56;
    reg clk = 0, vclk = 0, reset = 1;
    always #10 clk = ~clk;          // 50 MHz Avalon
    always #3.367 vclk = ~vclk;     // 148.5 MHz video

    reg [10:0] hc = 0, vc = 0;
    wire de = hc < SW && vc < SH;
    wire vs = vc >= SH + 2 && vc < SH + 4;
    always @(posedge vclk) begin
        hc <= (hc == HT - 1) ? 0 : hc + 1;
        if (hc == HT - 1) vc <= (vc == VT - 1) ? 0 : vc + 1;
    end

    reg [3:0] a; reg rd = 0, wr = 0; reg [31:0] wdata; wire [31:0] rdata; wire wreq;
    wire [31:0] maddr, mwdata; wire [3:0] mbe; wire mwrite, mread;
    reg mwait = 0; reg [31:0] mrdata = 0; reg mrvalid = 0;
    reg [31:0] mem [0:4095];

    hps_snapshot #(.SAMPLE_LOG2(2), .SOURCE_WIDTH(SW), .SOURCE_HEIGHT(SH), .FIFO_BITS(10)) dut (
        .clk(clk), .reset(reset), .video_clk(vclk),
        .video_r(hc[7:0]), .video_g(vc[7:0]), .video_b(8'hA5), .video_vs(vs), .video_de(de),
        .csr_address(a), .csr_read(rd), .csr_write(wr), .csr_writedata(wdata), .csr_readdata(rdata),
        .csr_waitrequest(wreq),
        .master_address(maddr), .master_writedata(mwdata), .master_byteenable(mbe),
        .master_write(mwrite), .master_read(mread), .master_waitrequest(mwait),
        .master_readdata(mrdata), .master_readdatavalid(mrvalid));

    // DDR model: random wait states, one-cycle read latency
    always @(posedge clk) begin
        mwait <= ($random & 3) == 0;
        mrvalid <= 0;
        if (mwrite && !mwait) mem[(maddr - 32'h1000) >> 2] <= mwdata;
        if (mread && !mwait) begin mrdata <= mem[(maddr - 32'h1000) >> 2]; mrvalid <= 1; end
    end

    task csr_w(input [3:0] ad, input [31:0] d);
        begin @(posedge clk) begin a <= ad; wdata <= d; wr <= 1; end @(posedge clk) wr <= 0; end
    endtask
    reg [31:0] r;
    task csr_r(input [3:0] ad);
        begin @(posedge clk) a <= ad; @(posedge clk) r = rdata; end
    endtask

    integer errors = 0;
    task capture_and_check(input en, input [10:0] x0, y0, w, h, input [1:0] step, input [127:0] name);
        integer i, j, n, cols, rows, sx, sy, sstep, guard;
        begin
            csr_w(10, x0); csr_w(11, y0); csr_w(12, w); csr_w(13, h); csr_w(15, step); csr_w(14, en);
            for (i = 0; i < 4096; i = i + 1) mem[i] = 32'hDEADBEEF;
            csr_w(1, 32'h1000); csr_w(0, 1);
            guard = 0;
            csr_r(0);
            while (!r[1] && guard < 200000) begin csr_r(0); guard = guard + 1; end
            sstep = en ? (1 << step) : 4;
            cols = en ? (w >> step) : SW / 4; rows = en ? (h >> step) : SH / 4;
            csr_r(6); n = r;
            csr_r(0);
            if (r[2] || n != cols * rows) begin
                $display("%0s: status error=%0d words=%0d expected=%0d", name, r[2], n, cols * rows);
                errors = errors + 1;
            end
            for (j = 0; j < rows; j = j + 1)
                for (i = 0; i < cols; i = i + 1) begin
                    sx = (en ? x0 : 0) + i * sstep; sy = (en ? y0 : 0) + j * sstep;
                    if (mem[j * cols + i] !== {8'd0, sx[7:0], sy[7:0], 8'hA5}) begin
                        if (errors < 10) $display("%0s: word (%0d,%0d) = %h, expected pixel (%0d,%0d)",
                                                  name, i, j, mem[j * cols + i], sx, sy);
                        errors = errors + 1;
                    end
                end
            if (mem[cols * rows] !== 32'hDEADBEEF) begin
                $display("%0s: wrote past the end", name); errors = errors + 1;
            end
            $display("%0s: %0d x %0d words checked", name, cols, rows);
        end
    endtask

    initial begin
        repeat (5) @(posedge clk); reset = 0; repeat (20) @(posedge clk);
        csr_r(9); $display("version %h", r);
        capture_and_check(0, 0, 0, 0, 0, 0, "normal 1/4");
        capture_and_check(1, 20, 10, 24, 16, 1, "window step 2");
        capture_and_check(1, 8, 4, 16, 12, 0, "window full res");
        capture_and_check(0, 0, 0, 0, 0, 0, "normal again");
        if (errors) $display("FAIL: %0d errors", errors); else $display("PASS: all words correct");
        $finish;
    end
endmodule

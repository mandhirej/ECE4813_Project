`timescale 1ns/1ps
// Independent 120-second timer. No automatic turn handoff:
// HPS software must accept a MANUAL ARM DS App Console Enter before it
// commands the next player to start.
module turn_timer_120s #(
    parameter integer CLOCKS_PER_SECOND = 50000000,
    parameter integer TURN_SECONDS = 120
)(
    input  wire clk,
    input  wire reset_n,
    input  wire command_valid,            // 1-clock pulse in clk domain
    input  wire [1:0] command,             // 0 ours, 1 opponent, 2 stop, 3 clear
    output reg  valid,
    output reg  running,
    output reg  expired,
    output reg  opponent,
    output reg  [6:0] seconds_left
);
    localparam integer COUNTER_WIDTH = $clog2(CLOCKS_PER_SECOND);
    reg [COUNTER_WIDTH-1:0] subsecond;
    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            valid <= 1'b0;
            running <= 1'b0;
            expired <= 1'b0;
            opponent <= 1'b0;
            seconds_left <= 7'd0;
            subsecond <= {COUNTER_WIDTH{1'b0}};
        end else if (command_valid) begin
            case (command)
                2'd0, 2'd1: begin
                    valid <= 1'b1;
                    running <= 1'b1;
                    expired <= 1'b0;
                    opponent <= command[0];
                    seconds_left <= TURN_SECONDS;
                    subsecond <= {COUNTER_WIDTH{1'b0}};
                end
                2'd2: running <= 1'b0;  // preserve deadline/result on STOP
                2'd3: begin
                    valid <= 1'b0;
                    running <= 1'b0;
                    expired <= 1'b0;
                    seconds_left <= 7'd0;
                    subsecond <= {COUNTER_WIDTH{1'b0}};
                end
            endcase
        end else if (running) begin
            if (subsecond == CLOCKS_PER_SECOND-1) begin
                subsecond <= {COUNTER_WIDTH{1'b0}};
                if (seconds_left == 7'd1) begin
                    seconds_left <= 7'd0;
                    running <= 1'b0;
                    expired <= 1'b1; // sticky until new START or CLEAR
                end else if (seconds_left != 7'd0) begin
                    seconds_left <= seconds_left - 7'd1;
                end
            end else begin
                subsecond <= subsecond + 1'b1;
            end
        end
    end
endmodule

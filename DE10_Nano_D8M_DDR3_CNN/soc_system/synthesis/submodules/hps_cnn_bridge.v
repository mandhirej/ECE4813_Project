`timescale 1ns/1ps
`default_nettype none
// One Avalon slave, same external 50-MHz clock as the CNN. No custom CDC.
module hps_cnn_bridge (
 input wire clk, reset,
 input wire [3:0] csr_address,
 input wire csr_read,csr_write,
 input wire [3:0] csr_byteenable,
 input wire [31:0] csr_writedata,
 output reg [31:0] csr_readdata,
 output wire csr_waitrequest,
 output wire pixel_clear,pixel_we,
 output wire [11:0] pixel_addr,
 output wire [7:0] pixel_data,
 output reg start,
 output wire core_reset_n,
 input wire core_busy,core_done,
 input wire [5:0] core_class,
 input wire [31:0] image_hash,
 input wire [12:0] image_count,
 output reg bbox_valid,
 output reg [10:0] bbox_min_x,bbox_max_x,bbox_min_y,bbox_max_y
);
 reg busy,done,loading,core_ready;
 reg [7:0] error_code;
 reg [12:0] count;
 reg [5:0] result_class;
 reg [31:0] cycles,ticks;
 reg [2:0] reset_left;
 wire write_full = csr_write && (csr_byteenable==4'hf);
 wire idle_ok = !busy && !core_busy && core_ready;
 assign csr_waitrequest=1'b0;
 assign core_reset_n=!reset && core_ready;
 assign pixel_clear=write_full && csr_address==0 && csr_writedata==1 && idle_ok;
 assign pixel_we=write_full && csr_address==1 && idle_ok && loading && count<4096;
 assign pixel_addr=count[11:0];
 assign pixel_data=csr_writedata[7:0];
 always @(*) begin
  csr_readdata=0;
  case(csr_address)
   0:csr_readdata={25'd0,(!core_ready),core_busy,(count==4096),loading,(error_code!=0),done,busy};
   2:csr_readdata={19'd0,count};
   3:csr_readdata=image_hash;
   4:csr_readdata={26'd0,result_class};
   5:csr_readdata=cycles;
   6:csr_readdata={24'd0,error_code};
   7:csr_readdata=32'h434e4e31;
   8:csr_readdata={21'd0,bbox_min_x};
   9:csr_readdata={21'd0,bbox_max_x};
   10:csr_readdata={21'd0,bbox_min_y};
   11:csr_readdata={21'd0,bbox_max_y};
   12:csr_readdata={31'd0,bbox_valid};
   13:csr_readdata=32'h00010000;
   14:csr_readdata={19'd0,image_count};
   15:csr_readdata=ticks;
   default:csr_readdata=0;
  endcase
 end
 always @(posedge clk or posedge reset) begin
  if(reset)begin
   busy<=0;done<=0;loading<=0;error_code<=0;count<=0;result_class<=0;
   cycles<=0;ticks<=0;reset_left<=4;core_ready<=0;start<=0;
   bbox_valid<=0;bbox_min_x<=0;bbox_max_x<=0;bbox_min_y<=0;bbox_max_y<=0;
  end else begin
   ticks<=ticks+1'b1;start<=0;
   if(reset_left!=0)begin reset_left<=reset_left-1'b1;core_ready<=0;end
   else core_ready<=1;
   if(busy)begin
    cycles<=cycles+1'b1;
    if(core_done)begin busy<=0;done<=1;result_class<=core_class;end
   end
   if(csr_write)begin
    if(csr_byteenable!=4'hf)error_code<=8'd5;
    else if(csr_address==0 && csr_writedata==4)begin
     busy<=0;done<=0;loading<=0;error_code<=0;count<=0;cycles<=0;
     result_class<=0;reset_left<=4;core_ready<=0;bbox_valid<=0;
    end else if(!idle_ok)error_code<=8'd1;
    else case(csr_address)
     0:case(csr_writedata)
      1:begin loading<=1;done<=0;error_code<=0;count<=0;cycles<=0;result_class<=0;bbox_valid<=0;end
      2:if(loading && count==4096 && image_count==4096 && error_code==0)begin
          start<=1;busy<=1;done<=0;loading<=0;cycles<=0;
         end else error_code<=8'd2;
      default:error_code<=8'd4;
     endcase
     1:if(pixel_we)count<=count+1'b1;else error_code<=8'd3;
     8:bbox_min_x<=csr_writedata[10:0];
     9:bbox_max_x<=csr_writedata[10:0];
     10:bbox_min_y<=csr_writedata[10:0];
     11:bbox_max_y<=csr_writedata[10:0];
     12:bbox_valid<=csr_writedata[0];
     default:error_code<=8'd4;
    endcase
   end
  end
 end
endmodule
`default_nettype wire

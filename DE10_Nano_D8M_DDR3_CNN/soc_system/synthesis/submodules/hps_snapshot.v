// One-shot D8M/CVO RGB capture to HPS DDR. Idle until START is written.
// Default: sample every 4th pixel/line, 480x270, one 0x00RRGGBB word/pixel.
// V2 window mode (CSR 10..15, set before START): capture only the rectangle
// x0..x0+w-1, y0..y0+h-1 of the 1920x1080 source, every 2^step-th pixel/line,
// e.g. the DRAW card slot at 2x the normal resolution. w and h must be multiples
// of 2^step; the HPS then expects (w>>step)*(h>>step) words. Window mode stays
// selected until CSR 14 is written 0. Ports unchanged (no Platform Designer change).
// Two clocks; no backpressure is ever applied to HDMI or CNN.
module hps_snapshot #(
 parameter integer SAMPLE_LOG2=2,
 parameter integer SOURCE_WIDTH=1920,
 parameter integer SOURCE_HEIGHT=1080,
 parameter integer FIFO_BITS=10
)(
 input wire clk,reset,
 input wire video_clk,
 input wire [7:0] video_r,video_g,video_b,
 input wire video_vs,video_de,
 input wire [3:0] csr_address,
 input wire csr_read,csr_write,
 input wire [31:0] csr_writedata,
 output reg [31:0] csr_readdata,
 output wire csr_waitrequest,
 output wire [31:0] master_address,
 output wire [31:0] master_writedata,
 output wire [3:0] master_byteenable,
 output wire master_write,master_read,
 input wire master_waitrequest,
 input wire [31:0] master_readdata,
 input wire master_readdatavalid
);
 localparam integer WIDTH=SOURCE_WIDTH>>SAMPLE_LOG2;
 localparam integer HEIGHT=SOURCE_HEIGHT>>SAMPLE_LOG2;
 localparam integer PIXELS=WIDTH*HEIGHT;
 localparam integer MASK=(1<<SAMPLE_LOG2)-1;
 localparam integer DEPTH=1<<FIFO_BITS;
 assign csr_waitrequest=1'b0;
 assign master_byteenable=4'hF;
 reg [31:0] base_address,write_address,words_written,last_word;
 reg busy,done,error;
 reg request_toggle;
 (* altera_attribute="-name SYNCHRONIZER_IDENTIFICATION FORCED_IF_ASYNCHRONOUS" *) reg req_meta,req_sync;
 reg req_seen,video_done_toggle;
 (* altera_attribute="-name SYNCHRONIZER_IDENTIFICATION FORCED_IF_ASYNCHRONOUS" *) reg done_meta,done_sync;
 reg [1:0] video_error;
 reg [1:0] error_code;
 reg [31:0] video_count;
 reg armed,capturing;
 reg [10:0] vx,vy;
 reg vde_d,vvs_d,line_seen;
 // window registers (Avalon clock domain, quasi-static: written before START)
 reg [10:0] win_x0,win_y0,win_w,win_h;
 reg [1:0] win_step;
 reg win_en;
 wire [31:0] win_words=({21'd0,win_w}>>win_step)*({21'd0,win_h}>>win_step);
 wire [31:0] target_words=win_en?win_words:PIXELS;
 // copies used by the video domain, taken when a capture is armed: the request
 // toggle crosses two synchronizer flops after the registers were last written
 reg [10:0] v_x0,v_y0,v_x1,v_y1;
 reg [1:0] v_step;
 reg v_en;
 reg [31:0] v_target;
 wire fs=video_vs && !vvs_d;
 wire ls=video_de && !vde_d;
 wire [10:0] x=ls?11'd0:vx+11'd1;
 wire [10:0] y=ls?(line_seen?vy+11'd1:11'd0):vy;
 wire [10:0] dx=x-v_x0,dy=y-v_y0;
 wire [10:0] step_mask=(11'd1<<v_step)-11'd1;
 wire sample=capturing && video_de && x<SOURCE_WIDTH && y<SOURCE_HEIGHT &&
             (v_en ? (x>=v_x0 && x<v_x1 && y>=v_y0 && y<v_y1 &&
                      (dx&step_mask)==0 && (dy&step_mask)==0)
                   : (((x&MASK)==0) && ((y&MASK)==0)));

 // Reset is asserted to both domains. Deassertion is synchronized locally.
 (* altera_attribute="-name SYNCHRONIZER_IDENTIFICATION FORCED_IF_ASYNCHRONOUS" *) reg [1:0] video_reset_pipe;
 always @(posedge video_clk or posedge reset)
  if(reset)video_reset_pipe<=2'b11;else video_reset_pipe<={video_reset_pipe[0],1'b0};
 wire video_reset=video_reset_pipe[1];

 // Dual-clock FIFO with Gray pointers. Data is read synchronously into an
 // output holding register, which remains stable for all Avalon wait cycles.
 (* ramstyle="M10K, no_rw_check" *) reg [31:0] fifo_mem[0:DEPTH-1];
 reg [FIFO_BITS:0] wb,wg,rb,rg;
 (* altera_attribute="-name SYNCHRONIZER_IDENTIFICATION FORCED_IF_ASYNCHRONOUS" *) reg [FIFO_BITS:0] rg_meta,rg_sync,wg_meta,wg_sync;
 wire [FIFO_BITS:0] wb_next=wb+1'b1;
 wire [FIFO_BITS:0] wg_next=(wb_next>>1)^wb_next;
 wire full=(wg_next=={~rg_sync[FIFO_BITS:FIFO_BITS-1],rg_sync[FIFO_BITS-2:0]});
 wire empty=(rg==wg_sync);
 wire push=sample && !full;
 reg data_valid;
 reg [31:0] data_q;
 reg [2:0] quiet;
 reg fence_request,fence_wait;
 wire pop=busy && !data_valid && !empty && !fence_request && !fence_wait;
 assign master_address=(fence_request || fence_wait)?write_address-32'd4:write_address;
 assign master_writedata=data_q;
 assign master_write=busy && data_valid && !fence_request && !fence_wait;
 assign master_read=fence_request;
 always @(posedge video_clk)if(push)fifo_mem[wb[FIFO_BITS-1:0]]<={8'd0,video_r,video_g,video_b};
 always @(posedge clk)if(pop)data_q<=fifo_mem[rb[FIFO_BITS-1:0]];

 always @(posedge video_clk or posedge video_reset)begin
  if(video_reset)begin
   wb<=0;wg<=0;rg_meta<=0;rg_sync<=0;req_meta<=0;req_sync<=0;req_seen<=0;
   video_done_toggle<=0;video_error<=0;video_count<=0;armed<=0;capturing<=0;
   vx<=0;vy<=0;vde_d<=0;vvs_d<=0;line_seen<=0;
   v_x0<=0;v_y0<=0;v_x1<=0;v_y1<=0;v_step<=0;v_en<=0;v_target<=PIXELS;
  end else begin
   req_meta<=request_toggle;req_sync<=req_meta;
   rg_meta<=rg;rg_sync<=rg_meta;vde_d<=video_de;vvs_d<=video_vs;
   if(fs)begin vx<=0;vy<=0;line_seen<=0;end
   else if(video_de)begin vx<=x;vy<=y;line_seen<=1;end
   if(push)begin wb<=wb_next;wg<=wg_next;end
   if(req_sync!=req_seen)begin
    req_seen<=req_sync;armed<=1;video_error<=0;video_count<=0;
    v_x0<=win_x0;v_y0<=win_y0;v_x1<=win_x0+win_w;v_y1<=win_y0+win_h;
    v_step<=win_step;v_en<=win_en;v_target<=target_words;
   end
   if(armed && fs)begin armed<=0;capturing<=1;end
   else if(capturing && fs)begin
    capturing<=0;video_error<=2;video_done_toggle<=req_seen;
   end
   if(sample)begin
    if(full)begin capturing<=0;video_error<=1;video_done_toggle<=req_seen;end
    else begin
     video_count<=video_count+1;
     if(video_count==v_target-1)begin capturing<=0;video_done_toggle<=req_seen;end
    end
   end
  end
 end
 always @* begin
  case(csr_address)
   0:csr_readdata={29'd0,error,done,busy};
   1:csr_readdata=base_address;
   2:csr_readdata=WIDTH;            // normal-mode size (window mode: see 12..15)
   3:csr_readdata=HEIGHT;
   4:csr_readdata=WIDTH*4;
   5:csr_readdata=32'h58524742; // 'XRGB': 0x00RRGGBB words, B,G,R,0 in ARM memory
   6:csr_readdata=words_written;
   7:csr_readdata={30'd0,error_code};
   8:csr_readdata=32'h534E4150; // 'SNAP'
   9:csr_readdata=32'h00020000;     // V2: window mode
   10:csr_readdata={21'd0,win_x0};
   11:csr_readdata={21'd0,win_y0};
   12:csr_readdata={21'd0,win_w};
   13:csr_readdata={21'd0,win_h};
   14:csr_readdata={31'd0,win_en};
   15:csr_readdata={30'd0,win_step};
   default:csr_readdata=0;
  endcase
 end
 always @(posedge clk or posedge reset)begin
  if(reset)begin
   base_address<=0;write_address<=0;words_written<=0;last_word<=0;
   busy<=0;done<=0;error<=0;error_code<=0;request_toggle<=0;done_meta<=0;done_sync<=0;
   rb<=0;rg<=0;wg_meta<=0;wg_sync<=0;data_valid<=0;quiet<=0;
   fence_request<=0;fence_wait<=0;
   win_x0<=0;win_y0<=0;win_w<=0;win_h<=0;win_en<=0;win_step<=0;
  end else begin
   done_meta<=video_done_toggle;done_sync<=done_meta;wg_meta<=wg;wg_sync<=wg_meta;
   if(csr_write && csr_address==1 && !busy)base_address<=csr_writedata;
   if(csr_write && !busy)case(csr_address)
    10:win_x0<=csr_writedata[10:0];
    11:win_y0<=csr_writedata[10:0];
    12:win_w<=csr_writedata[10:0];
    13:win_h<=csr_writedata[10:0];
    14:win_en<=csr_writedata[0];
    15:win_step<=csr_writedata[1:0];
    default:;
   endcase
   if(csr_write && csr_address==0 && csr_writedata[0] && !busy)begin
    done<=0;error<=0;error_code<=0;quiet<=0;words_written<=0;write_address<=base_address;
    if(base_address[1:0]!=0 || base_address==0)begin done<=1;error<=1;end
    else begin busy<=1;request_toggle<=~request_toggle;end
   end
   if(pop)begin
    rb<=rb+1'b1;rg<=((rb+1'b1)>>1)^(rb+1'b1);data_valid<=1;
   end
   if(master_write && !master_waitrequest)begin
    data_valid<=0;write_address<=write_address+4;words_written<=words_written+1;
    last_word<=data_q;
   end
   if(busy && done_sync==request_toggle && empty && !data_valid && !fence_request && !fence_wait)begin
    if(quiet<4)quiet<=quiet+1'b1;
    else if(words_written!=0)fence_request<=1;
    else begin busy<=0;done<=1;error<=1;error_code<=video_error;end
   end else quiet<=0;
   if(fence_request && !master_waitrequest)begin fence_request<=0;fence_wait<=1;end
   if((fence_wait || (fence_request && !master_waitrequest)) && master_readdatavalid)begin
    fence_wait<=0;busy<=0;done<=1;
    error<=video_error!=0 || words_written!=target_words || master_readdata!=last_word;
    error_code<=video_error;
   end
  end
 end
endmodule

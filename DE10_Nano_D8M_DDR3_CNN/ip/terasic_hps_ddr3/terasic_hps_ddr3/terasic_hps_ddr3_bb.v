
module terasic_hps_ddr3 (
	clk_clk,
	h2f_reset_reset_n,
	hps_0_hps_io_hps_io_sdio_inst_CMD,
	hps_0_hps_io_hps_io_sdio_inst_D0,
	hps_0_hps_io_hps_io_sdio_inst_D1,
	hps_0_hps_io_hps_io_sdio_inst_CLK,
	hps_0_hps_io_hps_io_sdio_inst_D2,
	hps_0_hps_io_hps_io_sdio_inst_D3,
	hps_0_hps_io_hps_io_uart0_inst_RX,
	hps_0_hps_io_hps_io_uart0_inst_TX,
	hps_0_hps_io_hps_io_gpio_inst_GPIO53,
	hps_f2h_sdram0_clock_clk,
	hps_f2h_sdram0_data_address,
	hps_f2h_sdram0_data_read,
	hps_f2h_sdram0_data_readdata,
	hps_f2h_sdram0_data_write,
	hps_f2h_sdram0_data_writedata,
	hps_f2h_sdram0_data_readdatavalid,
	hps_f2h_sdram0_data_waitrequest,
	hps_f2h_sdram0_data_byteenable,
	hps_f2h_sdram0_data_burstcount,
	memory_mem_a,
	memory_mem_ba,
	memory_mem_ck,
	memory_mem_ck_n,
	memory_mem_cke,
	memory_mem_cs_n,
	memory_mem_ras_n,
	memory_mem_cas_n,
	memory_mem_we_n,
	memory_mem_reset_n,
	memory_mem_dq,
	memory_mem_dqs,
	memory_mem_dqs_n,
	memory_mem_odt,
	memory_mem_dm,
	memory_oct_rzqin,
	hps_h2f_axi_master_awid,
	hps_h2f_axi_master_awaddr,
	hps_h2f_axi_master_awlen,
	hps_h2f_axi_master_awsize,
	hps_h2f_axi_master_awburst,
	hps_h2f_axi_master_awlock,
	hps_h2f_axi_master_awcache,
	hps_h2f_axi_master_awprot,
	hps_h2f_axi_master_awvalid,
	hps_h2f_axi_master_awready,
	hps_h2f_axi_master_wid,
	hps_h2f_axi_master_wdata,
	hps_h2f_axi_master_wstrb,
	hps_h2f_axi_master_wlast,
	hps_h2f_axi_master_wvalid,
	hps_h2f_axi_master_wready,
	hps_h2f_axi_master_bid,
	hps_h2f_axi_master_bresp,
	hps_h2f_axi_master_bvalid,
	hps_h2f_axi_master_bready,
	hps_h2f_axi_master_arid,
	hps_h2f_axi_master_araddr,
	hps_h2f_axi_master_arlen,
	hps_h2f_axi_master_arsize,
	hps_h2f_axi_master_arburst,
	hps_h2f_axi_master_arlock,
	hps_h2f_axi_master_arcache,
	hps_h2f_axi_master_arprot,
	hps_h2f_axi_master_arvalid,
	hps_h2f_axi_master_arready,
	hps_h2f_axi_master_rid,
	hps_h2f_axi_master_rdata,
	hps_h2f_axi_master_rresp,
	hps_h2f_axi_master_rlast,
	hps_h2f_axi_master_rvalid,
	hps_h2f_axi_master_rready,
	hps_h2f_lw_axi_master_awid,
	hps_h2f_lw_axi_master_awaddr,
	hps_h2f_lw_axi_master_awlen,
	hps_h2f_lw_axi_master_awsize,
	hps_h2f_lw_axi_master_awburst,
	hps_h2f_lw_axi_master_awlock,
	hps_h2f_lw_axi_master_awcache,
	hps_h2f_lw_axi_master_awprot,
	hps_h2f_lw_axi_master_awvalid,
	hps_h2f_lw_axi_master_awready,
	hps_h2f_lw_axi_master_wid,
	hps_h2f_lw_axi_master_wdata,
	hps_h2f_lw_axi_master_wstrb,
	hps_h2f_lw_axi_master_wlast,
	hps_h2f_lw_axi_master_wvalid,
	hps_h2f_lw_axi_master_wready,
	hps_h2f_lw_axi_master_bid,
	hps_h2f_lw_axi_master_bresp,
	hps_h2f_lw_axi_master_bvalid,
	hps_h2f_lw_axi_master_bready,
	hps_h2f_lw_axi_master_arid,
	hps_h2f_lw_axi_master_araddr,
	hps_h2f_lw_axi_master_arlen,
	hps_h2f_lw_axi_master_arsize,
	hps_h2f_lw_axi_master_arburst,
	hps_h2f_lw_axi_master_arlock,
	hps_h2f_lw_axi_master_arcache,
	hps_h2f_lw_axi_master_arprot,
	hps_h2f_lw_axi_master_arvalid,
	hps_h2f_lw_axi_master_arready,
	hps_h2f_lw_axi_master_rid,
	hps_h2f_lw_axi_master_rdata,
	hps_h2f_lw_axi_master_rresp,
	hps_h2f_lw_axi_master_rlast,
	hps_h2f_lw_axi_master_rvalid,
	hps_h2f_lw_axi_master_rready);	

	input		clk_clk;
	output		h2f_reset_reset_n;
	inout		hps_0_hps_io_hps_io_sdio_inst_CMD;
	inout		hps_0_hps_io_hps_io_sdio_inst_D0;
	inout		hps_0_hps_io_hps_io_sdio_inst_D1;
	output		hps_0_hps_io_hps_io_sdio_inst_CLK;
	inout		hps_0_hps_io_hps_io_sdio_inst_D2;
	inout		hps_0_hps_io_hps_io_sdio_inst_D3;
	input		hps_0_hps_io_hps_io_uart0_inst_RX;
	output		hps_0_hps_io_hps_io_uart0_inst_TX;
	inout		hps_0_hps_io_hps_io_gpio_inst_GPIO53;
	input		hps_f2h_sdram0_clock_clk;
	input	[25:0]	hps_f2h_sdram0_data_address;
	input		hps_f2h_sdram0_data_read;
	output	[127:0]	hps_f2h_sdram0_data_readdata;
	input		hps_f2h_sdram0_data_write;
	input	[127:0]	hps_f2h_sdram0_data_writedata;
	output		hps_f2h_sdram0_data_readdatavalid;
	output		hps_f2h_sdram0_data_waitrequest;
	input	[15:0]	hps_f2h_sdram0_data_byteenable;
	input	[8:0]	hps_f2h_sdram0_data_burstcount;
	output	[14:0]	memory_mem_a;
	output	[2:0]	memory_mem_ba;
	output		memory_mem_ck;
	output		memory_mem_ck_n;
	output		memory_mem_cke;
	output		memory_mem_cs_n;
	output		memory_mem_ras_n;
	output		memory_mem_cas_n;
	output		memory_mem_we_n;
	output		memory_mem_reset_n;
	inout	[31:0]	memory_mem_dq;
	inout	[3:0]	memory_mem_dqs;
	inout	[3:0]	memory_mem_dqs_n;
	output		memory_mem_odt;
	output	[3:0]	memory_mem_dm;
	input		memory_oct_rzqin;
	output	[11:0]	hps_h2f_axi_master_awid;
	output	[29:0]	hps_h2f_axi_master_awaddr;
	output	[3:0]	hps_h2f_axi_master_awlen;
	output	[2:0]	hps_h2f_axi_master_awsize;
	output	[1:0]	hps_h2f_axi_master_awburst;
	output	[1:0]	hps_h2f_axi_master_awlock;
	output	[3:0]	hps_h2f_axi_master_awcache;
	output	[2:0]	hps_h2f_axi_master_awprot;
	output		hps_h2f_axi_master_awvalid;
	input		hps_h2f_axi_master_awready;
	output	[11:0]	hps_h2f_axi_master_wid;
	output	[63:0]	hps_h2f_axi_master_wdata;
	output	[7:0]	hps_h2f_axi_master_wstrb;
	output		hps_h2f_axi_master_wlast;
	output		hps_h2f_axi_master_wvalid;
	input		hps_h2f_axi_master_wready;
	input	[11:0]	hps_h2f_axi_master_bid;
	input	[1:0]	hps_h2f_axi_master_bresp;
	input		hps_h2f_axi_master_bvalid;
	output		hps_h2f_axi_master_bready;
	output	[11:0]	hps_h2f_axi_master_arid;
	output	[29:0]	hps_h2f_axi_master_araddr;
	output	[3:0]	hps_h2f_axi_master_arlen;
	output	[2:0]	hps_h2f_axi_master_arsize;
	output	[1:0]	hps_h2f_axi_master_arburst;
	output	[1:0]	hps_h2f_axi_master_arlock;
	output	[3:0]	hps_h2f_axi_master_arcache;
	output	[2:0]	hps_h2f_axi_master_arprot;
	output		hps_h2f_axi_master_arvalid;
	input		hps_h2f_axi_master_arready;
	input	[11:0]	hps_h2f_axi_master_rid;
	input	[63:0]	hps_h2f_axi_master_rdata;
	input	[1:0]	hps_h2f_axi_master_rresp;
	input		hps_h2f_axi_master_rlast;
	input		hps_h2f_axi_master_rvalid;
	output		hps_h2f_axi_master_rready;
	output	[11:0]	hps_h2f_lw_axi_master_awid;
	output	[20:0]	hps_h2f_lw_axi_master_awaddr;
	output	[3:0]	hps_h2f_lw_axi_master_awlen;
	output	[2:0]	hps_h2f_lw_axi_master_awsize;
	output	[1:0]	hps_h2f_lw_axi_master_awburst;
	output	[1:0]	hps_h2f_lw_axi_master_awlock;
	output	[3:0]	hps_h2f_lw_axi_master_awcache;
	output	[2:0]	hps_h2f_lw_axi_master_awprot;
	output		hps_h2f_lw_axi_master_awvalid;
	input		hps_h2f_lw_axi_master_awready;
	output	[11:0]	hps_h2f_lw_axi_master_wid;
	output	[31:0]	hps_h2f_lw_axi_master_wdata;
	output	[3:0]	hps_h2f_lw_axi_master_wstrb;
	output		hps_h2f_lw_axi_master_wlast;
	output		hps_h2f_lw_axi_master_wvalid;
	input		hps_h2f_lw_axi_master_wready;
	input	[11:0]	hps_h2f_lw_axi_master_bid;
	input	[1:0]	hps_h2f_lw_axi_master_bresp;
	input		hps_h2f_lw_axi_master_bvalid;
	output		hps_h2f_lw_axi_master_bready;
	output	[11:0]	hps_h2f_lw_axi_master_arid;
	output	[20:0]	hps_h2f_lw_axi_master_araddr;
	output	[3:0]	hps_h2f_lw_axi_master_arlen;
	output	[2:0]	hps_h2f_lw_axi_master_arsize;
	output	[1:0]	hps_h2f_lw_axi_master_arburst;
	output	[1:0]	hps_h2f_lw_axi_master_arlock;
	output	[3:0]	hps_h2f_lw_axi_master_arcache;
	output	[2:0]	hps_h2f_lw_axi_master_arprot;
	output		hps_h2f_lw_axi_master_arvalid;
	input		hps_h2f_lw_axi_master_arready;
	input	[11:0]	hps_h2f_lw_axi_master_rid;
	input	[31:0]	hps_h2f_lw_axi_master_rdata;
	input	[1:0]	hps_h2f_lw_axi_master_rresp;
	input		hps_h2f_lw_axi_master_rlast;
	input		hps_h2f_lw_axi_master_rvalid;
	output		hps_h2f_lw_axi_master_rready;
endmodule

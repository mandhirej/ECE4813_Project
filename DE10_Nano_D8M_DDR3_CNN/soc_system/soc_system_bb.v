
module soc_system (
	alt_vip_cl_cvo_0_clocked_video_vid_clk,
	alt_vip_cl_cvo_0_clocked_video_vid_data,
	alt_vip_cl_cvo_0_clocked_video_underflow,
	alt_vip_cl_cvo_0_clocked_video_vid_datavalid,
	alt_vip_cl_cvo_0_clocked_video_vid_v_sync,
	alt_vip_cl_cvo_0_clocked_video_vid_h_sync,
	alt_vip_cl_cvo_0_clocked_video_vid_f,
	alt_vip_cl_cvo_0_clocked_video_vid_h,
	alt_vip_cl_cvo_0_clocked_video_vid_v,
	bridge_test_in_export,
	bridge_test_out_export,
	clk_clk,
	clk_d8m_clk,
	clk_hdmi_clk,
	clk_hdmi_ref_clk,
	clk_hps_ref_clk,
	cnn_status_export,
	hps_0_hps_io_hps_io_sdio_inst_CMD,
	hps_0_hps_io_hps_io_sdio_inst_D0,
	hps_0_hps_io_hps_io_sdio_inst_D1,
	hps_0_hps_io_hps_io_sdio_inst_CLK,
	hps_0_hps_io_hps_io_sdio_inst_D2,
	hps_0_hps_io_hps_io_sdio_inst_D3,
	hps_0_hps_io_hps_io_uart0_inst_RX,
	hps_0_hps_io_hps_io_uart0_inst_TX,
	hps_0_hps_io_hps_io_gpio_inst_GPIO53,
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
	reset_reset_n,
	snapshot_video_r,
	snapshot_video_g,
	snapshot_video_b,
	snapshot_video_vs,
	snapshot_video_de,
	snapshot_video_clk_clk,
	terasic_camera_0_conduit_end_camera_d,
	terasic_camera_0_conduit_end_camera_fval,
	terasic_camera_0_conduit_end_camera_lval,
	terasic_camera_0_conduit_end_camera_pixclk,
	cnn_upload_pixel_clear,
	cnn_upload_pixel_we,
	cnn_upload_pixel_addr,
	cnn_upload_pixel_data,
	cnn_upload_start,
	cnn_upload_core_reset_n,
	cnn_upload_core_busy,
	cnn_upload_core_done,
	cnn_upload_core_class,
	cnn_upload_image_hash,
	cnn_upload_image_count,
	cnn_upload_bbox_valid,
	cnn_upload_bbox_min_x,
	cnn_upload_bbox_max_x,
	cnn_upload_bbox_min_y,
	cnn_upload_bbox_max_y,
	cnn_upload_clk_clk,
	cnn_upload_reset_reset);	

	input		alt_vip_cl_cvo_0_clocked_video_vid_clk;
	output	[23:0]	alt_vip_cl_cvo_0_clocked_video_vid_data;
	output		alt_vip_cl_cvo_0_clocked_video_underflow;
	output		alt_vip_cl_cvo_0_clocked_video_vid_datavalid;
	output		alt_vip_cl_cvo_0_clocked_video_vid_v_sync;
	output		alt_vip_cl_cvo_0_clocked_video_vid_h_sync;
	output		alt_vip_cl_cvo_0_clocked_video_vid_f;
	output		alt_vip_cl_cvo_0_clocked_video_vid_h;
	output		alt_vip_cl_cvo_0_clocked_video_vid_v;
	input	[7:0]	bridge_test_in_export;
	output	[7:0]	bridge_test_out_export;
	input		clk_clk;
	output		clk_d8m_clk;
	output		clk_hdmi_clk;
	input		clk_hdmi_ref_clk;
	input		clk_hps_ref_clk;
	input	[7:0]	cnn_status_export;
	inout		hps_0_hps_io_hps_io_sdio_inst_CMD;
	inout		hps_0_hps_io_hps_io_sdio_inst_D0;
	inout		hps_0_hps_io_hps_io_sdio_inst_D1;
	output		hps_0_hps_io_hps_io_sdio_inst_CLK;
	inout		hps_0_hps_io_hps_io_sdio_inst_D2;
	inout		hps_0_hps_io_hps_io_sdio_inst_D3;
	input		hps_0_hps_io_hps_io_uart0_inst_RX;
	output		hps_0_hps_io_hps_io_uart0_inst_TX;
	inout		hps_0_hps_io_hps_io_gpio_inst_GPIO53;
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
	input		reset_reset_n;
	input	[7:0]	snapshot_video_r;
	input	[7:0]	snapshot_video_g;
	input	[7:0]	snapshot_video_b;
	input		snapshot_video_vs;
	input		snapshot_video_de;
	input		snapshot_video_clk_clk;
	input	[11:0]	terasic_camera_0_conduit_end_camera_d;
	input		terasic_camera_0_conduit_end_camera_fval;
	input		terasic_camera_0_conduit_end_camera_lval;
	input		terasic_camera_0_conduit_end_camera_pixclk;
	output		cnn_upload_pixel_clear;
	output		cnn_upload_pixel_we;
	output	[11:0]	cnn_upload_pixel_addr;
	output	[7:0]	cnn_upload_pixel_data;
	output		cnn_upload_start;
	output		cnn_upload_core_reset_n;
	input		cnn_upload_core_busy;
	input		cnn_upload_core_done;
	input	[5:0]	cnn_upload_core_class;
	input	[31:0]	cnn_upload_image_hash;
	input	[12:0]	cnn_upload_image_count;
	output		cnn_upload_bbox_valid;
	output	[10:0]	cnn_upload_bbox_min_x;
	output	[10:0]	cnn_upload_bbox_max_x;
	output	[10:0]	cnn_upload_bbox_min_y;
	output	[10:0]	cnn_upload_bbox_max_y;
	input		cnn_upload_clk_clk;
	input		cnn_upload_reset_reset;
endmodule

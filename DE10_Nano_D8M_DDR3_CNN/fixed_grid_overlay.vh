`ifndef FIXED_GRID_OVERLAY_V16
`define FIXED_GRID_OVERLAY_V16
// V16 display-only guide: 4x4 D/C/H/S board + DRAW at the top of column 5,
// HG hand-gesture zone below DRAW, status legend under the thumbnail.
// No dividers are used in the HDMI path.
// state: 0 cyan/expected empty, 1 green/good, 2 red/bad, 3 amber alert.
// ink: 0 transparent, 1 cyan, 2 dark label backing, 3 white glyph,
//      4 green, 5 red, 6 amber, 7 blue DRAW edge.
module fixed_grid_overlay_v16(
 input wire [7:0] sx,sy,
 input wire [31:0] cell_states,
 input wire [1:0] draw_state,
 input wire guide_mode,
 input wire [15:0] move_mask,
 input wire [3:0] target_cell,
 input wire target_active,
 input wire blink,
 output reg [3:0] ink
);
 `include "fixed_board_grid.vh"
 reg board_hit,draw_hit,xhit,yhit;
 reg [1:0] col,row,state;
 reg [7:0] lx,ly,font_x,font_y;
 reg [3:0] font_code;
 reg glyph;
 reg [2:0] legend_idx;
 reg [47:0] legend_label;
 reg [7:0] legend_lx,legend_ly;
 reg [3:0] legend_colour;
 reg [4:0] legend_bits;
 reg [7:0] legend_char;
 reg show_arrow;
    function [4:0] font5x7_row;
        input [7:0] ch;
        input [2:0] row;
        begin
            case (ch)
                "0": case(row) 0:font5x7_row=5'b01110;1:font5x7_row=5'b10001;2:font5x7_row=5'b10011;3:font5x7_row=5'b10101;4:font5x7_row=5'b11001;5:font5x7_row=5'b10001;default:font5x7_row=5'b01110;endcase
                "1": case(row) 0:font5x7_row=5'b00100;1:font5x7_row=5'b01100;2:font5x7_row=5'b00100;3:font5x7_row=5'b00100;4:font5x7_row=5'b00100;5:font5x7_row=5'b00100;default:font5x7_row=5'b01110;endcase
                "2": case(row) 0:font5x7_row=5'b01110;1:font5x7_row=5'b10001;2:font5x7_row=5'b00001;3:font5x7_row=5'b00010;4:font5x7_row=5'b00100;5:font5x7_row=5'b01000;default:font5x7_row=5'b11111;endcase
                "3": case(row) 0:font5x7_row=5'b11110;1:font5x7_row=5'b00001;2:font5x7_row=5'b00001;3:font5x7_row=5'b01110;4:font5x7_row=5'b00001;5:font5x7_row=5'b00001;default:font5x7_row=5'b11110;endcase
                "4": case(row) 0:font5x7_row=5'b00010;1:font5x7_row=5'b00110;2:font5x7_row=5'b01010;3:font5x7_row=5'b10010;4:font5x7_row=5'b11111;5:font5x7_row=5'b00010;default:font5x7_row=5'b00010;endcase
                "5": case(row) 0:font5x7_row=5'b11111;1:font5x7_row=5'b10000;2:font5x7_row=5'b10000;3:font5x7_row=5'b11110;4:font5x7_row=5'b00001;5:font5x7_row=5'b00001;default:font5x7_row=5'b11110;endcase
                "6": case(row) 0:font5x7_row=5'b01110;1:font5x7_row=5'b10000;2:font5x7_row=5'b10000;3:font5x7_row=5'b11110;4:font5x7_row=5'b10001;5:font5x7_row=5'b10001;default:font5x7_row=5'b01110;endcase
                "7": case(row) 0:font5x7_row=5'b11111;1:font5x7_row=5'b00001;2:font5x7_row=5'b00010;3:font5x7_row=5'b00100;4:font5x7_row=5'b01000;5:font5x7_row=5'b01000;default:font5x7_row=5'b01000;endcase
                "8": case(row) 0:font5x7_row=5'b01110;1:font5x7_row=5'b10001;2:font5x7_row=5'b10001;3:font5x7_row=5'b01110;4:font5x7_row=5'b10001;5:font5x7_row=5'b10001;default:font5x7_row=5'b01110;endcase
                "9": case(row) 0:font5x7_row=5'b01110;1:font5x7_row=5'b10001;2:font5x7_row=5'b10001;3:font5x7_row=5'b01111;4:font5x7_row=5'b00001;5:font5x7_row=5'b00001;default:font5x7_row=5'b01110;endcase
                "A": case(row) 0:font5x7_row=5'b01110;1:font5x7_row=5'b10001;2:font5x7_row=5'b10001;3:font5x7_row=5'b11111;4:font5x7_row=5'b10001;5:font5x7_row=5'b10001;default:font5x7_row=5'b10001;endcase
                "B": case(row) 0:font5x7_row=5'b11110;1:font5x7_row=5'b10001;2:font5x7_row=5'b10001;3:font5x7_row=5'b11110;4:font5x7_row=5'b10001;5:font5x7_row=5'b10001;default:font5x7_row=5'b11110;endcase
                "C": case(row) 0:font5x7_row=5'b01111;1:font5x7_row=5'b10000;2:font5x7_row=5'b10000;3:font5x7_row=5'b10000;4:font5x7_row=5'b10000;5:font5x7_row=5'b10000;default:font5x7_row=5'b01111;endcase
                "D": case(row) 0:font5x7_row=5'b11110;1:font5x7_row=5'b10001;2:font5x7_row=5'b10001;3:font5x7_row=5'b10001;4:font5x7_row=5'b10001;5:font5x7_row=5'b10001;default:font5x7_row=5'b11110;endcase
                "E": case(row) 0:font5x7_row=5'b11111;1:font5x7_row=5'b10000;2:font5x7_row=5'b10000;3:font5x7_row=5'b11110;4:font5x7_row=5'b10000;5:font5x7_row=5'b10000;default:font5x7_row=5'b11111;endcase
                "F": case(row) 0:font5x7_row=5'b11111;1:font5x7_row=5'b10000;2:font5x7_row=5'b10000;3:font5x7_row=5'b11110;4:font5x7_row=5'b10000;5:font5x7_row=5'b10000;default:font5x7_row=5'b10000;endcase
                "G": case(row) 0:font5x7_row=5'b01110;1:font5x7_row=5'b10001;2:font5x7_row=5'b10000;3:font5x7_row=5'b10111;4:font5x7_row=5'b10001;5:font5x7_row=5'b10001;default:font5x7_row=5'b01110;endcase
                "H": case(row) 0:font5x7_row=5'b10001;1:font5x7_row=5'b10001;2:font5x7_row=5'b10001;3:font5x7_row=5'b11111;4:font5x7_row=5'b10001;5:font5x7_row=5'b10001;default:font5x7_row=5'b10001;endcase
                "I": case(row) 0:font5x7_row=5'b01110;1:font5x7_row=5'b00100;2:font5x7_row=5'b00100;3:font5x7_row=5'b00100;4:font5x7_row=5'b00100;5:font5x7_row=5'b00100;default:font5x7_row=5'b01110;endcase
                "J": case(row) 0:font5x7_row=5'b00111;1:font5x7_row=5'b00010;2:font5x7_row=5'b00010;3:font5x7_row=5'b00010;4:font5x7_row=5'b10010;5:font5x7_row=5'b10010;default:font5x7_row=5'b01100;endcase
                "K": case(row) 0:font5x7_row=5'b10001;1:font5x7_row=5'b10010;2:font5x7_row=5'b10100;3:font5x7_row=5'b11000;4:font5x7_row=5'b10100;5:font5x7_row=5'b10010;default:font5x7_row=5'b10001;endcase
                "L": case(row) 0:font5x7_row=5'b10000;1:font5x7_row=5'b10000;2:font5x7_row=5'b10000;3:font5x7_row=5'b10000;4:font5x7_row=5'b10000;5:font5x7_row=5'b10000;default:font5x7_row=5'b11111;endcase
                "M": case(row) 0:font5x7_row=5'b10001;1:font5x7_row=5'b11011;2:font5x7_row=5'b10101;3:font5x7_row=5'b10101;4:font5x7_row=5'b10001;5:font5x7_row=5'b10001;default:font5x7_row=5'b10001;endcase
                "N": case(row) 0:font5x7_row=5'b10001;1:font5x7_row=5'b11001;2:font5x7_row=5'b10101;3:font5x7_row=5'b10011;4:font5x7_row=5'b10001;5:font5x7_row=5'b10001;default:font5x7_row=5'b10001;endcase
                "O": case(row) 0:font5x7_row=5'b01110;1:font5x7_row=5'b10001;2:font5x7_row=5'b10001;3:font5x7_row=5'b10001;4:font5x7_row=5'b10001;5:font5x7_row=5'b10001;default:font5x7_row=5'b01110;endcase
                "P": case(row) 0:font5x7_row=5'b11110;1:font5x7_row=5'b10001;2:font5x7_row=5'b10001;3:font5x7_row=5'b11110;4:font5x7_row=5'b10000;5:font5x7_row=5'b10000;default:font5x7_row=5'b10000;endcase
                "Q": case(row) 0:font5x7_row=5'b01110;1:font5x7_row=5'b10001;2:font5x7_row=5'b10001;3:font5x7_row=5'b10001;4:font5x7_row=5'b10101;5:font5x7_row=5'b10010;default:font5x7_row=5'b01101;endcase
                "R": case(row) 0:font5x7_row=5'b11110;1:font5x7_row=5'b10001;2:font5x7_row=5'b10001;3:font5x7_row=5'b11110;4:font5x7_row=5'b10100;5:font5x7_row=5'b10010;default:font5x7_row=5'b10001;endcase
                "S": case(row) 0:font5x7_row=5'b01111;1:font5x7_row=5'b10000;2:font5x7_row=5'b10000;3:font5x7_row=5'b01110;4:font5x7_row=5'b00001;5:font5x7_row=5'b00001;default:font5x7_row=5'b11110;endcase
                "T": case(row) 0:font5x7_row=5'b11111;1:font5x7_row=5'b00100;2:font5x7_row=5'b00100;3:font5x7_row=5'b00100;4:font5x7_row=5'b00100;5:font5x7_row=5'b00100;default:font5x7_row=5'b00100;endcase
                "U": case(row) 0:font5x7_row=5'b10001;1:font5x7_row=5'b10001;2:font5x7_row=5'b10001;3:font5x7_row=5'b10001;4:font5x7_row=5'b10001;5:font5x7_row=5'b10001;default:font5x7_row=5'b01110;endcase
                "V": case(row) 0:font5x7_row=5'b10001;1:font5x7_row=5'b10001;2:font5x7_row=5'b10001;3:font5x7_row=5'b10001;4:font5x7_row=5'b10001;5:font5x7_row=5'b01010;default:font5x7_row=5'b00100;endcase
                "W": case(row) 0:font5x7_row=5'b10001;1:font5x7_row=5'b10001;2:font5x7_row=5'b10001;3:font5x7_row=5'b10101;4:font5x7_row=5'b10101;5:font5x7_row=5'b10101;default:font5x7_row=5'b01010;endcase
                "X": case(row) 0:font5x7_row=5'b10001;1:font5x7_row=5'b10001;2:font5x7_row=5'b01010;3:font5x7_row=5'b00100;4:font5x7_row=5'b01010;5:font5x7_row=5'b10001;default:font5x7_row=5'b10001;endcase
                "Y": case(row) 0:font5x7_row=5'b10001;1:font5x7_row=5'b10001;2:font5x7_row=5'b01010;3:font5x7_row=5'b00100;4:font5x7_row=5'b00100;5:font5x7_row=5'b00100;default:font5x7_row=5'b00100;endcase
                "Z": case(row) 0:font5x7_row=5'b11111;1:font5x7_row=5'b00001;2:font5x7_row=5'b00010;3:font5x7_row=5'b00100;4:font5x7_row=5'b01000;5:font5x7_row=5'b10000;default:font5x7_row=5'b11111;endcase
                ":": case(row) 0:font5x7_row=5'b00000;1:font5x7_row=5'b00100;2:font5x7_row=5'b00100;3:font5x7_row=5'b00000;4:font5x7_row=5'b00100;5:font5x7_row=5'b00100;default:font5x7_row=5'b00000;endcase
                "?": case(row) 0:font5x7_row=5'b01110;1:font5x7_row=5'b10001;2:font5x7_row=5'b00010;3:font5x7_row=5'b00100;4:font5x7_row=5'b00100;5:font5x7_row=5'b00000;default:font5x7_row=5'b00100;endcase
                "/": case(row) 0:font5x7_row=5'b00001;1:font5x7_row=5'b00010;2:font5x7_row=5'b00010;3:font5x7_row=5'b00100;4:font5x7_row=5'b01000;5:font5x7_row=5'b01000;default:font5x7_row=5'b10000;endcase
                "+": case(row) 2:font5x7_row=5'b00100;3:font5x7_row=5'b11111;4:font5x7_row=5'b00100;default:font5x7_row=5'b00000;endcase
                "-": case(row) 3:font5x7_row=5'b11111;default:font5x7_row=0;endcase
                ".": case(row) 6:font5x7_row=5'b00100;default:font5x7_row=0;endcase
                ">": case(row)1:font5x7_row=5'b10000;2:font5x7_row=5'b01000;3:font5x7_row=5'b00100;4:font5x7_row=5'b01000;5:font5x7_row=5'b10000;default:font5x7_row=0;endcase
                "=": case(row)2,4:font5x7_row=5'b11111;default:font5x7_row=0;endcase
                default: font5x7_row=5'b00000;
            endcase
        end
    endfunction
 function font_pixel;
  input [3:0] code;
  input [7:0] fx,fy;
  reg [34:0] bits;
  begin
   case(code)
    0:bits=35'b11110_10001_10001_10001_10001_10001_11110; // D
    1:bits=35'b01111_10000_10000_10000_10000_10000_01111; // C
    2:bits=35'b10001_10001_10001_11111_10001_10001_10001; // H
    3:bits=35'b01111_10000_10000_01110_00001_00001_11110; // S
    4:bits=35'b11110_10001_10001_11110_10100_10010_10001; // R
    5:bits=35'b00100_01100_00100_00100_00100_00100_01110; // 1
    6:bits=35'b01110_10001_00001_00010_00100_01000_11111; // 2
    7:bits=35'b11110_00001_00001_01110_00001_00001_11110; // 3
    8:bits=35'b00010_00110_01010_10010_11111_00010_00010; // 4
    default:bits=0;
   endcase
   if(fx<5 && fy<7)font_pixel=bits[(6-fy)*5+(4-fx)];else font_pixel=0;
  end
 endfunction
 always @* begin
  board_hit=0;draw_hit=0;xhit=1;yhit=1;col=0;row=0;lx=0;ly=0;state=0;
  font_x=0;font_y=0;font_code=0;glyph=0;ink=0;
  legend_idx=0;legend_label="      ";legend_lx=0;legend_ly=0;
  legend_colour=0;legend_bits=0;legend_char=" ";show_arrow=0;
  if(sx>=GRID_X0 && sx<GRID_X0+GRID_SLOT_WIDTH)begin col=0;lx=sx-GRID_X0;end
  else if(sx>=GRID_X0+GRID_STEP_X && sx<GRID_X0+GRID_STEP_X+GRID_SLOT_WIDTH)begin col=1;lx=sx-(GRID_X0+GRID_STEP_X);end
  else if(sx>=GRID_X0+8'd2*GRID_STEP_X && sx<GRID_X0+8'd2*GRID_STEP_X+GRID_SLOT_WIDTH)begin col=2;lx=sx-(GRID_X0+8'd2*GRID_STEP_X);end
  else if(sx>=GRID_X0+8'd3*GRID_STEP_X && sx<GRID_X0+8'd3*GRID_STEP_X+GRID_SLOT_WIDTH)begin col=3;lx=sx-(GRID_X0+8'd3*GRID_STEP_X);end
  else xhit=0;
  if(sy>=GRID_Y0 && sy<GRID_Y0+GRID_SLOT_HEIGHT)begin row=0;ly=sy-GRID_Y0;end
  else if(sy>=GRID_Y0+GRID_STEP_Y && sy<GRID_Y0+GRID_STEP_Y+GRID_SLOT_HEIGHT)begin row=1;ly=sy-(GRID_Y0+GRID_STEP_Y);end
  else if(sy>=GRID_Y0+8'd2*GRID_STEP_Y && sy<GRID_Y0+8'd2*GRID_STEP_Y+GRID_SLOT_HEIGHT)begin row=2;ly=sy-(GRID_Y0+8'd2*GRID_STEP_Y);end
  else if(sy>=GRID_Y0+8'd3*GRID_STEP_Y && sy<GRID_Y0+8'd3*GRID_STEP_Y+GRID_SLOT_HEIGHT)begin row=3;ly=sy-(GRID_Y0+8'd3*GRID_STEP_Y);end
  else yhit=0;
  board_hit=xhit&&yhit;
  if(sx>=GRID_DRAW_X0 && sx<GRID_DRAW_X0+GRID_SLOT_WIDTH &&
     sy>=GRID_DRAW_Y0 && sy<GRID_DRAW_Y0+GRID_SLOT_HEIGHT)begin
    draw_hit=1;board_hit=0;lx=sx-GRID_DRAW_X0;ly=sy-GRID_DRAW_Y0;state=draw_state;
  end
  if(board_hit)begin
   case({row,col})
    4'h0:state=cell_states[1:0];4'h1:state=cell_states[3:2];4'h2:state=cell_states[5:4];4'h3:state=cell_states[7:6];
    4'h4:state=cell_states[9:8];4'h5:state=cell_states[11:10];4'h6:state=cell_states[13:12];4'h7:state=cell_states[15:14];
    4'h8:state=cell_states[17:16];4'h9:state=cell_states[19:18];4'hA:state=cell_states[21:20];4'hB:state=cell_states[23:22];
    4'hC:state=cell_states[25:24];4'hD:state=cell_states[27:26];4'hE:state=cell_states[29:28];default:state=cell_states[31:30];
   endcase
  end
  if(board_hit||draw_hit)begin
   if(lx==0||lx==GRID_SLOT_WIDTH-1||ly==0||ly==GRID_SLOT_HEIGHT-1)begin
    if(!guide_mode)ink=draw_hit?7:1;
    else if(state==1)ink=4;else if(state==2)ink=5;else if(state==3)ink=6;else ink=draw_hit?7:1;
   end
   if(lx>=2&&lx<15&&ly>=2&&ly<11)ink=2;
   if(ly>=3&&ly<10)begin
    font_y=ly-3;
    if(lx>=3&&lx<8)begin
     font_x=lx-3;glyph=1;
     if(draw_hit)font_code=0;
     else case(col)0:font_code=0;1:font_code=1;2:font_code=2;default:font_code=3;endcase
    end else if(lx>=9&&lx<14)begin
     font_x=lx-9;glyph=1;
     if(draw_hit)font_code=4;else font_code=5+row;
    end
    if(glyph&&font_pixel(font_code,font_x,font_y))ink=3;
   end
  end
  // PURPLE PLACE highlight is independent of the last scan status.
  // The selected target stays visible even when the physical cell is empty.
  if(board_hit && target_active && {row,col}==target_cell)begin
    if(lx<=2||lx>=GRID_SLOT_WIDTH-3||ly<=2||ly>=GRID_SLOT_HEIGHT-3)ink=8;
  end
  // V16.4.2: properly DOWNWARD arrow (wide head at TOP, tapering to a
  // single point at BOTTOM). Old triangle widened toward its bottom and
  // looked like an inverted/forked Y in the real camera preview.
  // Keep all pixels inside the 45x29 cell and below its name label.
  if(board_hit && move_mask[{row,col}] && blink)begin
    if((lx>=20&&lx<=24&&ly>=11&&ly<=17) ||
       (ly>=18&&ly<=26&&
        lx>=22-(26-ly) && lx<=22+(26-ly)))ink=9;
  end
  // HG: the three cells below DRAW merged into one hand-gesture zone
  // (the HPS crops exactly this area for the gesture CNN). Amber double outline,
  // "HG" label in the top-left corner like the cell labels.
  if(sx>=GRID_DRAW_X0 && sx<GRID_DRAW_X0+GRID_SLOT_WIDTH &&
     sy>=GRID_DRAW_Y0+GRID_STEP_Y && sy<GRID_Y0+8'd4*GRID_STEP_Y)begin
    lx=sx-GRID_DRAW_X0;ly=sy-(GRID_DRAW_Y0+GRID_STEP_Y);
    if(lx<=1||lx>=GRID_SLOT_WIDTH-2||ly<=1||ly>=8'd3*GRID_STEP_Y-2)ink=6;
    if(lx>=2&&lx<15&&ly>=2&&ly<11)ink=2;
    if(ly>=3&&ly<10)begin
      if(lx>=3&&lx<8)begin legend_bits=font5x7_row("H",ly-3);if(legend_bits[7-lx])ink=3;end
      else if(lx>=9&&lx<14)begin legend_bits=font5x7_row("G",ly-3);if(legend_bits[13-lx])ink=3;end
    end
  end
  // Status legend: two rows under the thumbnail (overlay rows 136..159, below the
  // camera image). GOOD/EMPTY/BAD/JOKER, then MOVE/PLACE/DRAW/HAND. Swatches match guide colours.
  if(sy>=136 && sy<160)begin
    ink=2;
    legend_ly=(sy<148)?sy-136:sy-148;
    if(sx<60)begin legend_idx=0;legend_lx=sx;end
    else if(sx<120)begin legend_idx=1;legend_lx=sx-60;end
    else if(sx<180)begin legend_idx=2;legend_lx=sx-120;end
    else begin legend_idx=3;legend_lx=sx-180;end
    if(sy>=148)legend_idx=legend_idx+3'd4;
    case(legend_idx)
      0: begin legend_label="GOOD  ";legend_colour=4;end
      1: begin legend_label="EMPTY ";legend_colour=1;end
      2: begin legend_label="BAD   ";legend_colour=5;end
      3: begin legend_label="JOKER ";legend_colour=10;end
      4: begin legend_label="MOVE  ";legend_colour=9;end
      5: begin legend_label="PLACE ";legend_colour=8;end
      6: begin legend_label="DRAW  ";legend_colour=7;end
      default:begin legend_label="HAND  ";legend_colour=6;end   // HG gesture zone (amber)
    endcase
    if(legend_lx>=3 && legend_lx<8 && legend_ly>=3 && legend_ly<9)ink=legend_colour;
    if(legend_lx>=11 && legend_lx<47 && legend_ly>=3 && legend_ly<10)begin
      // Explicit character decode avoids /6 and modulo on the HDMI clock.
      if(legend_lx<17)begin legend_char=legend_label[47:40];font_x=legend_lx-11;end
      else if(legend_lx<23)begin legend_char=legend_label[39:32];font_x=legend_lx-17;end
      else if(legend_lx<29)begin legend_char=legend_label[31:24];font_x=legend_lx-23;end
      else if(legend_lx<35)begin legend_char=legend_label[23:16];font_x=legend_lx-29;end
      else if(legend_lx<41)begin legend_char=legend_label[15:8];font_x=legend_lx-35;end
      else begin legend_char=legend_label[7:0];font_x=legend_lx-41;end
      if(font_x<5)begin
        legend_bits=font5x7_row(legend_char,legend_ly-3);
        if(legend_bits[4-font_x])ink=3;
      end
    end
  end
 end
endmodule
`endif

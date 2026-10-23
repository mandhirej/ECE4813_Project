/* Host models of the DE10-Nano hardware used by atlas_main.c (simulation only).
 *
 * Included right after vision_core.h (built with VISION_HOST_TEST) in the
 * generated host copy of atlas_main.c. Everything above the hardware stays the
 * real HPS code: game rules, board vision, voting, gesture hold, HDMI text.
 *
 *   FPGA mailbox  : same pair/toggle/ACK protocol and opcode semantics as the
 *                   DE10_Nano_D8M_DDR3.v decoder; keeps the HDMI display state.
 *   Turn timer    : turn_timer_120s.v behaviour on simulated time.
 *   Snapshot      : virtual 480x270 camera (dark board, real card photos, real
 *                   hand photos) written into frame[] like the DMA would.
 *   CNN bridge    : integer CNN from the Quartus weight files (bit-exact with
 *                   CNN.v in the iverilog check), card or gesture class range.
 *   Player        : reads the HDMI instruction rows, does the physical action
 *                   (draw, move, place, remove joker, show a hand) and answers
 *                   with a key (keyboard mode) or a held gesture (gesture mode).
 */
#ifndef SIM_HW_H
#define SIM_HW_H
#include <stdlib.h>
#include <math.h>
#include <ctype.h>
#include "cnn_weights.h"
#include "fixed_board_grid.h"
#define HG_Y0 (GRID_DRAW_Y0+GRID_STEP_Y)   /* gesture zone: the three cells below DRAW */
static int gesture_mode_on=0;          /* player's view: gestures in use */
static int deck[16],deck_pos;
static int sim_sw0=0;                  /* the simulated SW0 (stays down: H key is tested) */

/* ------------------------------------------------------------------ time */
static uint64_t sim_ns;
static void sim_ms(double ms){sim_ns+=(uint64_t)(ms*1e6);}
static void sim_halt(const char *why);

/* ------------------------------------------------------------------ turn timer */
static int tt_valid,tt_running,tt_expired,tt_opp,tt_left;
static uint64_t tt_start;
static void tt_update(void)
{
    if(!tt_running)return;
    {
        uint64_t el=(sim_ns-tt_start)/1000000000ull;
        if(el>=120u){tt_left=0;tt_running=0;tt_expired=1;}
        else tt_left=120-(int)el;
    }
}
static void tt_command(unsigned c)
{
    tt_update();
    if(c<=1u){tt_valid=1;tt_running=1;tt_expired=0;tt_opp=(int)c;tt_left=120;tt_start=sim_ns;}
    else if(c==2u)tt_running=0;
    else {tt_valid=0;tt_running=0;tt_expired=0;tt_left=0;}
}

/* ------------------------------------------------------------------ FPGA display state */
typedef struct {
    uint8_t cls[16],st[16],score,turn,ui_action,target,target_active,guide_mode,draw_state;
    uint8_t guide[16];
    uint16_t move_mask;
    char text[8][29];
} FpgaView;
static FpgaView fv;
static unsigned mb_seen_toggle,mb_pending,mb_first,mb_ack,mb_err;
static int mb_sel_valid,mb_low_valid,mb_reset_armed;
static unsigned mb_sel,mb_low,mb_phase,mb_lane,mb_pos,mb_hi,mb_move_stage;
static char mb_staging[29];
static uint32_t sim_cmd_var,sim_cmd_done,sim_status_var,sim_cnn_status_var;
static void player_on_text_row(unsigned row);

static void fpga_reset_view(void)
{
    memset(&fv,0,sizeof(fv));fv.turn=1;
    for(unsigned r=0;r<8;r++){memset(fv.text[r],' ',28);fv.text[r][28]=0;}
}
static void fpga_execute(unsigned body)
{
    unsigned op=(body>>4)&7u,p=body&15u;
    mb_err=0;
    if(op!=7u)mb_reset_armed=0;
    switch(op){
    case 0: /* CAPTURE: text stream / move mask / placement */
        switch(mb_phase){
        case 0: if(p==0xF)mb_phase=1; else if(p==0xD)mb_phase=4;
                else if(p==0xB)fv.target_active=1; else if(p==0xC){fv.move_mask=0;fv.target_active=0;} break;
        case 1: if(p<8u){mb_lane=p;mb_pos=0;memset(mb_staging,' ',28);mb_staging[28]=0;mb_phase=2;} else mb_phase=0; break;
        case 2: if(p==0xE){memcpy(fv.text[mb_lane],mb_staging,29);mb_phase=0;player_on_text_row(mb_lane);}
                else {mb_hi=p;mb_phase=3;} break;
        case 3: mb_staging[mb_pos]=(char)((mb_hi<<4)|p); if(mb_pos<27u)mb_pos++; mb_phase=2; break;
        case 4: mb_move_stage=p<<8;mb_phase=5;break;
        case 5: mb_move_stage|=p<<4;mb_phase=6;break;
        case 6: mb_move_stage|=p;mb_phase=7;break;
        case 7: fv.move_mask=(uint16_t)((mb_move_stage<<4)|p);mb_phase=0;break;
        }
        mb_sel_valid=0;mb_low_valid=0;break;
    case 1: mb_sel=p;mb_sel_valid=1;mb_low_valid=0;break;
    case 2: if(mb_sel_valid){mb_low=p;mb_low_valid=1;} else {mb_err=1;mb_low_valid=0;} break;
    case 3: if(mb_sel_valid&&mb_low_valid&&(((p&3u)<<4)|mb_low)<=53u){
                fv.cls[mb_sel]=(uint8_t)(((p&3u)<<4)|mb_low);fv.st[mb_sel]=(uint8_t)((p>>2)&3u);
            } else mb_err=1;
            mb_sel_valid=0;mb_low_valid=0;break;
    case 4: mb_sel_valid=0;mb_low_valid=0;if(p<=8u)fv.score=(uint8_t)p;else mb_err=1;break;
    case 5: if(mb_sel_valid&&!mb_low_valid)fv.turn=(uint8_t)((p<<4)|mb_sel);else mb_err=1;
            mb_sel_valid=0;mb_low_valid=0;break;
    case 6:
        if(p<=7u)fv.ui_action=(uint8_t)p;
        else if(p<=0xBu){ if(mb_sel_valid&&!mb_low_valid){fv.guide[mb_sel]=(uint8_t)(p&3u);fv.guide_mode=1;} else mb_err=1; }
        else if(p==0xCu){fv.guide_mode=1;memset(fv.guide,0,16);fv.draw_state=0;fv.move_mask=0;fv.target_active=0;}
        else if(p==0xDu){ if(mb_sel_valid&&!mb_low_valid)fv.draw_state=(uint8_t)(mb_sel&3u); else mb_err=1; }
        else if(p==0xEu){fv.guide_mode=0;memset(fv.guide,0,16);fv.draw_state=0;fv.move_mask=0;fv.target_active=0;}
        else { if(mb_sel_valid&&!mb_low_valid)fv.target=(uint8_t)mb_sel; else mb_err=1; }
        mb_sel_valid=0;mb_low_valid=0;break;
    case 7:
        mb_sel_valid=0;mb_low_valid=0;
        if((p>>2)==0u)tt_command(p);
        else if(p==0xAu)mb_reset_armed=1;
        else if(p==0x5u&&mb_reset_armed){mb_reset_armed=0;fpga_reset_view();mb_phase=0;}
        break;
    }
}
/* hps_hdmi decoder: a raw command is new when its toggle bit changes; the first
 * copy is staged, the matching second copy executes and flips the ACK. */
static void mailbox_service(void)
{
    if(sim_cmd_var==sim_cmd_done)return;
    sim_cmd_done=sim_cmd_var;
    if(((sim_cmd_var>>7)&1u)==mb_seen_toggle)return;
    mb_seen_toggle=(sim_cmd_var>>7)&1u;
    if(!mb_pending){mb_first=sim_cmd_var&0x7Fu;mb_pending=1;return;}
    mb_pending=0;
    if((sim_cmd_var&0x7Fu)!=mb_first)mb_err=1; else fpga_execute(mb_first);
    mb_ack^=1u;sim_ms(0.02);
}
static volatile uint32_t *sim_game_reg(int which)
{
    mailbox_service();
    if(which==1)return &sim_cmd_var;
    if(which==0){
        tt_update();
        sim_status_var=(mb_ack<<5)|(mb_err<<4)|((uint32_t)tt_valid<<3)|((uint32_t)tt_opp<<2)|
                       ((uint32_t)tt_running<<1)|(uint32_t)tt_expired;
        return &sim_status_var;
    }
    sim_cnn_status_var=(uint32_t)sim_sw0<<7;           /* SW0: board switch for the input mode */
    return &sim_cnn_status_var;
}

/* ------------------------------------------------------------------ CNN bridge (integer model) */
static uint32_t sim_ureg_var[16];
static volatile uint32_t *sim_ureg(unsigned n)
{
    if(n==15u){sim_ns+=1000;sim_ureg_var[15]=(uint32_t)(sim_ns/20u);}
    else if(n==0u)sim_ureg_var[0]=0;          /* idle, never busy/error */
    else if(n==7u)sim_ureg_var[7]=0x434e4e31u;
    else if(n==13u)sim_ureg_var[13]=0x10000u;
    return &sim_ureg_var[n&15u];
}
#define UREG(n) (*sim_ureg(n))

static unsigned sim_cnn_runs;
static int hand_gesture=-1;             /* 0 fist, 1 palm, 2 peace, -1 none */
static unsigned gesture_reads[4][4];    /* [hand shown+1][class read] */
static unsigned cnn_classify(const uint8_t *img,int gesture)
{
    static uint8_t q1[8][32][32],q2[16][16][16];
    int oc,ic,oy,ox,ky,kx;
    for(oc=0;oc<8;oc++)for(oy=0;oy<32;oy++)for(ox=0;ox<32;ox++){
        int32_t acc=sim_b1[oc];
        for(ky=0;ky<3;ky++)for(kx=0;kx<3;kx++){
            int y=2*oy+ky-1,x=2*ox+kx-1;
            if(y>=0&&y<64&&x>=0&&x<64)acc+=sim_w1[oc*9+ky*3+kx]*(int32_t)img[y*64+x];
        }
        acc=acc<0?0:(acc>>SIM_L1_SHIFT);q1[oc][oy][ox]=(uint8_t)(acc>255?255:acc);
    }
    for(oc=0;oc<16;oc++)for(oy=0;oy<16;oy++)for(ox=0;ox<16;ox++){
        int32_t acc=sim_b2[oc];
        for(ic=0;ic<8;ic++)for(ky=0;ky<3;ky++)for(kx=0;kx<3;kx++){
            int y=2*oy+ky-1,x=2*ox+kx-1;
            if(y>=0&&y<32&&x>=0&&x<32)acc+=sim_w2[((oc*8+ic)*3+ky)*3+kx]*(int32_t)q1[ic][y][x];
        }
        acc=acc<0?0:(acc>>SIM_L2_SHIFT);q2[oc][oy][ox]=(uint8_t)(acc>255?255:acc);
    }
    {
        unsigned lo=gesture?54u:0u,hi=gesture?57u:53u,c,best=lo;int64_t bests=0;int have=0;
        for(c=lo;c<=hi;c++){
            int64_t s=sim_bfc[c];const int8_t *w=&sim_wfc[c*4096u];const uint8_t *f=&q2[0][0][0];
            for(unsigned i=0;i<4096u;i++)s+=(int64_t)w[i]*f[i];
            if(!have||s>bests){bests=s;best=c;have=1;}   /* RTL: strict '>' keeps the first max */
        }
        return best;
    }
}
#define CNN_GESTURE_MAGIC 0x7FFu
static int cnn_run_mode(const uint8_t *pixels,int live_bbox,int gesture,uint8_t *prediction)
{
    (void)live_bbox;
    *prediction=(uint8_t)cnn_classify(pixels,gesture);
    sim_ms(gesture?8.5:14.6);sim_cnn_runs++;
    if(gesture)gesture_reads[hand_gesture+1][*prediction-54u]++;
    return 1;
}
static int cnn_run(const uint8_t *pixels,int live_bbox,uint8_t *prediction)
{
    return cnn_run_mode(pixels,live_bbox,0,prediction);
}

/* ------------------------------------------------------------------ virtual camera */
typedef struct { int id,w,h; uint8_t *px; } Sprite;
static Sprite sim_cards[32],sim_hands[32];
static unsigned sim_ncards,sim_nhands;
static int phys[17];          /* class per board cell 0..15 and DRAW (16); -1 empty */
static unsigned hand_variant;
static float board_tex[N];
static uint32_t sim_rng=12345u;
static unsigned rnd(unsigned n){sim_rng=sim_rng*1664525u+1013904223u;return (sim_rng>>8)%n;}
static double rndf(void){return rnd(1000000u)/1000000.0;}

static void load_assets(void)
{
    FILE *f=fopen("assets.bin","rb");uint32_t nc,nh,i;
    if(!f)sim_halt("assets.bin missing");
    if(fread(&nc,4,1,f)!=1||fread(&nh,4,1,f)!=1)sim_halt("assets.bin bad");
    for(i=0;i<nc+nh;i++){
        int32_t hdr[3];Sprite s;size_t bytes;
        if(fread(hdr,4,3,f)!=3)sim_halt("assets.bin truncated");
        s.id=hdr[0];s.w=hdr[1];s.h=hdr[2];bytes=(size_t)s.w*s.h*(i<nc?3u:4u);
        s.px=(uint8_t*)malloc(bytes);if(fread(s.px,1,bytes,f)!=bytes)sim_halt("assets.bin truncated");
        if(i<nc)sim_cards[sim_ncards++]=s;else sim_hands[sim_nhands++]=s;
    }
    fclose(f);
    for(i=0;i<N;i++){
        unsigned x=i%W,y=i/W;
        board_tex[i]=(float)(30.0+7.0*sin(x/41.0)+5.0*cos(y/23.0+x/97.0)+4.0*(double)y/H+(rndf()-0.5)*6.0);
    }
}
static const Sprite *card_sprite(int cls)
{
    for(unsigned i=0;i<sim_ncards;i++)if(sim_cards[i].id==cls)return &sim_cards[i];
    return 0;
}
static void put_card(uint32_t *dst,int cls,int cx0,int cy0,double gain)
{
    const Sprite *s=card_sprite(cls);int x,y,jx=(int)rnd(3)-1,jy=(int)rnd(3)-1;
    if(!s)return;
    for(y=0;y<s->h;y++)for(x=0;x<s->w;x++){
        int fx=cx0+(GRID_SLOT_WIDTH-s->w)/2+jx+x,fy=cy0+(GRID_SLOT_HEIGHT-s->h)/2+jy+y;
        const uint8_t *p=&s->px[(y*s->w+x)*3];
        unsigned r=(unsigned)fmin(255,p[0]*gain),g=(unsigned)fmin(255,p[1]*gain),b=(unsigned)fmin(255,p[2]*gain);
        if(fx>=0&&fx<W&&fy>=0&&fy<H)dst[fy*W+fx]=(r<<16)|(g<<8)|b;
    }
}
static void render_camera(uint32_t *dst)
{
    unsigned i;double gain=0.95+0.1*rndf();
    for(i=0;i<N;i++){
        int v=(int)(board_tex[i]+(rndf()-0.5)*5.0);v=v<0?0:v>255?255:v;
        dst[i]=((uint32_t)v<<16)|((uint32_t)v<<8)|(uint32_t)(v+4>255?255:v+4);
    }
    for(i=0;i<16;i++)if(phys[i]>=0)put_card(dst,phys[i],GRID_X0+(int)(i%4)*GRID_STEP_X,GRID_Y0+(int)(i/4)*GRID_STEP_Y,gain);
    if(phys[16]>=0)put_card(dst,phys[16],GRID_DRAW_X0,GRID_DRAW_Y0,gain);
    if(hand_gesture>=0){
        unsigned k,n=0;const Sprite *s=0;
        for(k=0;k<sim_nhands;k++)if(sim_hands[k].id==hand_gesture){if(n==hand_variant%6u)s=&sim_hands[k];n++;}
        if(s){int x,y,jx=(int)rnd(5)-2,jy=(int)rnd(5)-2,ax0=s->w,ax1=0;unsigned sr=0,sg=0,sb=0,sn=0;
            /* forearm: the hand comes in from the DRAW side, so the arm lies across the
             * DR cell down to the frame edge, as wide as the wrist in the sprite */
            for(x=0;x<s->w;x++){const uint8_t *p=&s->px[((s->h-1)*s->w+x)*4];
                if(p[3]>128){if(x<ax0)ax0=x;if(x>ax1)ax1=x;sr+=p[0];sg+=p[1];sb+=p[2];sn++;}}
            if(sn)for(y=HG_Y0+s->h+jy;y<H;y++)for(x=ax0;x<=ax1;x++){
                int fx=GRID_DRAW_X0+x+jx;double shade=0.9+0.1*sin(x*0.2)+(rndf()-0.5)*0.06;
                if(fx<0||fx>=W||y<0)continue;
                dst[y*W+fx]=((uint32_t)fmin(255,sr/sn*gain*shade)<<16)|((uint32_t)fmin(255,sg/sn*gain*shade)<<8)|
                            (uint32_t)fmin(255,sb/sn*gain*shade);
            }
            for(y=0;y<s->h;y++)for(x=0;x<s->w;x++){
                int fx=GRID_DRAW_X0+x+jx,fy=HG_Y0+y+jy;const uint8_t *p=&s->px[(y*s->w+x)*4];
                double a=p[3]/255.0;uint32_t o;
                if(fx<0||fx>=W||fy<0||fy>=H||a<=0)continue;
                o=dst[fy*W+fx];
                dst[fy*W+fx]=((uint32_t)(a*p[0]*gain+(1-a)*((o>>16)&255))<<16)|
                               ((uint32_t)(a*p[1]*gain+(1-a)*((o>>8)&255))<<8)|
                               (uint32_t)(a*p[2]*gain+(1-a)*(o&255));
            }}
    }
}
/* snapshot IP V2 window: the virtual camera is 480x270, so the window is filled by
 * nearest-pixel upsampling. This checks the high-res crop geometry, not its gain. */
static unsigned sim_windows;
static int snapshot_window(unsigned x0,unsigned y0,unsigned w,unsigned h)
{
    static uint32_t view[N];unsigned i,j,cw=w>>HIRES_STEP,ch=h>>HIRES_STEP;
    if(cw*ch>HIRES_MAX_WORDS)return 0;
    render_camera(view);
    for(j=0;j<ch;j++)for(i=0;i<cw;i++){
        unsigned sx=(x0+(i<<HIRES_STEP))/4u,sy=(y0+(j<<HIRES_STEP))/4u;
        hires[j*cw+i]=view[(sy<H?sy:H-1)*W+(sx<W?sx:W-1)];
    }
    hires_x0=x0;hires_y0=y0;hires_w=w;hires_h=h;sim_ms(40.0);sim_windows++;
    return 1;
}
static int vision_init(void)
{
    hires_supported=1;
    load_assets();
    printf("SIM: virtual DE10-Nano: %u card sprites, %u hand sprites, CNN %u classes\n",sim_ncards,sim_nhands,SIM_FC_CLASSES);
    return 1;
}
static unsigned sim_snapshots;
static uint64_t hand_since,hand_remove_at,reach_until,draw_place_at;
static int draw_reinsert=-1;
static int intended_gesture=-1,touched_draw;
static int awaiting_gesture;
static int snapshot_frame(void)
{
    /* the player pulls the hand away ~0.8 s after seeing HAND COMMAND */
    if(hand_remove_at&&sim_ns>=hand_remove_at){hand_gesture=-1;hand_remove_at=0;}
    if(draw_place_at&&sim_ns>=draw_place_at){
        if(draw_reinsert>=0){phys[16]=draw_reinsert;draw_reinsert=-1;}
        else if(deck_pos<16)phys[16]=deck[deck_pos++];
        draw_place_at=0;
    }
    /* DRAW is at the top of column 5: putting a card there means reaching across HG
     * with an open hand (~1.2 s) before the real gesture is shown */
    if(reach_until){
        if(sim_ns<reach_until)hand_gesture=1;
        else{hand_gesture=intended_gesture;reach_until=0;hand_since=sim_ns;}
    }
    /* a person whose gesture is not being accepted changes the pose after ~8 s */
    if(hand_gesture>=0&&!hand_remove_at&&sim_ns-hand_since>8000000000ull){
        hand_variant=(hand_variant+1u)%6u;hand_since=sim_ns;
        printf(">>> PLAYER: gesture not accepted yet, re-poses the hand\n");
    }
    render_camera(frame);sim_ms(40.0);sim_snapshots++;return 1;
}

/* ------------------------------------------------------------------ trace for the HDMI renderer */
static FILE *trace_f;
static unsigned trace_n;
static void trace_step(const char *what)
{
    unsigned i;char name[64];FILE *cf;static uint32_t view[N];
    if(!trace_f){trace_f=fopen("trace.jsonl","w");}
    tt_update();
    render_camera(view);
    snprintf(name,sizeof(name),"cam_%04u.rgb",trace_n);
    cf=fopen(name,"wb");
    for(i=0;i<N;i++){uint8_t p[3]={(uint8_t)(view[i]>>16),(uint8_t)(view[i]>>8),(uint8_t)view[i]};fwrite(p,1,3,cf);}
    fclose(cf);
    fprintf(trace_f,"{\"n\":%u,\"t\":%.3f,\"what\":\"",trace_n,sim_ns/1e9);
    for(const char *c=what;*c;c++){if(*c=='"'||*c=='\\')fputc('\\',trace_f);fputc(*c,trace_f);}
    fprintf(trace_f,"\",\"cam\":\"%s\",\"text\":[",name);
    for(i=0;i<8;i++){
        fputc('"',trace_f);
        for(const char *c=fv.text[i];*c;c++){if(*c=='"'||*c=='\\')fputc('\\',trace_f);fputc(*c,trace_f);}
        fprintf(trace_f,"\"%s",i<7?",":"");
    }
    fprintf(trace_f,"],\"cls\":[");for(i=0;i<16;i++)fprintf(trace_f,"%u%s",fv.cls[i],i<15?",":"");
    fprintf(trace_f,"],\"st\":[");for(i=0;i<16;i++)fprintf(trace_f,"%u%s",fv.st[i],i<15?",":"");
    fprintf(trace_f,"],\"guide\":[");for(i=0;i<16;i++)fprintf(trace_f,"%u%s",fv.guide[i],i<15?",":"");
    fprintf(trace_f,"],\"guide_mode\":%u,\"draw\":%u,\"move\":%u,\"target\":%u,\"target_active\":%u,"
            "\"score\":%u,\"turn\":%u,\"tt\":[%d,%d,%d,%d,%d]}\n",
            fv.guide_mode,fv.draw_state,fv.move_mask,fv.target,fv.target_active,fv.score,fv.turn,
            tt_valid,tt_running,tt_expired,tt_opp,tt_left);
    fflush(trace_f);trace_n++;
}

/* ------------------------------------------------------------------ player */

static const int deck_classes[16]={37,5,9,13, 28,32,0,40, 30,34,2,42, 39,7,11,15};
static int use_gestures=1,games_played=0,opp_turns=0,our_choices=0,stuck=0,draw_card=-1;

static unsigned steps;

static void sim_halt(const char *why)
{
    static const char *gn[4]={"none ","fist ","palm ","peace"};unsigned gi,gj;
    printf("\nSIM gesture readings (rows: hand shown; columns: CNN read fist / palm / peace / nothing)\n");
    for(gi=0;gi<4;gi++){printf("  %s",gn[gi]);for(gj=0;gj<4;gj++)printf(" %7u",gesture_reads[gi][gj]);printf("\n");}
    printf("\nSIM high-res DRAW windows captured: %u\n",sim_windows);
    printf("\nSIM END: %s (simulated time %.1f s, %u snapshots, %u CNN runs)\n",why,sim_ns/1e9,sim_snapshots,sim_cnn_runs);
    if(trace_f)fclose(trace_f);
    fflush(stdout);exit(0);
}
static int row_has(const char *s){unsigned r;for(r=0;r<5;r++)if(strstr(fv.text[r],s))return 1;return 0;}
static int cell_index(const char *t){int col=-1;
    if(t[0]=='D')col=0;else if(t[0]=='C')col=1;else if(t[0]=='H')col=2;else if(t[0]=='S')col=3;
    if(col<0||t[1]<'1'||t[1]>'4')return -1;return (t[1]-'1')*4+col;}
/* cell tokens are a suit-column letter followed by 1..4, never part of a card name */
static int find_cells(const char *s,int out[4]){
    int n=0;for(const char *p=s;*p&&n<4;p++)
        if((p==s||p[-1]==' ')&&cell_index(p)>=0&&(p[2]==' '||p[2]==0||p[2]==';'))out[n++]=cell_index(p);
    return n;}
static const char *gesture_label(int g){return g==0?"FIST":g==1?"PALM":g==2?"PEACE":"none";}

/* Read the HDMI instructions, do the physical action, return the key. */
static char player_decide(char *say,size_t say_n)
{
    int cells[4];
    if(row_has("SYSTEM MENU")&&row_has("CONTINUE GAME")){snprintf(say,say_n,"continue the game that was left");return gesture_mode_on?'G':'A';}
    if(row_has("SYSTEM MENU")){
        if(games_played>=1){
            if(gesture_mode_on){gesture_mode_on=0;snprintf(say,say_n,"game over: PALM = back to keyboard");return 'Q';}
            snprintf(say,say_n,"game over: stop");return 'Q';}
        if(use_gestures&&!gesture_mode_on){gesture_mode_on=1;snprintf(say,say_n,"types H: hand gestures (SW0 stays down)");return 'H';}
        games_played++;snprintf(say,say_n,"start a new game");return 'G';
    }
    if(row_has("WHO STARTS")){snprintf(say,say_n,"our team starts");return '1';}
    if(strstr(fv.text[0],"OPPONENT TURN")){
        opp_turns++;
        if(opp_turns==3&&gesture_mode_on){   /* do nothing: the opponent's 120 s must hand over by themselves */
            snprintf(say,say_n,"opponent uses the full 120 s; player shows nothing");return 'W';
        }
        if(opp_turns==2){       /* opponent places a joker on an empty cell of our board */
            int c;for(c=15;c>=0;c--)if(phys[c]<0){phys[c]=45;break;}
            snprintf(say,say_n,"opponent plays; they put a JOKER on our board, then our turn");
        } else snprintf(say,say_n,"opponent plays, then our turn");
        sim_ms(5000);return '\n';
    }
    if(row_has("REMOVE OPP JOKER AT")){
        if(find_cells(fv.text[1],cells)==1)phys[cells[0]]=-1;
        snprintf(say,say_n,"remove the opponent joker");return '\n';
    }
    if(row_has("CHOOSE YOUR ACTION")){
        our_choices++;
        if(our_choices==3){snprintf(say,say_n,"quits to the menu by mistake");return 'Q';}
        if(our_choices==4&&row_has("PLACE YOUR JOKER")){snprintf(say,say_n,"use our own joker this turn");return 'J';}
        snprintf(say,say_n,"draw a card");return '\n';
    }
    if(row_has("USE YOUR JOKER")&&row_has("OPP EMPTY CELL")){snprintf(say,say_n,"put our joker on the opponent's board");return '\n';}
    if(row_has("PUT CARD IN DRAW SLOT")){
        if(phys[16]<0&&deck_pos<16){phys[16]=deck[deck_pos++];draw_card=phys[16];touched_draw=1;}
        snprintf(say,say_n,"draw the next card into DRAW");return '\n';
    }
    if(strncmp(fv.text[1],"STEP 3: PLACE",13)==0||strstr(fv.text[1],"; ")){
        int r,target=-1,moved=0;
        for(r=2;r<5;r++)if(strncmp(fv.text[r],"MOVE ",5)==0&&find_cells(fv.text[r],cells)==2){
            phys[cells[1]]=phys[cells[0]];phys[cells[0]]=-1;moved++;
        }
        if(find_cells(fv.text[1],cells)>=1)target=cells[0];
        if(target>=0){phys[target]=phys[16];phys[16]=-1;touched_draw=1;}
        snprintf(say,say_n,"do %d move(s), place the drawn card, then check",moved);stuck=0;return '\n';
    }
    if(row_has("NOT RECOGNISED")||row_has("INVALID OR USED")||row_has("CHECK CARD AT")||row_has("WRONG CARD")||
       row_has("STILL PRESENT")||row_has("SCAN FAILED")||row_has("DETECTED IN DRAW")){
        if(++stuck>6)sim_halt("player gave up after repeated recognition problems");
        if(row_has("DETECTED IN DRAW"))phys[16]=-1;
        snprintf(say,say_n,"adjust the card and retry");return '\n';
    }
    if(row_has("NO OWN JOKERS LEFT")){snprintf(say,say_n,"draw a card");return '\n';}
    snprintf(say,say_n,"(unknown screen) press ENTER");
    if(++stuck>6)sim_halt("player does not understand the screen");
    return '\n';
}
/* FIST = ENTER/1/new game, PEACE = J/2/scan, PALM = Q (gesture_key in gesture_core.h) */
static int key_to_gesture(char k){return (k=='\n'||k=='1'||k=='G')?2:(k=='J'||k=='2'||k=='A')?0:1;}

static void player_on_text_row(unsigned row)
{
    /* automatic DRAW scan: the player puts the card down ~1.5 s after the prompt */
    if(row==2u&&strncmp(fv.text[2],"PUT CARD IN DRAW SLOT",21)==0&&phys[16]<0&&!draw_place_at){
        draw_place_at=sim_ns+1500000000ull;printf(">>> PLAYER: sees PUT CARD IN DRAW SLOT, puts the next card down\n");}
    if(row==2u&&strncmp(fv.text[2],"TAKE IT OUT OF DRAW",19)==0&&phys[16]>=0){
        draw_reinsert=phys[16];phys[16]=-1;draw_place_at=sim_ns+2000000000ull;
        printf(">>> PLAYER: takes the card out of DRAW and puts it back\n");}
    if(row!=7u||!gesture_mode_on)return;
    if(strncmp(fv.text[7],"HAND: SHOW GESTURE",18)==0||strncmp(fv.text[7],"HAND: READY",11)==0){
        if(!awaiting_gesture){
            char say[96];char k=player_decide(say,sizeof(say));
            if(k=='W'){                 /* show nothing and let the timer decide */
                char w[160];hand_gesture=-1;
                printf(">>> PLAYER: %s\n",say);snprintf(w,sizeof(w),"PLAYER: %s",say);trace_step(w);
                return;
            }
            hand_gesture=key_to_gesture(k);hand_variant=rnd(6);awaiting_gesture=1;hand_since=sim_ns;
            if(k=='J')hand_variant=2;   /* start with the unclear PEACE pose to show the safety rule */
            if(touched_draw){intended_gesture=hand_gesture;hand_gesture=1;reach_until=sim_ns+1200000000ull;
                             touched_draw=0;printf(">>> PLAYER: reaches across HG to the DRAW slot (open hand, 1.2 s)\n");}
            printf(">>> PLAYER: %s -> holds %s in HG\n",say,gesture_label(reach_until?intended_gesture:hand_gesture));
            {char w[160];snprintf(w,sizeof(w),"PLAYER: %s / holds %s",say,gesture_label(reach_until?intended_gesture:hand_gesture));trace_step(w);}
        }
    } else if(strncmp(fv.text[7],"HAND COMMAND:",13)==0){
        char w[64];snprintf(w,sizeof(w),"gesture accepted: %s",fv.text[7]+14);trace_step(w);
        hand_remove_at=sim_ns+800000000ull;awaiting_gesture=0;
        if(++steps>400)sim_halt("step limit");
    } else if(strncmp(fv.text[7],"HAND: PALM 1.5",14)==0||strncmp(fv.text[7],"HAND: PEACE 1.5",15)==0||
              strncmp(fv.text[7],"HAND: FIST 1.5",14)==0){
        trace_step("holding gesture");
    }
    if(sim_ns>3600ull*1000000000ull)sim_halt("simulated hour elapsed");
}

/* fgets replacement: the player types the key (keyboard mode). */
static char *sim_fgets(char *buf,int n,FILE *f)
{
    char say[96],k,w[160];(void)f;
    k=player_decide(say,sizeof(say));
    if(k=='Q'&&games_played>=1&&row_has("SYSTEM MENU")){trace_step("PLAYER: game over");sim_halt("game finished and the player stopped at the menu");}
    printf(">>> PLAYER (keyboard): %s -> types %s\n",say,k=='\n'?"ENTER":(char[]){k,0});
    snprintf(w,sizeof(w),"PLAYER: %s / types %s",say,k=='\n'?"ENTER":(char[]){k,0});trace_step(w);
    sim_ms(2000);
    if(++steps>400)sim_halt("step limit");
    if(k=='\n'){buf[0]='\n';buf[1]=0;} else {buf[0]=k;buf[1]='\n';buf[2]=0;}
    (void)n;return buf;
}

static void sim_init_world(void)
{
    int i;
    if(getenv("SIM_KEYBOARD"))use_gestures=0;   /* keyboard-only player */for(i=0;i<17;i++)phys[i]=-1;
    for(i=0;i<16;i++)deck[i]=deck_classes[i];
    for(i=15;i>0;i--){int j=(int)rnd((unsigned)i+1u),t=deck[i];deck[i]=deck[j];deck[j]=t;}
    fpga_reset_view();
}
#endif

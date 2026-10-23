/* V16.4.3 SUIT STACK RACE - board-only joker scans + 7-to-8 rank recovery
 *
 * New game mode:
 *   - no card flipping and no back learning;
 *   - 4x4 own board: columns Diamond / Club / Heart / Spade;
 *   - one fixed DRAW slot at the top of the fifth column (HG gesture zone below it);
 *   - configurable 16-card deck in game_config.h;
 *   - cards in each suit are packed from the TOP and sorted high-to-low;
 *   - opponent jokers may occupy EMPTY board cells; system commands removal,
 *     but the player may spend the turn placing one of their own jokers instead;
 *   - all normal-card recognition uses rank-first constrained CNN voting;
 *   - D/S ambiguity for duplicate ranks is assisted by RGB red/black evidence;
 *   - affected suit column is verified after every placement.
 *
 * Requires the matching V16 FPGA top/overlay. No Platform Designer/Qsys change.
 */
#include <stdio.h>
#include <stdint.h>
#include <ctype.h>
#include <string.h>
#include "vision_core.h"
#include "board_core.h"
#include "game_core.h"

int __auto_semihosting;

#define GAME_STATUS_REG   (*(volatile uint32_t *)0xFF200000u)
#define GAME_COMMAND_REG  (*(volatile uint32_t *)0xFF200010u)
#define CNN_STATUS_REG    (*(volatile uint32_t *)0xFF200020u)

enum {
    CMD_CAPTURE=0, CMD_SELECT_CELL=1, CMD_CLASS_LOW=2, CMD_CELL_COMMIT=3,
    CMD_SCORE=4, CMD_TURN=5, CMD_CONTROL=6, CMD_RESET_GAME=7
};

enum {
    DISP_EMPTY=0,
    DISP_TARGET=1,
    DISP_JOKER=2,
    DISP_NORMAL=3
};

enum {
    UI_READY=0,
    UI_PLACE_DRAW=1,
    UI_PLACE_CARD=2,
    UI_REMOVE_JOKER=3,
    UI_USE_JOKER=4,
    UI_FIX_BOARD=5,
    UI_RETRY=6,
    UI_COMPLETE=7
};

enum {
    GUIDE_CLEAR=0,
    GUIDE_GOOD=1,
    GUIDE_BAD=2,
    GUIDE_ALERT=3
};

#define PRESENCE_FRAMES 3u
#define DRAW_MIN_VOTES 5u
#define DRAW_MAX_VOTES 9u
#define DRAW_MAX_ATTEMPTS 18u
#define JOKER_VOTES 5u
#define JOKER_MAX_ATTEMPTS 10u
#define VERIFY_FRAMES 3u
#define RED_SCORE_RED   BOARD_PIP_RED_THRESHOLD
#define RED_SCORE_BLACK BOARD_PIP_BLACK_THRESHOLD

typedef struct {
    uint8_t raw_class[DRAW_MAX_VOTES];
    uint8_t rank[DRAW_MAX_VOTES];
    unsigned red_score[DRAW_MAX_VOTES];
    unsigned count;
} DrawEvidence;

typedef struct {
    uint8_t status[BOARD_ACTIVE_CELLS]; /* 0 empty,1 occupied,2 ambiguous */
} PresenceScan;

/* Only manual A requests produce verbose scan progress. Normal game scans
 * and CNN recognition keep their previous behaviour and performance. */
static int manual_a_trace=0;

static uint8_t raw_command_toggle=0;
static uint8_t fpga_ack_state=0;
static int fpga_ack_known=0;

static uint16_t joker_mask=0;
static uint16_t opp_joker_seen=0;   /* opponent jokers on our board already counted */
static uint8_t joker_class[16];
static uint8_t pending_classes[16];
static uint16_t pending_mask=0;
static uint8_t pending_new_class=0;
static uint8_t pending_target=0;
static int pending_active=0;

static void short_poll_delay(void)
{
    volatile uint32_t i; for(i=0;i<150000u;i++)__asm volatile("nop");
}
static void mailbox_poll_delay(void)
{
    volatile uint32_t i; for(i=0;i<128u;i++)__asm volatile("nop");
}
static void io_barrier(void){__asm volatile("dmb sy" ::: "memory");}

static void mailbox_startup_resync(void)
{
    volatile uint32_t i;
    GAME_COMMAND_REG=0u;io_barrier();
    for(i=0;i<20000000u;i++)__asm volatile("nop");
    raw_command_toggle=0;fpga_ack_known=0;
}

static int status_get_ack(uint32_t status,uint8_t *ack,uint8_t *error)
{
    if(status&0x80u){*ack=(uint8_t)((status>>3)&1u);*error=(uint8_t)((status>>2)&1u);return 1;}
    if(!(status&0x40u)){*ack=(uint8_t)((status>>5)&1u);*error=(uint8_t)((status>>4)&1u);return 1;}
    return 0;
}

static int mailbox_write_copy(uint8_t opcode,uint8_t payload)
{
    uint8_t value;uint32_t i,consecutive=0;
    raw_command_toggle^=1u;
    value=(uint8_t)(((raw_command_toggle&1u)<<7)|((opcode&7u)<<4)|(payload&15u));
    GAME_COMMAND_REG=value;io_barrier();
    for(i=0;i<4096u;i++){
        uint8_t rb=(uint8_t)(GAME_COMMAND_REG&0xffu);
        if(rb==value){if(++consecutive>=16u)return 1;}else consecutive=0;
    }
    printf("MAILBOX WRITEBACK ERROR: wanted=0x%02X read=0x%02X\n",value,(unsigned)(GAME_COMMAND_REG&0xffu));
    return 0;
}

static int fpga_command_once(uint8_t opcode,uint8_t payload)
{
    uint32_t guard,status;uint8_t ack,error,ack_before;
    status=GAME_STATUS_REG;
    if(status_get_ack(status,&ack,&error)){fpga_ack_state=ack;fpga_ack_known=1;}
    else if(!fpga_ack_known)return 0;
    ack_before=fpga_ack_state;
    if(!mailbox_write_copy(opcode,payload)||!mailbox_write_copy(opcode,payload))return 0;
    for(guard=0;guard<500000u;guard++){
        status=GAME_STATUS_REG;
        if(status_get_ack(status,&ack,&error)&&ack!=ack_before){fpga_ack_state=ack;fpga_ack_known=1;return error?0:1;}
        mailbox_poll_delay();
    }
    printf("MAILBOX TIMEOUT: opcode=%u payload=0x%X status=0x%02X command=0x%02X\n",
           opcode,payload&15u,(unsigned)(GAME_STATUS_REG&0xffu),(unsigned)(GAME_COMMAND_REG&0xffu));
    return 0;
}

static int fpga_command(uint8_t opcode,uint8_t payload)
{
    unsigned a;for(a=0;a<5u;a++){if(fpga_command_once(opcode,payload))return 1;short_poll_delay();}
    printf("ERROR: HPS->FPGA command failed opcode=%u payload=0x%X\n",opcode,payload&15u);return 0;
}

static void fatal_fpga_link(void)
{
    puts("FATAL: V16 HPS->FPGA display mailbox failed. Stop rather than desynchronise the physical and HDMI boards.");
    fflush(stdout);for(;;)__asm volatile("wfi");
}

#include "hdmi_text_v16_4.h"

// FPGA timer: opcode 7 payload 0/1/2/3 has no overlap with the
// protected board reset sequence (A then 5). Enter remains semihosted.
// GAME_STATUS_REG idle bits: 3=valid, 2=opponent, 1=running, 0=expired.
#define TIMER_START_OURS     0u
#define TIMER_START_OPPONENT 1u
#define TIMER_STOP           2u
#define TIMER_CLEAR          3u
static int our_timed_input=0;
static int turn_timeout_seen=0;

static void fpga_timer_command(uint8_t operation)
{
    if(!fpga_command(CMD_RESET_GAME,operation))fatal_fpga_link();
}
static int fpga_timer_expired(void)
{
    // The original FPGA capture-result path is not used in V16 game mode.
    // Timer readback is in the low nibble of its idle status word.
    uint32_t status=GAME_STATUS_REG;
    if((status&0xC0u)!=0u){
        puts("FATAL: unexpected capture status while reading timer.");
        fatal_fpga_link();
    }
    return (status&1u)!=0u;
}
static int our_deadline_expired(void)
{
    if(!our_timed_input)return 0;
    if(fpga_timer_expired()){
        if(!turn_timeout_seen){
            puts("TIME EXPIRED (120 seconds). Unverified action is cancelled.");
            fflush(stdout);
        }
        turn_timeout_seen=1;
        return 1;
    }
    return 0;
}
static void start_our_turn_timer(void)
{
    fpga_timer_command(TIMER_START_OURS);
    our_timed_input=1;
    turn_timeout_seen=0;
    puts("120-second turn started (FPGA). Enter remains in ARM DS App Console.");
}
static void finish_our_turn_timer(void)
{
    if(our_timed_input){
        fpga_timer_command(TIMER_STOP);
        our_timed_input=0;
    }
}
// Atomically close the verification window before changing game state.
// Catch expiry that occurs during the STOP mailbox transaction itself.
static int finish_verified_action_before_deadline(void)
{
    uint32_t status=0;uint32_t i;
    if(our_deadline_expired())return 0;
    finish_our_turn_timer();
    for(i=0;i<500000u;i++){
        status=GAME_STATUS_REG;
        if((status&0xC0u)==0u && (status&2u)==0u)break;
        mailbox_poll_delay();
    }
    if(i==500000u){puts("FATAL: timer STOP not acknowledged by running bit.");fatal_fpga_link();}
    if((status&1u)!=0u){
        turn_timeout_seen=1;
        puts("TIME EXPIRED while closing verification. Move not committed.");
        return 0;
    }
    return 1;
}
static int fpga_set_cell(uint8_t coord,uint8_t card_class,uint8_t state)
{
    uint8_t hi=(uint8_t)(((state&3u)<<2)|((card_class>>4)&3u));unsigned a;
    for(a=0;a<4u;a++)if(fpga_command(CMD_SELECT_CELL,coord&15u)&&
                         fpga_command(CMD_CLASS_LOW,card_class&15u)&&
                         fpga_command(CMD_CELL_COMMIT,hi))return 1;
    return 0;
}

static int fpga_set_stats(void)
{
    uint8_t t=(game_round>255u)?255u:(uint8_t)game_round;unsigned a;
    /* own and opponent jokers (0..2 each) travel together as own + 3*opp (<= 8) */
    if(!fpga_command(CMD_SCORE,(uint8_t)(game_own_jokers_left+3u*game_opp_jokers_left)))return 0;
    for(a=0;a<4u;a++)if(fpga_command(CMD_SELECT_CELL,t&15u)&&fpga_command(CMD_TURN,t>>4))return 1;
    return 0;
}

static int fpga_set_ui_action(uint8_t action){return fpga_command(CMD_CONTROL,action&7u);}
static int fpga_guide_begin(void){return fpga_command(CMD_CONTROL,0xCu);}
static int fpga_guide_end(void){return fpga_command(CMD_CONTROL,0xEu);}
static int fpga_set_guide_cell(uint8_t cell,uint8_t state)
{
    unsigned a;for(a=0;a<4u;a++)if(fpga_command(CMD_SELECT_CELL,cell&15u)&&
                                    fpga_command(CMD_CONTROL,(uint8_t)(0x8u|(state&3u))))return 1;
    return 0;
}
static int fpga_set_draw_guide(uint8_t state)
{
    unsigned a;for(a=0;a<4u;a++)if(fpga_command(CMD_SELECT_CELL,state&3u)&&fpga_command(CMD_CONTROL,0xDu))return 1;
    return 0;
}
static int fpga_set_target(uint8_t cell)
{
    unsigned a;for(a=0;a<4u;a++)if(fpga_command(CMD_SELECT_CELL,cell&15u)&&fpga_command(CMD_CONTROL,0xFu))return 1;
    return 0;
}

/* V16.4: compact 16-bit movement arrow bitmap and target highlight on the
 * existing reliable mailbox. Opcode 0 uses D + 4 nibbles in idle mode.
 * C clears the animation; normal F-line text transfers remain unchanged. */
static int fpga_set_move_mask(uint16_t mask)
{
    return fpga_command(CMD_CAPTURE,0xDu) &&
        fpga_command(CMD_CAPTURE,(uint8_t)((mask>>12)&15u)) &&
        fpga_command(CMD_CAPTURE,(uint8_t)((mask>>8)&15u)) &&
        fpga_command(CMD_CAPTURE,(uint8_t)((mask>>4)&15u)) &&
        fpga_command(CMD_CAPTURE,(uint8_t)(mask&15u));
}
static void fpga_show_placement(uint16_t mask)
{
    if(!fpga_set_move_mask(mask)||!fpga_command(CMD_CAPTURE,0xBu))fatal_fpga_link();
}
static void fpga_clear_placement(void)
{
    if(!fpga_command(CMD_CAPTURE,0xCu))fatal_fpga_link();
}

static void print_card(uint8_t c)
{
    static const char suit_char[4]={'C','D','H','S'};
    uint8_t r=cnn_class_rank(c),s=cnn_class_suit(c);
    if(c==44){printf("JOKER B");return;}if(c==45){printf("JOKER R");return;}
    if(r==1)printf("A");else if(r==11)printf("J");else if(r==12)printf("Q");else if(r==13)printf("K");else printf("%u",r);
    if(s<4)printf("%c",suit_char[s]);
}
static char column_letter(unsigned col){static const char c[4]={'D','C','H','S'};return col<4?c[col]:'?';}
static void cell_text(unsigned cell,char out[4])
{
    if(cell>=16){out[0]='D';out[1]='R';out[2]='W';out[3]=0;return;}
    out[0]=column_letter(cell%4u);out[1]=(char)('1'+cell/4u);out[2]=0;
}

static int reset_fpga_state(void)
{
    unsigned attempt;int ok=0;
    for(attempt=0;attempt<4u&&!ok;attempt++)if(fpga_command(CMD_RESET_GAME,0xAu)&&fpga_command(CMD_RESET_GAME,0x5u))ok=1;
    return ok;
}

static void build_display_cell(unsigned i,uint8_t *cls,uint8_t *state)
{
    if(joker_mask&(1u<<i)) {*cls=joker_class[i];*state=DISP_JOKER;return;}
    if(pending_active&&(pending_mask&(1u<<i))) {
        *cls=pending_classes[i];
        /* Animate target AND shifted cards: compare proposed column against
         * committed board. The pulse runs in FPGA without further HPS scans. */
        *state=(i==pending_target || !(game_occupied_mask&(1u<<i)) ||
                game_board_class[i]!=pending_classes[i])?DISP_TARGET:DISP_NORMAL;
        return;
    }
    if(game_occupied_mask&(1u<<i)){*cls=game_board_class[i];*state=DISP_NORMAL;return;}
    *cls=0;*state=DISP_EMPTY;
}

static int fpga_sync_board(uint8_t action,uint8_t target)
{
    unsigned i;uint8_t cls,state;
    for(i=0;i<16;i++){build_display_cell(i,&cls,&state);if(!fpga_set_cell((uint8_t)i,cls,state))return 0;}
    if(!fpga_set_stats())return 0;
    if(!fpga_set_target(target&15u))return 0;
    if(!fpga_set_ui_action(action))return 0;
    return 1;
}

static void timeout_turn_cleanup(void)
{
    pending_active=0;
    game_round++;                 /* a timed-out turn still counts as a round */
    fpga_clear_placement();
    finish_our_turn_timer();
    if(!fpga_sync_board(UI_READY,0))fatal_fpga_link();
    hdmi_log("TIME EXPIRED / TURN PASSED");
    puts("TIME EXPIRED. Our turn was forfeited. Opponent is next.");
}

static void stop_hardware(void)
{
    puts("STOP: capture/upload/cache fault. Save this console log before reloading.");fflush(stdout);
    for(;;)__asm volatile("wfi");
}
static void take_snapshot(void){if(snapshot_frame()<0)stop_hardware();}

#include "gesture_core.h"

/* Hand-gesture input: a 3 s gesture replaces the typed line (PEACE = ENTER,
 * FIST = J, PALM = Q, see gesture_key). Typing H or flipping SW0 up switches to
 * gestures, flipping SW0 down switches back; this works at every prompt, also in
 * the middle of a game. The console read blocks the CPU, so after flipping SW0 up
 * the player presses ENTER once; that line only hands over and is not a command. */
static int read_action_line(char *first)
{
    char line[32];
    for(;;) {
        if(input_mode_sync())hdmi_refresh_instructions();
        if(gesture_input) {
            unsigned g=gesture_wait_command(our_deadline_expired);
            if(g==GESTURE_SWITCHED)continue;
            if(g==GESTURE_ABORT)return 0;
            if(g==GESTURE_ERROR){
                gesture_error_lock=1;
                puts("Hand gestures OFF after a camera/CNN error (flip SW0 down and up to retry); type the command.");
                continue;
            }
            *first=gesture_key(g);return !our_deadline_expired();
        }
        if(!fgets(line,sizeof(line),stdin))return 0;
        // The FPGA continued counting while the App Console was blocking.
        // On the first MANUAL Enter after expiry, C abandons the current step.
        if(our_deadline_expired())return 0;
        if(input_mode_sync()){hdmi_refresh_instructions();puts("SW0 flipped: that line only changed the input mode.");continue;}
        if(line[0]=='h'||line[0]=='H'){input_request_gestures();hdmi_refresh_instructions();continue;}
        *first=line[0];return 1;
    }
}
static int wait_enter_or_j_or_q(char *choice)
{
    for(;;){
        char c;if(!read_action_line(&c))return 0;
        if(c=='q'||c=='Q'){*choice='Q';return 1;}
        if(c=='j'||c=='J'){*choice='J';return 1;}
        if(c=='\n'||c=='\r'||c==0){*choice='E';return 1;}
        puts("Use ENTER, J, or Q.");
    }
}
static int wait_enter_or_q(void)
{
    for(;;){char c;if(!read_action_line(&c))return 0;if(c=='q'||c=='Q')return 0;if(c=='\n'||c=='\r'||c==0)return 1;puts("Press ENTER or Q.");}
}

/* Opponent turn: 1 = start our turn, 0 = back to the menu.
 *   keyboard: ENTER starts our turn (early, or after the time is up); there is no
 *             automatic switch, because the Arm DS console read blocks the CPU.
 *   gestures: PEACE starts our turn early; when the opponent's 120 s end our turn
 *             starts by itself; PALM = menu.
 * H or a SW0 flip changes the mode during the wait, as at every other prompt. */
static int wait_opponent_turn(void)
{
    for(;;) {
        if(input_mode_sync())hdmi_refresh_instructions();
        if(!gesture_input) {
            char line[32];
            if(!fgets(line,sizeof(line),stdin))return 0;
            if(input_mode_sync()){hdmi_refresh_instructions();continue;}   /* SW0 flipped: hand over */
            if(line[0]=='h'||line[0]=='H'){input_request_gestures();hdmi_refresh_instructions();continue;}
            if(line[0]=='q'||line[0]=='Q')return 0;
            if(line[0]=='\n'||line[0]=='\r'||line[0]==0)return 1;
            puts("Press ENTER (our turn) or Q (menu).");
            continue;
        }
        {
            unsigned g=gesture_wait_command(fpga_timer_expired);
            if(g==GESTURE_SWITCHED)continue;
            if(g==GESTURE_ABORT){
                puts("OPPONENT TIME EXPIRED: our turn starts automatically.");
                hdmi_log("OPP TIME UP / OUR TURN");return 1;
            }
            if(g==GESTURE_ERROR){
                gesture_error_lock=1;
                puts("Hand gestures OFF after a camera/CNN error; press ENTER when our turn starts.");
                continue;
            }
            if(gesture_key(g)=='\n')return 1;
            if(gesture_key(g)=='Q')return 0;
        }
    }
}

static void guide_apply_presence(const PresenceScan *p,uint16_t expected_mask,int expect_draw)
{
    unsigned i;
    for(i=0;i<16;i++) {
        uint8_t st;
        if(expected_mask&(1u<<i)) st=(p->status[i]==1)?GUIDE_GOOD:GUIDE_BAD;
        else st=(p->status[i]==0)?GUIDE_CLEAR:GUIDE_ALERT;
        if(manual_a_trace) {
            printf("A: HDMI update cell %u/17 ...\n",i+1u); fflush(stdout);
        }
        if(!fpga_set_guide_cell((uint8_t)i,st))fatal_fpga_link();
    }
    if(expect_draw>=0) {
        uint8_t st;
        if(expect_draw)st=(p->status[BOARD_DRAW_CELL]==1)?GUIDE_GOOD:
                          (p->status[BOARD_DRAW_CELL]==2?GUIDE_ALERT:GUIDE_BAD);
        else st=(p->status[BOARD_DRAW_CELL]==0)?GUIDE_CLEAR:GUIDE_ALERT;
        if(manual_a_trace) {puts("A: HDMI update DRAW (17/17) ...");fflush(stdout);}
        if(!fpga_set_draw_guide(st))fatal_fpga_link();
    }
}

/* Three-frame geometry vote. Good in >=2 frames = occupied. Any unstable or
 * multi-component result is ambiguous. No CNN work is done here. */
/* include_draw=0 is a board-only scan; the DRAW position is neither searched
 * nor included in the joker-scan result. Manual A uses the original full scan. */
static void presence_scan_mode(PresenceScan *out,int include_draw)
{
    unsigned good[BOARD_ACTIVE_CELLS]={0},amb[BOARD_ACTIVE_CELLS]={0};
    unsigned active_cells=include_draw?BOARD_ACTIVE_CELLS:BOARD_CELLS;
    unsigned f,i;
    memset(out,0,sizeof(*out));
    for(f=0;f<PRESENCE_FRAMES;f++) {
        int t;
        if(manual_a_trace) {printf("A: frame %u/%u capture START\n",f+1u,PRESENCE_FRAMES);fflush(stdout);}
        take_snapshot();
        if(manual_a_trace) {printf("A: frame %u/%u captured; grayscale/threshold START\n",f+1u,PRESENCE_FRAMES);fflush(stdout);}
        t=board_threshold();
        if(manual_a_trace) {printf("A: frame %u/%u threshold=%d; component scan START\n",f+1u,PRESENCE_FRAMES,t);fflush(stdout);}
        if(include_draw)(void)board_scan_frame(t);
        else (void)board_scan_board_only(t);
        if(manual_a_trace) {printf("A: frame %u/%u component scan DONE\n",f+1u,PRESENCE_FRAMES);fflush(stdout);}
        for(i=0;i<active_cells;i++) {
            if(board_detect_state[i]==BOARD_SLOT_GOOD)good[i]++;
            else if(board_detect_state[i]==BOARD_SLOT_AMBIG)amb[i]++;
        }
    }
    for(i=0;i<active_cells;i++) {
        if(good[i]>=2u&&amb[i]==0u)out->status[i]=1;
        else if(good[i]||amb[i])out->status[i]=2;
        else out->status[i]=0;
    }
}

static void presence_scan(PresenceScan *out)
{
    presence_scan_mode(out,1);
}

/* Crop one known cell from a NEW frame and optionally return RGB red score. */
static int fresh_cell_crop(unsigned cell,unsigned *red_score)
{
    int t;take_snapshot();t=board_threshold();
    if(cell<BOARD_CELLS)(void)board_scan_board_only(t);
    else (void)board_scan_frame(t);
    if(board_detect_state[cell]!=BOARD_SLOT_GOOD||!board_crop_cell(cell,t))return 0;
    /* DRAW: replace the 480x270 crop by a 2x-resolution, area-averaged crop */
    if(cell==BOARD_DRAW_CELL)(void)board_hires_refine(cell);
    if(!board_crop_exposure_ok(board_crops[cell]))return 0;
    if(red_score)*red_score=board_red_score(cell);
    board_slot_corners(cell);
    return 1;
}

static int recognize_joker_cell(unsigned cell,uint8_t *which_joker)
{
    unsigned total=0,attempts=0,joker_votes=0,redj=0,blackj=0;
    vision_verbose=0;
    while(total<JOKER_VOTES&&attempts<JOKER_MAX_ATTEMPTS) {
        uint8_t pred;attempts++;
        if(!fresh_cell_crop(cell,0))continue;
        if(!cnn_run(board_crops[cell],1,&pred))stop_hardware();
        total++;
        if(cnn_class_is_joker(pred)){joker_votes++;if(pred==45)redj++;else blackj++;}
    }
    vision_verbose=1;
    if(total<JOKER_VOTES||joker_votes<3u)return 0;
    *which_joker=(redj>blackj)?45u:44u;return 1;
}

/* Decoder after rank-first votes. Returns 1 = card decoded, 0 = not sure yet
 * (gather more votes / retry), -1 = rejected (fixed deck: rank not in deck).
 * fixed_deck_enable = 1: uses only configured, not-yet placed cards; RGB and
 *   CNN red/black evidence picks the deck card of that rank and colour.
 * fixed_deck_enable = 0: the CNN rank and suit majorities, no correction. */
static int decode_draw_evidence(const DrawEvidence *e,uint8_t *decoded)
{
    unsigned rank_votes[14]={0},i,best=1,bestv=0,second=0;
    uint8_t candidates[4];unsigned n;
    unsigned red=0,black=0,suit_votes[4]={0},cnn_red=0,cnn_black=0;
    if(e->count<5u)return 0;
    for(i=0;i<e->count;i++) {
        uint8_t r=game_vote_rank(e->rank[i]);
        if(r>=1u&&r<=13u)rank_votes[r]++;
    }
    for(i=1;i<=13;i++)if(rank_votes[i]>bestv){second=bestv;bestv=rank_votes[i];best=i;}else if(rank_votes[i]>second)second=rank_votes[i];
    if(bestv<((e->count/2u)+1u)||bestv==second)return 0;
    if(!fixed_deck_enable) {
        unsigned sv[4]={0},s,bs=0,b1=0,b2=0;
        for(i=0;i<e->count;i++)if(e->rank[i]==best&&(s=cnn_class_suit(e->raw_class[i]))<4u)sv[s]++;
        for(s=0;s<4u;s++)if(sv[s]>b1){b2=b1;b1=sv[s];bs=s;}else if(sv[s]>b2)b2=sv[s];
        if(b1*2u<=bestv||b1==b2)return 0;
        return class_from_rank_suit((uint8_t)best,(uint8_t)bs,decoded);
    }
    if(!game_rank_in_deck((uint8_t)best))return -1;
    n=game_remaining_candidates_for_rank((uint8_t)best,candidates);
    if(n==0u)return 0;
    if(n==1u){*decoded=candidates[0];return 1;}

    for(i=0;i<e->count;i++)if(game_vote_rank(e->rank[i])==best) {
        uint8_t s=cnn_class_suit(e->raw_class[i]);
        if(s<4u){suit_votes[s]++;if(suit_is_red(s))cnn_red++;else cnn_black++;}
        if(e->red_score[i]==BOARD_PIP_COLOUR_UNKNOWN)continue;
        if(e->red_score[i]>=RED_SCORE_RED)red++;
        else if(e->red_score[i]<=RED_SCORE_BLACK)black++;
    }
    /* V16.4.1 FIX: trusted CNN suit agreement must not be overwritten by
     * an uncalibrated red-pixel count. The old colour threshold wrongly
     * turned 5/5 QC votes into QH under tinted lighting.
     */
    {
        unsigned besti=0,bv=0,sv=0;
        int colour_known=(red>=3u||black>=3u);
        int want_red=red>black;
        for(i=0;i<n;i++) {
            unsigned v=suit_votes[cnn_class_suit(candidates[i])];
            if(v>bv){sv=bv;bv=v;besti=i;}
            else if(v>sv)sv=v;
        }
        /* Four of five, or seven of nine, legal predictions win directly. */
        if(bv>=4u && bv*4u>=e->count*3u && bv>sv) {
            *decoded=candidates[besti];return 1;
        }
        /* A weaker strict CNN majority is accepted only if RGB does not
         * contradict it. Otherwise gather more votes or return RETRY.
         * RGB alone never forces a different suit without CNN support.
         */
        if(bv*2u>e->count && bv>sv) {
            if(!colour_known || suit_is_red(cnn_class_suit(candidates[besti]))==want_red) {
                *decoded=candidates[besti];return 1;
            }
        }
    }
    /* Deck rule: the CNN often confuses suits of the same rank (2S read as 2C or
     * 2H), but the remaining legal cards of a rank usually differ in COLOUR (this
     * deck: A-4 in D and S, 8-J in C and H). Colour evidence = CNN suit colour
     * plus the corner-pip RGB colour. A clear colour majority (>= 3 votes and at
     * least twice the other colour) picks the only legal candidate of that colour. */
    {
        unsigned r=cnn_red+red,b=cnn_black+black,k,match=0,pick=0;
        if(r+b>=3u && (r>=2u*b || b>=2u*r) && r!=b) {
            int want=r>b;
            for(k=0;k<n;k++)if(suit_is_red(cnn_class_suit(candidates[k]))==want){match++;pick=k;}
            if(match==1u) {
                printf("DECK COLOUR RULE: %s evidence %u:%u -> ",want?"red":"black",want?r:b,want?b:r);
                print_card(candidates[pick]);printf("\n");
                *decoded=candidates[pick];return 1;
            }
        }
    }
    return 0;
}

static void short_card(uint8_t cls,char out[5]);
/* 1 = recognised, 0 = retry, -1 = rejected (rank not in the fixed deck). */
static int recognize_draw_card(uint8_t *decoded)
{
    DrawEvidence e;unsigned attempts=0;int d;memset(&e,0,sizeof(e));vision_verbose=0;hdmi_log("DRAW SCAN STARTED");
    while(e.count<DRAW_MAX_VOTES&&attempts<DRAW_MAX_ATTEMPTS) {
        uint8_t pred;unsigned red=0;attempts++;
        if(!fresh_cell_crop(BOARD_DRAW_CELL,&red)) {
            if(attempts==1u||attempts==5u)hdmi_log("DRAW BOUNDARY NOT FOUND");
            if(!fpga_set_draw_guide(GUIDE_BAD))fatal_fpga_link();
            continue;
        }
        if(!fpga_set_draw_guide(GUIDE_GOOD))fatal_fpga_link();
        if(!cnn_run(board_crops[BOARD_DRAW_CELL],1,&pred))stop_hardware();
        e.raw_class[e.count]=pred;e.rank[e.count]=cnn_class_rank(pred);e.red_score[e.count]=red;e.count++;
        if(e.rank[e.count-1u]==7u && game_vote_rank(7u)==8u)
            puts("DECK RANK RECOVERY: CNN 7 counted as game rank 8.");
        printf("DRAW vote %u: ",e.count);print_card(pred);if(red==BOARD_PIP_COLOUR_UNKNOWN)
            printf("; rank=%u; corner-pip colour=UNCERTAIN\n",(unsigned)e.rank[e.count-1]);
        else printf("; rank=%u; corner-pip red=%u/10000\n",(unsigned)e.rank[e.count-1],red);
        if(e.count==1u||e.count==3u||e.count==5u||e.count==9u){
            char summary[29];
            snprintf(summary,sizeof(summary),"DRAW SCAN %u/%u",e.count,DRAW_MAX_VOTES);
            hdmi_log(summary);
        }
        if(e.count==DRAW_MIN_VOTES&&decode_draw_evidence(&e,decoded)!=0)break;
    }
    vision_verbose=1;
    d=decode_draw_evidence(&e,decoded);
    if(d<0) {
        puts("DRAW REJECTED: the card's rank is not in the fixed deck (game_config.h).");
        hdmi_log("WRONG RANK: NOT IN DECK");
        return -1;
    }
    if(d==0) {
        puts("DRAW RECOGNITION RETRY: constrained deck decoder could not get a unique legal card.");hdmi_log("DRAW SCAN: RETRY NEEDED");
        return 0;
    }
    printf("CONSTRAINED RESULT: ");print_card(*decoded);printf(" using %u fresh CNN samples.\n",e.count);hdmi_log("DRAW SCAN COMPLETE");
    return 1;
}

/* Three fresh observations per expected cell. A stable incorrect rank is
 * reported as WRONG CARD rather than being merged with missing/uncertain.
 * The CNN is still rank-verified only; the physical suit column fixes suit.
 */
typedef enum { VERIFY_OK=0, VERIFY_MISSING, VERIFY_WRONG_CARD,
               VERIFY_UNCERTAIN, VERIFY_EXTRA, VERIFY_DRAW_OCCUPIED } VerifyIssue;
typedef struct {
    VerifyIssue issue;
    unsigned cell;
    unsigned expected_rank;
    unsigned seen_rank;  /* set only when the same wrong rank wins >=2 frames */
    unsigned draw_observed;
} VerifyReport;

static int verify_column(unsigned col,const uint8_t expected[16],uint16_t expected_mask,
                         VerifyReport *report)
{
    unsigned votes[4][14]={ {0} },presence[4]={0},empty[4]={0};
    unsigned draw_seen=0,draw_ambiguous=0,f,r;
    report->issue=VERIFY_OK;report->cell=0;report->expected_rank=0;
    report->seen_rank=0;report->draw_observed=0;
    vision_verbose=0;
    for(f=0;f<VERIFY_FRAMES;f++) {
        int t;
        printf("VERIFY: frame %u/%u capture and detect...\n",f+1u,VERIFY_FRAMES);fflush(stdout);
        take_snapshot();t=board_threshold();(void)board_scan_frame(t);
        if(board_detect_state[BOARD_DRAW_CELL]==BOARD_SLOT_GOOD)draw_seen++;
        else if(board_detect_state[BOARD_DRAW_CELL]==BOARD_SLOT_AMBIG)draw_ambiguous++;
        for(r=0;r<4;r++) {
            unsigned cell=r*4u+col;
            if(expected_mask&(1u<<cell)) {
                uint8_t pred;
                if(board_detect_state[cell]==BOARD_SLOT_GOOD &&
                   board_crop_cell(cell,t) && board_crop_exposure_ok(board_crops[cell])) {
                    unsigned rank;
                    presence[r]++;board_slot_corners(cell);
                    if(!cnn_run(board_crops[cell],1,&pred))stop_hardware();
                    rank=game_vote_rank(cnn_class_rank(pred));
                    printf("VERIFY %c%u: CNN class=%u rank=%u (expected rank=%u)\n",
                           column_letter(col),r+1u,(unsigned)pred,rank,
                           (unsigned)cnn_class_rank(expected[cell]));
                    if(rank<=13u)votes[r][rank]++;
                }
            } else if(board_detect_state[cell]==BOARD_SLOT_BAD)empty[r]++;
        }
        printf("VERIFY: frame %u/%u checked.\n",f+1u,VERIFY_FRAMES);fflush(stdout);
    }
    vision_verbose=1;
    report->draw_observed=(draw_seen>=2u || draw_ambiguous>=2u);
    for(r=0;r<4;r++) {
        unsigned cell=r*4u+col;int ok=0;
        VerifyIssue issue=VERIFY_OK;
        unsigned observed=0;
        if(expected_mask&(1u<<cell)) {
            unsigned expected_rank=cnn_class_rank(expected[cell]);
            if(presence[r]>=2u && votes[r][expected_rank]>=2u)ok=1;
            else if(presence[r]<2u)issue=VERIFY_MISSING;
            else {
                observed=game_wrong_rank(votes[r],expected_rank);
                issue=observed?VERIFY_WRONG_CARD:VERIFY_UNCERTAIN;
            }
            if(!ok && report->issue==VERIFY_OK) {
                report->issue=issue;report->cell=cell;
                report->expected_rank=expected_rank;report->seen_rank=observed;
            }
        } else {
            ok=(empty[r]>=2u);
            if(!ok && report->issue==VERIFY_OK){report->issue=VERIFY_EXTRA;report->cell=cell;}
        }
        if(!fpga_set_guide_cell((uint8_t)cell,ok?GUIDE_GOOD:GUIDE_BAD))fatal_fpga_link();
    }
    if(!fpga_set_draw_guide(report->draw_observed?GUIDE_BAD:GUIDE_CLEAR))fatal_fpga_link();
    if(report->issue==VERIFY_OK && report->draw_observed)report->issue=VERIFY_DRAW_OCCUPIED;
    return report->issue==VERIFY_OK;
}

static void short_card(uint8_t cls,char out[5])
{
    unsigned rank=cnn_class_rank(cls);char suit=column_letter((unsigned)game_column_for_suit(cnn_class_suit(cls)));
    if(cnn_class_is_joker(cls)){strcpy(out,"JKR");return;}
    if(rank==1)snprintf(out,5,"A%c",suit);
    else if(rank==11)snprintf(out,5,"J%c",suit);
    else if(rank==12)snprintf(out,5,"Q%c",suit);
    else if(rank==13)snprintf(out,5,"K%c",suit);
    else snprintf(out,5,"%u%c",rank,suit);
}
/* HPS derives exact bottom-to-top moves, never a vague SHIFT message.
 * Bottom-up is safe for the real player: it avoids overwriting a card. */
static void print_insertion_moves(const uint8_t proposed[16],uint16_t pmask,uint8_t new_class,uint8_t target)
{
    unsigned col=target%4u,r,newcell;
    unsigned next_line=2u;
    uint16_t move_sources=0;
    char cell[4],card[5],buf[29],screen[5][29]={{0}};
    cell_text(target,cell);short_card(new_class,card);
    printf("RECOGNISED: ");print_card(new_class);printf(" -> place at %s\n",cell);
    /* The first line is also the turn indicator, always large on HDMI.
     * Every move is listed bottom-to-top to avoid moving onto occupied cells. */
    strcpy(screen[0],"YOUR TURN");
    snprintf(screen[1],29,"STEP 3: PLACE %s AT %s",card,cell);
    for(r=4;r>0;r--) {
        unsigned oldcell=(r-1u)*4u+col;
        if(!(game_occupied_mask&(1u<<oldcell)))continue;
        for(newcell=0;newcell<4;newcell++) {
            unsigned dest=newcell*4u+col;
            char src[4],dst[4],moved[5];
            if(!(pmask&(1u<<dest))||proposed[dest]!=game_board_class[oldcell]||dest==oldcell)continue;
            cell_text(oldcell,src);cell_text(dest,dst);short_card(game_board_class[oldcell],moved);
            snprintf(buf,sizeof(buf),"MOVE %s %s > %s",moved,src,dst);
            printf("  %s\n",buf);
            move_sources|=(uint16_t)(1u<<oldcell);
            if(next_line<5u)strcpy(screen[next_line++],buf);
            break;
        }
    }
    printf("  PLACE %s AT %s AFTER MOVES\n",card,cell);
    /* With three shifts all five rows are occupied; the placement row
     * includes the ENTER instruction rather than discarding a move. */
    if(next_line==5u)snprintf(screen[1],29,"PLACE %s %s; ENTER: CHECK",card,cell);
    else strcpy(screen[4],"ENTER: CHECK PLACEMENT");
    fpga_show_placement(move_sources);
    puts("HDMI STEP3: sending exact instructions...");fflush(stdout);
    hdmi_instructions(screen[0],screen[1],screen[2],screen[3],screen[4]);
    puts("HDMI STEP3: instructions sent.");fflush(stdout);
}

/* Scan board geometry and classify only unexpected occupied cells. This is the
 * fast round-start joker strategy: expected normal cards are NOT all CNN-run. */
static int refresh_board_and_jokers(void)
{
    PresenceScan p;unsigned i;uint16_t new_jokers=0;uint8_t new_class[16]={0};int geometry_ok=1;
    hdmi_log("SCAN BOARD / SEEK JOKERS");
    /* DRAW is intentionally excluded: it can still contain the previous card.
     * Do not overwrite the DRAW overlay status or block joker scanning. */
    presence_scan_mode(&p,0);guide_apply_presence(&p,game_occupied_mask,-1);
    for(i=0;i<16;i++) {
        if(game_occupied_mask&(1u<<i)) {
            if(p.status[i]!=1){char c[4];cell_text(i,c);printf("BOARD CHECK: expected normal card missing/ambiguous at %s.\n",c);geometry_ok=0;}
        }
    }
    if(!geometry_ok){joker_mask=0;if(!fpga_sync_board(UI_FIX_BOARD,0))fatal_fpga_link();return 0;}

    for(i=0;i<16;i++)if(!(game_occupied_mask&(1u<<i))&&p.status[i]==2) {
        char c[4];cell_text(i,c);printf("BOARD CHECK: unstable/ambiguous object at empty %s.\n",c);geometry_ok=0;
        if(!fpga_set_guide_cell((uint8_t)i,GUIDE_BAD))fatal_fpga_link();
    }
    if(!geometry_ok){joker_mask=0;if(!fpga_sync_board(UI_FIX_BOARD,0))fatal_fpga_link();return 0;}
    for(i=0;i<16;i++)if(!(game_occupied_mask&(1u<<i))&&p.status[i]==1) {
        uint8_t jc;char c[4];cell_text(i,c);printf("Unexpected card at empty %s: checking for opponent joker...\n",c);
        if(!recognize_joker_cell(i,&jc)) {
            printf("BOARD CHECK: card at %s is not a stable joker. Correct the board.\n",c);geometry_ok=0;
            if(!fpga_set_guide_cell((uint8_t)i,GUIDE_BAD))fatal_fpga_link();
        } else {
            new_jokers|=(uint16_t)(1u<<i);new_class[i]=jc;
            if(!fpga_set_guide_cell((uint8_t)i,GUIDE_ALERT))fatal_fpga_link();
            printf("Opponent joker confirmed at %s.\n",c);hdmi_log("OPPONENT JOKER CONFIRMED");
        }
    }
    if(!geometry_ok){joker_mask=0;if(!fpga_sync_board(UI_FIX_BOARD,0))fatal_fpga_link();return 0;}
    {   /* a joker not seen before = one more opponent joker played */
        uint16_t fresh=(uint16_t)(new_jokers&~opp_joker_seen);
        for(i=0;i<16;i++)if(fresh&(1u<<i)&&game_opp_jokers_left)game_opp_jokers_left--;
        opp_joker_seen|=new_jokers;
        if(fresh)printf("Opponent jokers left: %u\n",(unsigned)game_opp_jokers_left);
    }
    joker_mask=new_jokers;memcpy(joker_class,new_class,sizeof(joker_class));
    if(!new_jokers)hdmi_log("SCAN OK / NO OPP JOKER");
    return 1;
}

static int verify_joker_removed(unsigned cell)
{
    PresenceScan p;unsigned i;
    presence_scan_mode(&p,0);guide_apply_presence(&p,game_occupied_mask,-1);
    if(p.status[cell]!=0)return 0;
    for(i=0;i<16;i++)if(game_occupied_mask&(1u<<i))if(p.status[i]!=1)return 0;
    return 1;
}

static int use_own_joker_turn(void)
{
    if(game_own_jokers_left==0u){
        puts("No more own jokers remain.");
        hdmi_instructions("YOUR TURN","NO OWN JOKERS LEFT", "REMOVE JOKER OR DRAW", "", "");
        hdmi_log("NO OWN JOKERS LEFT");return 0;
    }
    if(!fpga_sync_board(UI_USE_JOKER,0))fatal_fpga_link();
    hdmi_instructions("YOUR TURN", "STEP: USE YOUR JOKER", "PLACE ON OPP EMPTY CELL", "ENTER: FINISH TURN", "Q: CANCEL");
    hdmi_log("JOKER USE TURN");
    puts("JOKER TURN: place one of your jokers in ANY EMPTY cell on the opponent's board. Press ENTER when done; Q cancels before committing.");
    if(!wait_enter_or_q() || !finish_verified_action_before_deadline())return 0;
    game_own_jokers_left--;game_round++;
    if(!fpga_sync_board(UI_READY,0))fatal_fpga_link();
    hdmi_log(game_own_jokers_left?"OWN JOKER USED":"NO OWN JOKERS LEFT");
    return 1;
}

static int handle_incoming_joker(void)
{
    unsigned cell;char choice,c[4];
    cell=game_choose_joker(joker_mask);
    if(cell>=16)return 0;
    cell_text(cell,c);
    if(!fpga_set_target((uint8_t)cell)||!fpga_sync_board(UI_REMOVE_JOKER,(uint8_t)cell))fatal_fpga_link();
    printf("SYSTEM DECISION: remove opponent joker at %s. ENTER after removal, or J to spend this turn using your own joker instead.\n",c);
    {char buf[29];snprintf(buf,sizeof(buf),"REMOVE OPP JOKER AT %s",c);
     hdmi_instructions("YOUR TURN",buf,"REMOVE ONE JOKER", "ENTER: CHECK REMOVAL",
                       game_own_jokers_left?"J: USE OWN JOKER INSTEAD":"NO OWN JOKERS LEFT");}
    hdmi_log("JOKER FOUND / REMOVE");
    for(;;) {
        if(!wait_enter_or_j_or_q(&choice)||choice=='Q')return -1;
        if(choice=='J') {
            if(game_own_jokers_left==0u){
                puts("No more own jokers: remove the selected opponent joker.");
                hdmi_instructions("YOUR TURN","NO OWN JOKERS LEFT","REMOVE DETECTED JOKER", "ENTER: CHECK REMOVAL", "");
                hdmi_log("NO OWN JOKERS LEFT");continue;
            }
            return use_own_joker_turn()?1:-1;
        }
        if(verify_joker_removed(cell)) {
            if(!finish_verified_action_before_deadline())return -1;
            joker_mask&=(uint16_t)~(1u<<cell);joker_class[cell]=0;game_round++;
            opp_joker_seen&=(uint16_t)~(1u<<cell);
            if(!fpga_sync_board(UI_READY,0))fatal_fpga_link();
            puts("Joker removal verified. Round complete.");hdmi_log("JOKER REMOVAL VERIFIED");return 1;
        }
        puts("Joker still present or cell ambiguous. Remove the highlighted joker and press ENTER again.");
        hdmi_instructions("YOUR TURN","JOKER STILL PRESENT","REMOVE HIGHLIGHTED JOKER","ENTER: CHECK AGAIN",
                          game_own_jokers_left?"J: USE YOUR JOKER":"NO OWN JOKERS LEFT");
        hdmi_log("JOKER STILL PRESENT");
        if(!fpga_sync_board(UI_FIX_BOARD,(uint8_t)cell))fatal_fpga_link();
    }
}

/* After "draw a card" is chosen: keep scanning DRAW until a card has been seen in
 * DRAW_SETTLE_FRAMES snapshots in a row (the hand has let go), reminding the player
 * on HDMI while it is empty. need_empty_first: wait for DRAW to be emptied first
 * (after an invalid/used card). Returns 0 if our turn time runs out. */
#define DRAW_SETTLE_FRAMES 5u
static int wait_for_draw_card(int need_empty_first)
{
    unsigned seen=0,empty=0,reminded=0;
    vision_verbose=0;
    for(;;) {
        int t;
        if(our_deadline_expired()){vision_verbose=1;return 0;}
        take_snapshot();t=board_threshold();(void)board_scan_frame(t);
        if(board_detect_state[BOARD_DRAW_CELL]==BOARD_SLOT_GOOD) {
            empty=0;
            if(need_empty_first)continue;
            if(++seen>=DRAW_SETTLE_FRAMES){vision_verbose=1;return 1;}
        } else {
            seen=0;
            if(need_empty_first && ++empty>=3u){need_empty_first=0;reminded=0;}
            if(!reminded && !need_empty_first) {
                if(!fpga_set_draw_guide(GUIDE_BAD))fatal_fpga_link();
                hdmi_instructions("YOUR TURN","STEP 2: DRAW A CARD","PUT CARD IN DRAW SLOT","SCANNING DRAW SLOT...","");
                hdmi_log("WAITING FOR DRAW CARD");
                puts("DRAW is empty: put the drawn card in DRAW (scanning automatically).");
                reminded=1;
            }
        }
    }
}

/* Why game_build_insert refused a recognised DRAW card (HDMI-sized text). */
static const char *draw_reject_reason(uint8_t cls)
{
    int col=game_column_for_suit(cnn_class_suit(cls));
    if(game_card_already_placed(cls))return "ALREADY ON BOARD";
    if(col>=0&&game_column_count((unsigned)col)>=4u)return "COLUMN FULL";
    if(fixed_deck_enable)return "NOT IN DECK";
    return "NOT A NORMAL CARD";
}

static int normal_draw_turn(void)
{
    int need_empty=0,rec;
    uint8_t cls,target;uint16_t pmask;char c[4];VerifyReport report;
    char last_reject[29]="";   /* shown while DRAW is scanned again after a rejected card */
    for(;;) {
        if(!fpga_set_draw_guide(GUIDE_CLEAR)||!fpga_sync_board(UI_PLACE_DRAW,0))fatal_fpga_link();
        hdmi_instructions("YOUR TURN", "STEP 2: DRAW A CARD", "PUT CARD IN DRAW SLOT", "SCANNING DRAW SLOT...", last_reject);
        last_reject[0]=0;
        hdmi_log("WAITING FOR DRAW CARD");
        puts("Put the drawn card in DRAW: it is recognised automatically (no command needed).");
        if(!wait_for_draw_card(need_empty))return 0;
        need_empty=0;
        hdmi_instructions("YOUR TURN", "STEP 2: DRAW A CARD", "CARD FOUND IN DRAW", "RECOGNISING...", "");
        rec=recognize_draw_card(&cls);
        if(rec<0) {
            if(our_deadline_expired())return 0;
            if(!fpga_sync_board(UI_RETRY,0))fatal_fpga_link();
            puts("Wrong-rank card (not in the deck): scanning DRAW again; swap the card if it really is that rank.");
            hdmi_log("WRONG RANK: SCANNING AGAIN");
            strcpy(last_reject,"LAST: RANK NOT IN DECK");
            continue;
        }
        if(!rec) {
            if(our_deadline_expired())return 0;
            if(!fpga_sync_board(UI_RETRY,0))fatal_fpga_link();
            hdmi_instructions("YOUR TURN", "CARD NOT RECOGNISED", "ADJUST THE DRAW CARD", "RETRYING AUTOMATICALLY", "");
            hdmi_log("DRAW RECOGNITION RETRY");
            puts("Card not recognised: adjust the card in DRAW; retrying automatically.");continue;
        }
        if(our_deadline_expired())return 0;
        if(!game_build_insert(cls,pending_classes,&pmask,&target)) {
            const char *why=draw_reject_reason(cls);char card[5];
            short_card(cls,card);
            printf("Recognised card refused: ");print_card(cls);
            printf(" - %s. Scanning DRAW again (adjust or swap the card if needed).\n",why);
            hdmi_log("CARD REFUSED: SCANNING AGAIN");
            snprintf(last_reject,sizeof(last_reject),"LAST: %s %s",card,why);
            if(!fpga_sync_board(UI_RETRY,0))fatal_fpga_link();
            continue;
        }
        pending_mask=pmask;pending_new_class=cls;pending_target=target;pending_active=1;
        cell_text(target,c);printf("RECOGNISED: ");print_card(cls);printf(" -> target %s\n",c);
        print_insertion_moves(pending_classes,pending_mask,cls,target);
        puts("HDMI STEP3: updating board artwork...");fflush(stdout);
        if(!fpga_sync_board(UI_PLACE_CARD,target))fatal_fpga_link();
        hdmi_log("TARGET / MOVES SHOWN");
        puts("HDMI STEP3: ready. Waiting for ENTER to verify.");fflush(stdout);
        puts("Follow the named source-to-destination moves on HDMI, place the new card at its purple target, and press ENTER to check.");
        for(;;) {
            if(!wait_enter_or_q()){pending_active=0;fpga_clear_placement();return 0;}
            hdmi_log("VERIFYING CARD COLUMN");
            puts("PLACEMENT: ENTER received, verifying column...");fflush(stdout);
            if(verify_column(target%4u,pending_classes,pending_mask,&report)) {
                if(!finish_verified_action_before_deadline()){
                    pending_active=0;fpga_clear_placement();return 0;
                }
                if(!game_commit_insert(cls,pending_classes,pending_mask))fatal_fpga_link();
                pending_active=0;fpga_clear_placement();game_round++;
                printf("PLACEMENT VERIFIED. %u/16 normal cards complete.\n",game_placed_count());
                hdmi_log("COLUMN VERIFIED / PASS");
                if(game_placed_count()==16u) {
                    if(!fpga_sync_board(UI_COMPLETE,target))fatal_fpga_link();
                    puts("COMPLETE: all 16 configured cards are correctly placed.");
                    hdmi_instructions("FINISHED", "ALL 16 CARDS PLACED", "GAME COMPLETE", "", "");
                    hdmi_log("FINISHED 16 OF 16");return 2;
                }
                if(!fpga_sync_board(UI_READY,0))fatal_fpga_link();
                return 1;
            }
            {
                char loc[4],detail[29],expected[5],seen[5];
                cell_text(report.cell,loc);
                if(report.issue==VERIFY_WRONG_CARD) {
                    uint8_t observed_class;
                    short_card(pending_classes[report.cell],expected);
                    if(class_from_rank_suit((uint8_t)report.seen_rank,
                                            game_column_suit[report.cell%4u],&observed_class))
                        short_card(observed_class,seen);
                    else strcpy(seen,"??");
                    snprintf(detail,sizeof(detail),"EXPECT %s / RANK %u SEEN",expected,report.seen_rank);
                    printf("WRONG CARD %s: expected %s, observed rank %u (suit not independently verified).\n",
                           loc,expected,report.seen_rank);
                    {char title[29];snprintf(title,sizeof(title),"WRONG CARD AT %s",loc);
                     hdmi_instructions("YOUR TURN",title,detail,"REPLACE WITH CORRECT CARD",
                                       report.draw_observed?"DRAW HAS CARD / ENTER AGAIN":"ENTER: CHECK AGAIN");}
                } else if(report.issue==VERIFY_DRAW_OCCUPIED) {
                    puts("DRAW has a detected card/object. Remove it, then recheck.");
                    hdmi_instructions("YOUR TURN","CARD DETECTED IN DRAW","REMOVE DRAW CARD","ENTER: CHECK AGAIN","");
                } else {
                    char title[29];
                    snprintf(title,sizeof(title),"CHECK CARD AT %s",loc);
                    if(report.issue==VERIFY_MISSING)strcpy(detail,"EXPECTED CARD NOT FOUND");
                    else if(report.issue==VERIFY_EXTRA)strcpy(detail,"UNEXPECTED CARD IN CELL");
                    else strcpy(detail,"CARD ID NOT CONFIRMED");
                    hdmi_instructions("YOUR TURN",title,detail,"CORRECT BOARD / RETRY",
                                      report.draw_observed?"DRAW HAS CARD / ENTER AGAIN":"ENTER: CHECK AGAIN");
                    printf("PLACEMENT: %s at %s.\n",detail,loc);
                }
            }
            hdmi_log("PLACEMENT CHECK: CORRECT IT");
            if(!fpga_sync_board(UI_FIX_BOARD,target))fatal_fpga_link();
        }
    }
}

static void game_link_start(void)
{
    uint8_t ack,error;uint32_t tick;
    mailbox_startup_resync();tick=UREG(15);
    while(GAME_STATUS_REG&0x80u)if((uint32_t)(UREG(15)-tick)>400000000u)fatal_fpga_link();
    if(!status_get_ack(GAME_STATUS_REG,&ack,&error)){puts("Old result hides mailbox ACK. Reload matching V16 SOF/AXF.");fatal_fpga_link();}
    fpga_ack_state=ack;fpga_ack_known=1;
    if(!reset_fpga_state())fatal_fpga_link();
    if(!fpga_guide_begin())fatal_fpga_link();
    hdmi_reset_shadow();
}

/* '1' = we start, '2' = opponent starts; all confirmation still through Arm DS. */
static int choose_first_team(int *opponent_first)
{
    char c;
    /* one text for both input modes, so it stays right if SW0 is flipped here */
    hdmi_instructions("WHO STARTS?", "1 OR PEACE: OUR TEAM", "2 OR FIST: OPPONENT", "TYPE 1/2 OR HOLD 3 S", "Q: CANCEL GAME");
    hdmi_log("WAITING FOR STARTING TEAM");
    puts("Who goes first? Type 1 + ENTER for OUR TEAM, 2 + ENTER for OPPONENT, Q to cancel.");
    for(;;) {
        if(!read_action_line(&c))return 0;
        if(gesture_input)c=c=='\n'?'1':c=='J'?'2':c;
        if(c=='1') {*opponent_first=0;return 1;}
        if(c=='2') {*opponent_first=1;return 1;}
        if(c=='q'||c=='Q')return 0;
        puts("Enter 1, 2 or Q.");
    }
}

/* A game left with Q / PALM stays in memory and can be continued from the menu;
 * it is cleared when a new game starts or when all 16 cards are placed. */
static int game_paused=0;
static int wait_opponent=0;      /* whose turn comes next; kept for continuing */

/* resume=0: new game. resume=1: continue the game that was left (same board,
 * jokers, round and turn order); the turn that was interrupted starts again. */
static void game_main(int resume)
{
    char choice;int our_timer_started=0;
    if(!game_config_validate()){puts("FATAL: invalid deck configuration.");return;}
    if(!resume){game_reset_state();joker_mask=0;opp_joker_seen=0;memset(joker_class,0,sizeof(joker_class));}
    pending_active=0;
    our_timed_input=0;turn_timeout_seen=0;
    game_link_start();
    fpga_timer_command(TIMER_CLEAR);
    if(!fpga_sync_board(UI_READY,0))fatal_fpga_link();
    if(!resume){
        game_paused=0;wait_opponent=0;
        if(!choose_first_team(&wait_opponent))return;
    } else {
        printf("CONTINUE: previous game, round %u, %u/16 cards placed.\n",game_round,game_placed_count());
        hdmi_log("PREVIOUS GAME CONTINUED");
    }
    game_paused=1;                /* until all 16 cards are placed */
    puts("SUIT STACK RACE: no back learning; columns D/C/H/S sorted top-to-bottom.");
    for(;;) {
        int r;
        printf("\nROUND %u | placed=%u/16 | own jokers=%u\n",game_round,game_placed_count(),game_own_jokers_left);
        if(wait_opponent) {
            // FPGA counts the opponent's 120 seconds. Keyboard: ENTER starts our turn.
            // Gestures: PEACE starts it, or it starts by itself when the time is up.
            fpga_timer_command(TIMER_START_OPPONENT);
            if(!fpga_sync_board(UI_READY,0))fatal_fpga_link();
            /* ENTER/Q read PEACE/PALM in gesture mode; the auto start is gesture-only */
            hdmi_instructions("OPPONENT TURN", "WAIT FOR OPPONENT", "ENTER: START OUR TURN",
                              "", "Q: MAIN MENU");
            hdmi_log("OPPONENT TURN / WAITING");
            puts(gesture_input?"OPPONENT TURN. PEACE starts our turn; it also starts by itself when the 120 s end. PALM = menu."
                              :"OPPONENT TURN. Press ENTER to start our turn (also after the time is up). Q = menu.");
            if(!wait_opponent_turn()){fpga_timer_command(TIMER_CLEAR);return;}
            if(fpga_timer_expired())puts("OPPONENT TIME EXPIRED: switching to our turn.");
            fpga_timer_command(TIMER_STOP);
            wait_opponent=0;
            our_timer_started=0;
        }
        // Our 120 s start the moment our turn starts and run, without pausing,
        // through joker scan, draw, recognition, placement and verification.
        if(!our_timer_started){start_our_turn_timer();our_timer_started=1;}
        /* Once the opponent has played all jokers and none is on our board, there
         * is nothing to look for: skip the joker scan. */
        if(game_opp_jokers_left==0u && joker_mask==0u)
            puts("OUR TURN: opponent has no jokers left; joker scan skipped.");
        else {
        hdmi_instructions("YOUR TURN", "SCANNING FOR JOKERS", "CHECKING FOR JOKERS", "", "");
        hdmi_log("OWN ROUND / JOKER SCAN");
        puts("OUR TURN: checking grid for incoming jokers...");fflush(stdout);
        if(!refresh_board_and_jokers()) {
            if(our_deadline_expired()){
                timeout_turn_cleanup();our_timer_started=0;wait_opponent=1;continue;
            }
            puts("Correct the detected board issue; ENTER to rescan, Q to menu.");
            hdmi_instructions("YOUR TURN", "BOARD SCAN FAILED", "CHECK MARKED CELLS", "ENTER: SCAN AGAIN", "Q: MAIN MENU");
            if(!wait_enter_or_q()){
                if(turn_timeout_seen){timeout_turn_cleanup();our_timer_started=0;wait_opponent=1;continue;}
                return;
            }
            continue;
        }
        }
        if(our_deadline_expired()){
            timeout_turn_cleanup();our_timer_started=0;wait_opponent=1;continue;
        }
        if(joker_mask) {
            r=handle_incoming_joker();
            if(turn_timeout_seen || our_deadline_expired()){
                timeout_turn_cleanup();our_timer_started=0;wait_opponent=1;continue;
            }
            if(r<0)return;
            if(r>0){finish_our_turn_timer();our_timer_started=0;wait_opponent=1;}
            continue;
        }
        if(game_placed_count()==16u){
            finish_our_turn_timer();if(!fpga_sync_board(UI_COMPLETE,0))fatal_fpga_link();game_paused=0;return;
        }
        if(!fpga_sync_board(UI_READY,0))fatal_fpga_link();
        if(game_own_jokers_left)
            hdmi_instructions("YOUR TURN","CHOOSE YOUR ACTION","ENTER: DRAW A CARD","J: PLACE YOUR JOKER","Q: MAIN MENU");
        else
            hdmi_instructions("YOUR TURN","CHOOSE YOUR ACTION","ENTER: DRAW A CARD","NO OWN JOKERS LEFT","Q: MAIN MENU");
        puts("ENTER = draw normal card, J + ENTER = use joker, Q = menu.");
        if(!wait_enter_or_j_or_q(&choice)){
            if(turn_timeout_seen){timeout_turn_cleanup();our_timer_started=0;wait_opponent=1;continue;}
            return;
        }
        if(choice=='Q')return;
        if(our_deadline_expired()){
            timeout_turn_cleanup();our_timer_started=0;wait_opponent=1;continue;
        }
        if(choice=='J') {
            r=use_own_joker_turn();
            if(turn_timeout_seen || our_deadline_expired()){
                timeout_turn_cleanup();our_timer_started=0;wait_opponent=1;continue;
            }
            if(r){finish_our_turn_timer();our_timer_started=0;wait_opponent=1;}
            continue;
        }
        r=normal_draw_turn();
        if(turn_timeout_seen || our_deadline_expired()){
            timeout_turn_cleanup();our_timer_started=0;wait_opponent=1;continue;
        }
        if(r==0)return;
        finish_our_turn_timer();our_timer_started=0;
        if(r==2){game_paused=0;return;}
        wait_opponent=1;
    }
}

static void one_shot_status(void)
{
    PresenceScan p;unsigned i;
    manual_a_trace=1;
    puts("A: command received; guide already enabled; starting 3-frame geometry scan.");fflush(stdout);hdmi_log("A / 3-FRAME STATUS SCAN");
    presence_scan(&p);
    puts("A: all 3 frames scanned; updating the 16 HDMI board cells + DRAW.");fflush(stdout);
    guide_apply_presence(&p,game_occupied_mask,0);
    manual_a_trace=0;
    puts("A: HDMI status refresh finished.");fflush(stdout);hdmi_log("A STATUS SCAN COMPLETE");
    puts("A status refresh: GREEN=expected occupied and detected; CYAN=expected empty; RED=missing expected card; AMBER=unexpected/ambiguous.");
    for(i=0;i<16;i++){char c[4];cell_text(i,c);printf("%s: %s\n",c,p.status[i]==1?"CARD":p.status[i]==2?"AMBIGUOUS":"EMPTY");}
    printf("DRAW: %s\n",p.status[BOARD_DRAW_CELL]==1?"CARD":p.status[BOARD_DRAW_CELL]==2?"AMBIGUOUS":"EMPTY");
}

/* After a game was left with Q / PALM, "scan camera" becomes "continue game";
 * in gesture mode PEACE (the usual ENTER gesture) then continues, so a habit
 * gesture never throws the game away. */
static void show_main_menu(void)
{
    if(gesture_input) {
        if(game_paused)hdmi_instructions("SYSTEM MENU","PEACE: CONTINUE GAME","FIST: NEW GAME","HOLD GESTURE FOR 3 S","PALM: KEYBOARD INPUT");
        else hdmi_instructions("SYSTEM MENU","PEACE: NEW GAME","FIST: SCAN CAMERA","HOLD GESTURE FOR 3 S","PALM: KEYBOARD INPUT");
    } else {
        if(game_paused)hdmi_instructions("SYSTEM MENU","G: NEW GAME","A: CONTINUE GAME","H: GESTURES  C: CAPTURE","Q: STOP");
        else hdmi_instructions("SYSTEM MENU","G: NEW GAME","A: SCAN CAMERA","H: GESTURES  C: CAPTURE","Q: STOP");
    }
}

int main(void)
{
    char line[32];
    puts("V16.4.3 SUIT STACK RACE: board-only joker scan + 7-to-8 recovery; keep V16.4.2 SOF");
    puts(BOARD_LANDSCAPE_ROTATION==1?
         "Horizontal cards: card TOP points LEFT in camera (clockwise crop correction).":
         "Horizontal cards: card TOP points RIGHT in camera (counterclockwise crop correction).");
    printf("Deck config is in game_config.h: fixed_deck_enable=%d, seven_to_eight_enable=%d.\n",
           fixed_deck_enable,seven_to_eight_enable);
    puts(fixed_deck_enable?"Fixed deck: D=A234, C=8/9/10/J, H=8/9/10/J, S=A234 (off-suit cards corrected by colour, other ranks rejected).":
         "No fixed deck: any card accepted as the CNN reads it.");
    if(!vision_init())stop_hardware();
    board_init();
    if(!game_config_validate()){puts("STOP: invalid game_config.h deck.");for(;;)__asm volatile("wfi");}
    game_reset_state();joker_mask=0;pending_active=0;
    game_link_start();
    if(!fpga_sync_board(UI_READY,0))fatal_fpga_link();
    show_main_menu();hdmi_log("SYSTEM READY / HDMI OK");
    puts("Permanent camera guide is event-driven: FPGA keeps the last status visible; HPS refreshes it at scans/verification events.");
    for(;;) {
        if(input_mode_sync())show_main_menu();
        if(gesture_input) {
            /* no paused game: PEACE = new game, FIST = camera scan
             * paused game:    PEACE = continue,  FIST = new game;  PALM = keyboard */
            unsigned g;
            puts(game_paused?"\nHAND GESTURES: PEACE=continue game; FIST=new game; PALM=keyboard"
                            :"\nHAND GESTURES: PEACE=new game; FIST=camera scan; PALM=keyboard");fflush(stdout);
            g=gesture_wait_command(0);
            if(g==GESTURE_ERROR){gesture_error_lock=1;continue;}
            if(g==GESTURE_PALM){input_set_mode(0);show_main_menu();continue;}
            if(g!=GESTURE_FIST&&g!=GESTURE_PEACE)continue;
            if(game_paused)line[0]=g==GESTURE_PEACE?'A':'G';
            else line[0]=g==GESTURE_PEACE?'G':'A';
        } else {
            puts(game_paused?"\nG=new game; A=CONTINUE the previous game; H=hand gestures (or flip SW0 up); C=capture gesture images; Q=stop"
                            :"\nG=new game; A=refresh camera/board status; H=hand gestures (or flip SW0 up); C=capture gesture images; Q=stop");
            fflush(stdout);if(!fgets(line,sizeof(line),stdin))break;
            if(input_mode_sync()){show_main_menu();continue;}   /* SW0 flipped: that line only changed the mode */
            if(line[0]=='Q'||line[0]=='q')break;
        }
        switch(toupper((unsigned char)line[0])) {
            case 'G':
            case 'A':
                     if(toupper((unsigned char)line[0])=='A'&&!game_paused){one_shot_status();break;}
                     game_main(toupper((unsigned char)line[0])=='A');   /* A = continue */
                     our_timed_input=0;turn_timeout_seen=0;
                     fpga_timer_command(TIMER_CLEAR);fpga_clear_placement();
                     show_main_menu();break;
            case 'H':input_request_gestures();show_main_menu();break;
            case 'C':gesture_capture_menu();show_main_menu();break;
            default:puts("Enter G, A, H, C, or Q.");break;
        }
    }
    (void)fpga_guide_end();puts("Stopped. Reload AXF to restart.");fflush(stdout);for(;;)__asm volatile("wfi");
}

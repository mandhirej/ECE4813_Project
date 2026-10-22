/* Hand-gesture input: fist / palm / peace in the free zone above DRAW.
 * Include after vision_core.h, board_core.h and hdmi_text_v16_4.h, before the code
 * that reads input.
 *
 * Zone (HG): the three cells below DRAW in the fifth column (camera x 370..459,
 * y 76..249, 90 x 174). The hand enters from the bottom edge of the camera view,
 * fingers pointing up, so the arm never covers the DRAW slot.
 *
 * Per frame: snapshot -> gray zone -> 64x64 area resize -> contrast stretch ->
 * CNN in gesture mode (FPGA scores classes 54..57 only).  This preprocessing matches
 * gesture_work/prep_gesture_dataset.py::preprocess_zone().
 *
 * A gesture becomes a command only after a hand has been in the zone for
 * GESTURE_HOLD_TICKS (3 s) and that gesture has the majority of the readings of the
 * last 3 s. Tuned in the game simulation: clear poses read 67-100% correct, so 80%
 * often never fired, while an unclear PEACE read up to 47% PALM, so a plain
 * majority could fire the wrong command; 60% does neither. After a command the zone must
 * show "nothing" again before the next one is accepted, so one hold never fires twice.
 */
#ifndef GESTURE_CORE_H
#define GESTURE_CORE_H

#define GESTURE_X0 GRID_DRAW_X0
#define GESTURE_Y0 (GRID_DRAW_Y0+GRID_STEP_Y)
#define GESTURE_W  GRID_SLOT_WIDTH
#define GESTURE_H  (3*GRID_STEP_Y)
#define GESTURE_MIN_SPAN 48u            /* same as MIN_SPAN in the Python preprocessing */
#define GESTURE_FIRST_CLASS 54u
#define GESTURE_HOLD_TICKS 150000000u   /* 3 s of the bridge's 50 MHz tick counter */
#define GESTURE_AGREE_PCT 60u           /* share of the readings in the last 3 s; an
                                           unclear PEACE read 47% PALM must not fire */
#define GESTURE_WINDOW 128u             /* >= readings in 3 s (one every ~50 ms) */
#define GESTURE_REARM_NOTHING 2u        /* consecutive "nothing" readings that re-arm */
#define GESTURE_CLEAR_TICKS 25000000u   /* zone empty 0.5 s after a command: arm off DRAW */
/* The CNN is trained for the arm entering HG from the BOTTOM of the camera image
 * (fingers up). Set to 1 if the arm enters from the TOP: the zone is then turned
 * 180 deg before the CNN, so no retraining is needed. */
#ifndef GESTURE_FROM_TOP
#define GESTURE_FROM_TOP 0
#endif
/* camera pixel of zone pixel (x,y) as the CNN sees it */
#define GESTURE_PIX(x,y) (GESTURE_FROM_TOP ?     frame[(GESTURE_Y0+GESTURE_H-1u-(y))*W+GESTURE_X0+GESTURE_W-1u-(x)] :     frame[(GESTURE_Y0+(y))*W+GESTURE_X0+(x)])

enum { GESTURE_FIST=0, GESTURE_PALM=1, GESTURE_PEACE=2, GESTURE_NOTHING=3,
       GESTURE_SWITCHED=253, GESTURE_ABORT=254, GESTURE_ERROR=255 };
static const char *const gesture_names[4]={"FIST","PALM","PEACE","NOTHING"};

static int gesture_input=0;            /* 1: menus/prompts are answered by hand gestures */
static int gesture_armed=1;

/* Switch gesture input on/off; HDMI instruction rows follow (FIST/PEACE/PALM labels). */
static void gesture_set_input(int on)
{
    gesture_input=on;
    hdmi_gesture_labels=on;
    if(on)gesture_armed=0;             /* hand must leave the zone before the first command */
}

/* Input mode: keyboard or hand gestures.
 *   - typing H (any prompt) -> hand gestures
 *   - flipping SW0 up       -> hand gestures, flipping SW0 down -> keyboard
 *   - PALM at the main menu -> keyboard
 * SW0 acts on a CHANGE of position (edge), so it never fights the H key.
 * SW0 is bit 7 of the existing cnn_status PIO (no Platform Designer change); two
 * equal reads are required, which is enough for a slide switch. */
static int gesture_error_lock=0;       /* camera/CNN error: keyboard until the next H / SW0 flip */
static int gesture_sw_prev=-1;         /* last SW0 position seen; -1 = not read yet */
static int gesture_switch_on(void)
{
    uint32_t a=CNN_STATUS_REG,b=CNN_STATUS_REG;
    return (int)(((a&b)>>7)&1u);
}
static void gesture_status(const char *msg);
static void gesture_show_last(void);
/* Make gesture_input follow SW0. Returns 1 if the mode changed (caller redraws). */
/* Change the input mode and say so on the console and HDMI. Returns 1 if changed. */
static int input_set_mode(int gestures)
{
    if(gestures==gesture_input)return 0;
    gesture_set_input(gestures);
    if(gestures){puts("Hand-gesture input ON (FIST=ENTER, PEACE=J, PALM=Q).");gesture_show_last();}
    else {puts("Keyboard input (type H or flip SW0 up for hand gestures).");hdmi_line(6u,"");hdmi_line(7u,"INPUT: KEYBOARD");}
    return 1;
}
/* Apply a SW0 flip, if one happened. Returns 1 if the mode changed (caller redraws). */
static int input_mode_sync(void)
{
    int sw=gesture_switch_on(),want=gesture_input;
    if(sw!=gesture_sw_prev){gesture_sw_prev=sw;gesture_error_lock=0;want=sw;}
    if(gesture_error_lock)want=0;
    return input_set_mode(want);
}
/* Keyboard H: hand gestures. */
static int input_request_gestures(void)
{
    gesture_error_lock=0;
    return input_set_mode(1);
}
static uint8_t gesture_crop[SIDE*SIDE] ALIGNED;

/* Area-average resize of the zone to 64x64, exact integer arithmetic.
 * Coordinates are scaled by 64 so each source pixel spans 64 units and each output
 * pixel spans W (or H) units; overlaps are the integer weights. */
static void gesture_resize_zone(void)
{
    static uint32_t rows[GESTURE_H][SIDE];
    unsigned y,ox,oy;
    for(y=0;y<GESTURE_H;y++) {
        for(ox=0;ox<SIDE;ox++) {
            unsigned a=ox*GESTURE_W,b=a+GESTURE_W,s,acc=0;
            for(s=a/64u;s*64u<b;s++) {
                unsigned lo=s*64u>a?s*64u:a,hi=(s+1u)*64u<b?(s+1u)*64u:b;
                uint32_t p=GESTURE_PIX(s,y);
                unsigned g=(77u*((p>>16)&255u)+150u*((p>>8)&255u)+29u*(p&255u)+128u)>>8;
                acc+=g*(hi-lo);
            }
            rows[y][ox]=acc;                        /* weights sum to GESTURE_W */
        }
    }
    for(oy=0;oy<SIDE;oy++) {
        unsigned a=oy*GESTURE_H,b=a+GESTURE_H,s;
        for(ox=0;ox<SIDE;ox++) {
            uint32_t acc=0;
            for(s=a/64u;s*64u<b;s++) {
                unsigned lo=s*64u>a?s*64u:a,hi=(s+1u)*64u<b?(s+1u)*64u:b;
                acc+=rows[s][ox]*(hi-lo);
            }
            gesture_crop[oy*SIDE+ox]=(uint8_t)((acc+GESTURE_W*GESTURE_H/2u)/(GESTURE_W*GESTURE_H));
        }
    }
}

/* numpy.percentile (linear) truncated to int, from a histogram: value at fractional rank */
static unsigned gesture_rank_value(const uint32_t *hist,unsigned rank)
{
    unsigned v=0,seen=0;
    for(v=0;v<256u;v++){seen+=hist[v];if(seen>rank)return v;}
    return 255u;
}

static void gesture_stretch(void)
{
    uint32_t hist[256];
    unsigned i,lo,hi,a,b;
    memset(hist,0,sizeof(hist));
    for(i=0;i<SIDE*SIDE;i++)hist[gesture_crop[i]]++;
    /* 2nd percentile: rank 0.02*4095 = 81.9; 98th: rank 4013.1 */
    a=gesture_rank_value(hist,81u);b=gesture_rank_value(hist,82u);lo=(10u*a+9u*(b-a))/10u;
    a=gesture_rank_value(hist,4013u);b=gesture_rank_value(hist,4014u);hi=(10u*a+(b-a))/10u;
    if(hi<lo+GESTURE_MIN_SPAN)hi=lo+GESTURE_MIN_SPAN;
    for(i=0;i<SIDE*SIDE;i++) {
        int v=((int)gesture_crop[i]-(int)lo)*255/(int)(hi-lo);
        gesture_crop[i]=(uint8_t)(v<0?0:v>255?255:v);
    }
}

/* One reading: GESTURE_FIST..GESTURE_NOTHING, or GESTURE_ERROR. */
static unsigned gesture_read_once(void)
{
    uint8_t cls;
    int verbose=vision_verbose;
    if(snapshot_frame()<0)return GESTURE_ERROR;
    gesture_resize_zone();
    gesture_stretch();
    vision_verbose=0;
    if(!cnn_run_mode(gesture_crop,0,1,&cls)){vision_verbose=verbose;return GESTURE_ERROR;}
    vision_verbose=verbose;
    return (unsigned)cls-GESTURE_FIRST_CLASS;
}

/* With gesture input on, debug row 5 is the event log (hdmi_log), row 6 keeps the
 * last accepted gesture and what it did, row 7 is the live hold status. */
static void gesture_status(const char *msg){hdmi_line(7u,msg);}
static unsigned gesture_last=GESTURE_NOTHING;
static void gesture_show_last(void)
{
    static const char *const action[4]={"ENTER","Q","J",""};
    char msg[29];
    if(gesture_last>=GESTURE_NOTHING)snprintf(msg,sizeof(msg),"LAST HAND: -");
    else snprintf(msg,sizeof(msg),"LAST HAND: %s = %s",gesture_names[gesture_last],action[gesture_last]);
    hdmi_line(6u,msg);
}

static void gesture_show_progress(unsigned g,uint32_t held)
{
    char msg[40];
    unsigned tenths=(unsigned)(held/5000000u);
    if(tenths>30u)tenths=30u;
    snprintf(msg,sizeof(msg),"HAND: %s %u.%u/3.0 S",gesture_names[g],tenths/10u,tenths%10u);
    gesture_status(msg);
}

/* After a command the HPS scans the board/DRAW at once. Wait until the gesture zone
 * has been empty for GESTURE_CLEAR_TICKS, so a hand or arm that is still being
 * pulled away is not in the camera image of that scan.
 * Returns 0 only if stop() aborts the wait. */
static int gesture_wait_hand_gone(int (*stop)(void))
{
    uint32_t empty_since=0;int empty=0,asked=0;
    for(;;) {
        unsigned g=gesture_read_once();
        uint32_t now=UREG(15);
        if(g==GESTURE_ERROR)return 1;           /* camera problem: let the caller continue */
        if(stop && stop()){gesture_status("HAND: STOPPED - TIME UP");return 0;}
        if(g!=GESTURE_NOTHING){empty=0;
            if(!asked){gesture_status("HAND OK - REMOVE HAND");asked=1;}
            continue;}
        if(!empty){empty=1;empty_since=now;}
        if((uint32_t)(now-empty_since)>=GESTURE_CLEAR_TICKS){gesture_armed=1;return 1;}
    }
}

/* Blocks until a gesture is held for 3 s. Returns GESTURE_FIST/PALM/PEACE,
 * GESTURE_ABORT as soon as stop() (optional, e.g. the turn deadline) returns non-zero,
 * or GESTURE_ERROR if the camera/CNN fails. Never returns GESTURE_NOTHING.
 *
 * Vote over a sliding 3 s window. The leading gesture is the one with the most
 * readings in that window; its 3 s count starts when it takes the lead and
 * RESTARTS whenever another gesture takes the lead or the hand leaves. It fires
 * after 3 s if it also has more than GESTURE_AGREE_PCT of the window's readings.
 * A single misread does not change the lead of a clearly held gesture. */
static unsigned gesture_wait_command(int (*stop)(void))
{
    static uint32_t when[GESTURE_WINDOW];
    static uint8_t what[GESTURE_WINDOW];
    unsigned head=0,count=0,nothing_run=0,shown_half=99u,lead=GESTURE_NOTHING;
    uint32_t lead_since=0;
    gesture_show_last();
    gesture_status(gesture_armed?"HAND: SHOW GESTURE IN ZONE":"HAND: REMOVE HAND FROM ZONE");
    for(;;) {
        unsigned g=gesture_read_once(),n[4]={0,0,0,0},i,best=GESTURE_FIST;
        uint32_t now=UREG(15);
        if(g==GESTURE_ERROR){puts("GESTURE: camera/CNN read failed.");return GESTURE_ERROR;}
        if(stop && stop()){gesture_status("HAND: STOPPED - TIME UP");return GESTURE_ABORT;}
        if(gesture_switch_on()!=gesture_sw_prev){gesture_status("HAND: SW0 FLIPPED");return GESTURE_SWITCHED;}
        if(!gesture_armed) {
            nothing_run=g==GESTURE_NOTHING?nothing_run+1u:0u;
            if(nothing_run>=GESTURE_REARM_NOTHING){gesture_armed=1;gesture_status("HAND: READY");}
            continue;
        }
        /* keep only the readings of the last 3 s */
        when[head]=now;what[head]=(uint8_t)g;head=(head+1u)%GESTURE_WINDOW;
        if(count<GESTURE_WINDOW)count++;
        while(count>1u && (uint32_t)(now-when[(head+GESTURE_WINDOW-count)%GESTURE_WINDOW])>GESTURE_HOLD_TICKS)count--;
        for(i=0;i<count;i++)n[what[(head+GESTURE_WINDOW-1u-i)%GESTURE_WINDOW]]++;
        if(n[GESTURE_NOTHING]*2u>=count) {               /* no hand (any more) */
            if(lead!=GESTURE_NOTHING)gesture_status("HAND: HOLD STILL FOR 3 S");
            lead=GESTURE_NOTHING;shown_half=99u;
            continue;
        }
        for(i=1;i<3u;i++)if(n[i]>n[best])best=i;
        if(best!=lead){lead=best;lead_since=now;shown_half=99u;}   /* new leader: count from 0 */
        {
            uint32_t held=now-lead_since;
            unsigned half=(unsigned)(held/25000000u);          /* 0.5 s steps */
            if(half!=shown_half){gesture_show_progress(lead,held);shown_half=half;}
            if(held>=GESTURE_HOLD_TICKS && n[lead]*100u>GESTURE_AGREE_PCT*count) {
                char msg[32];
                snprintf(msg,sizeof(msg),"HAND COMMAND: %s",gesture_names[lead]);
                gesture_status(msg);
                gesture_last=lead;gesture_show_last();
                printf("GESTURE: %s accepted (%u/%u readings in the last 3 s)\n",gesture_names[lead],n[lead],count);
                gesture_armed=0;
                return gesture_wait_hand_gone(stop)?lead:GESTURE_ABORT;
            }
        }
    }
}

/* Capture mode (keyboard main menu, C): saves the raw grey HG zone (90 x 174, the
 * input of gesture_resize_zone) of GESTURE_CAPTURE_FRAMES snapshots as PGM files on
 * the PC through Arm DS semihosting, in the debugger's working directory, named
 * hg_<label>_<session>_<n>.pgm. These real board-camera images are used to retrain
 * the gesture rows. It also prints what the CNN read, a live accuracy test. */
#define GESTURE_CAPTURE_FRAMES 100u
static uint8_t gesture_zone_gray[GESTURE_H][GESTURE_W];
static void gesture_capture_menu(void)
{
    static const char *const names[4]={"fist","palm","peace","nothing"};
    char line[16];unsigned i,x,y,reads[4]={0,0,0,0};int want;uint32_t session;
    puts("CAPTURE: type F (fist), P (palm), V (peace) or N (nothing = empty HG), then ENTER.");
    if(!fgets(line,sizeof(line),stdin))return;
    switch(toupper((unsigned char)line[0])){
        case 'F':want=0;break; case 'P':want=1;break; case 'V':want=2;break; case 'N':want=3;break;
        default:puts("CAPTURE: cancelled.");return;
    }
    printf("CAPTURE %s: hold it in HG; move, tilt and turn it a little. %u frames start now...\n",
           names[want],GESTURE_CAPTURE_FRAMES);
    hdmi_instructions("CAPTURE MODE","HOLD GESTURE IN HG","MOVE AND TILT IT SLIGHTLY","SAVING TO THE PC","");
    session=UREG(15);
    for(i=0;i<GESTURE_CAPTURE_FRAMES;i++) {
        char name[48];FILE *f;uint8_t cls;
        if(snapshot_frame()<0){puts("CAPTURE: snapshot failed.");return;}
        for(y=0;y<GESTURE_H;y++)for(x=0;x<GESTURE_W;x++){
            uint32_t p=GESTURE_PIX(x,y);
            gesture_zone_gray[y][x]=(uint8_t)((77u*((p>>16)&255u)+150u*((p>>8)&255u)+29u*(p&255u)+128u)>>8);
        }
        gesture_resize_zone();gesture_stretch();
        if(cnn_run_mode(gesture_crop,0,1,&cls))reads[cls-GESTURE_FIRST_CLASS]++;
        snprintf(name,sizeof(name),"hg_%s_%08lx_%03u.pgm",names[want],(unsigned long)session,i);
        f=fopen(name,"wb");
        if(!f){puts("CAPTURE: cannot create files on the PC (semihosting). Stopped.");return;}
        fprintf(f,"P5\n%u %u\n255\n",(unsigned)GESTURE_W,(unsigned)GESTURE_H);
        fwrite(gesture_zone_gray,1,sizeof(gesture_zone_gray),f);fclose(f);
        if(i%10u==9u){char msg[29];snprintf(msg,sizeof(msg),"CAPTURE %s %u/%u",names[want],i+1u,GESTURE_CAPTURE_FRAMES);hdmi_line(7u,msg);}
    }
    printf("CAPTURE done: %u files hg_%s_%08lx_*.pgm. CNN read: fist %u, palm %u, peace %u, nothing %u.\n",
           GESTURE_CAPTURE_FRAMES,names[want],(unsigned long)session,reads[0],reads[1],reads[2],reads[3]);
}

/* Keyboard-equivalent of a gesture for the existing prompts:
 *   FIST  = ENTER (confirm / draw / check; "1" on the who-starts screen)
 *   PEACE = J     (use own joker;          "2" on the who-starts screen)
 *   PALM  = Q     (cancel / menu)
 * The most used command (ENTER) is on FIST, the most distinct gesture: in the game
 * simulation FIST read 100% correct, while PALM and PEACE are sometimes confused. */
static char gesture_key(unsigned g)
{
    return g==GESTURE_FIST?'\n':g==GESTURE_PEACE?'J':g==GESTURE_PALM?'Q':0;
}

#endif /* GESTURE_CORE_H */

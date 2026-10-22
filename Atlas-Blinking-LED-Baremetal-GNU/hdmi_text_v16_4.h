/* V16.4: 8 x 28-character HPS -> HDMI text display.
 * Five instruction rows and three rolling debug rows.
 * Uses the previously idle HDMI mailbox opcode 0. No Qsys changes.
 * Trims trailing spaces and only uploads changed lines to reduce traffic.
 */
#ifndef HDMI_TEXT_V16_4_H
#define HDMI_TEXT_V16_4_H
#define HDMI_ROWS 8u
#define HDMI_COLS 28u
static char hdmi_rows[HDMI_ROWS][HDMI_COLS+1];
static char hdmi_sent[HDMI_ROWS][HDMI_COLS+1];
static int hdmi_text_ready=0;
static unsigned hdmi_log_lane=5u;

static void hdmi_normalize(char out[HDMI_COLS+1],const char *in)
{
    unsigned i=0;
    while(i<HDMI_COLS&&in&&in[i]) {
        unsigned char c=(unsigned char)in[i];
        out[i]=(c>=32u&&c<=126u)?(char)c:' ';
        i++;
    }
    out[i]=0;
}
static void hdmi_reset_shadow(void)
{
    unsigned i;
    for(i=0;i<HDMI_ROWS;i++){hdmi_rows[i][0]=0;hdmi_sent[i][0]=(char)1;hdmi_sent[i][1]=0;}
    hdmi_log_lane=5u;
    hdmi_text_ready=1;
}
/* The RTL consumes printable ASCII one 4-bit nibble at a time; 0xE
 * terminates a line only when waiting for a NEW high nibble. */
static int hdmi_send_line(unsigned row)
{
    unsigned i,n;
    const char *msg=hdmi_rows[row];
    if(!strcmp(hdmi_sent[row],msg))return 1;
    n=(unsigned)strlen(msg);
    while(n && msg[n-1]==' ')n--;
    if(!fpga_command(CMD_CAPTURE,15u)||!fpga_command(CMD_CAPTURE,(uint8_t)row))return 0;
    for(i=0;i<n;i++) {
        uint8_t b=(uint8_t)msg[i];
        if(!fpga_command(CMD_CAPTURE,(uint8_t)(b>>4)) ||
           !fpga_command(CMD_CAPTURE,(uint8_t)(b&15u)))return 0;
    }
    if(!fpga_command(CMD_CAPTURE,14u))return 0;
    strcpy(hdmi_sent[row],msg);
    return 1;
}
static void hdmi_line(unsigned index,const char *message)
{
    char norm[HDMI_COLS+1];
    if(index>=HDMI_ROWS)return;
    if(!hdmi_text_ready)hdmi_reset_shadow();
    hdmi_normalize(norm,message);
    if(!strcmp(hdmi_rows[index],norm))return;
    strcpy(hdmi_rows[index],norm);
    if(!hdmi_send_line(index))fatal_fpga_link();
}
/* With hand-gesture input on, instruction rows name the gesture instead of the key:
 * ENTER -> FIST, "J:" -> "PEACE:", "Q:" -> "PALM:". Set by gesture_set_input(). */
static int hdmi_gesture_labels=0;
static void hdmi_gesture_text(char out[HDMI_COLS+1],const char *in)
{
    unsigned o=0;
    while(in&&*in&&o<HDMI_COLS) {
        const char *rep=0;unsigned skip=0;
        int word_start=(o==0||out[o-1]==' ');
        if(!strncmp(in,"ENTER",5)){rep="FIST";skip=5;}
        else if(word_start&&!strncmp(in,"J:",2)){rep="PEACE:";skip=2;}
        else if(word_start&&!strncmp(in,"Q:",2)){rep="PALM:";skip=2;}
        if(rep){while(*rep&&o<HDMI_COLS)out[o++]=*rep++;in+=skip;}
        else out[o++]=*in++;
    }
    out[o]=0;
}
static void hdmi_instruction_line(unsigned row,const char *msg)
{
    char buf[HDMI_COLS+1];
    if(!hdmi_gesture_labels){hdmi_line(row,msg);return;}
    hdmi_gesture_text(buf,msg);
    hdmi_line(row,buf);
}
/* Untranslated instruction rows, so they can be redrawn when the input mode
 * (keyboard / hand gestures, switch SW0) changes in the middle of a prompt. */
static char hdmi_instr_raw[5][HDMI_COLS+1];
static void hdmi_instructions(const char *a,const char *b,const char *c,const char *d,const char *e)
{
    const char *rows[5]={a,b,c,d,e};unsigned i;
    for(i=0;i<5u;i++){hdmi_normalize(hdmi_instr_raw[i],rows[i]);hdmi_instruction_line(i,hdmi_instr_raw[i]);}
}
static void hdmi_refresh_instructions(void)
{
    unsigned i;for(i=0;i<5u;i++)hdmi_instruction_line(i,hdmi_instr_raw[i]);
}
static void hdmi_log(const char *msg)
{
    /* Update only one of the three debug rows, cycling through rows 5-7.
     * Previous implementation resent all three lines for every event, causing
     * very long blocking HPS->FPGA mailbox traffic during recognition. */
    char norm[HDMI_COLS+1];
    if(!hdmi_text_ready)hdmi_reset_shadow();
    hdmi_normalize(norm,msg);
    /* Gesture input owns rows 6-7 (last hand result, live hold status). */
    if(hdmi_gesture_labels){hdmi_line(5u,norm);return;}
    hdmi_line(hdmi_log_lane,norm);
    hdmi_log_lane=(hdmi_log_lane==7u)?5u:hdmi_log_lane+1u;
}
#endif

/* Unit test of the fixed-deck switches using the real atlas_main.c code. */
/* Build: python build_sim.py, then
 *   gcc -O2 -std=gnu99 -w -I ../../../Atlas-Blinking-LED-Baremetal-GNU -I . -I build test_deck.c -o build/test_deck.exe -lm */
#define main atlas_real_main
#include "atlas_host.c"
#undef main
static uint8_t C(unsigned rank,unsigned suit){uint8_t c;class_from_rank_suit(rank,suit,&c);return c;}
static int fails;
/* n votes of one CNN class, with corner-pip colour unknown */
static int run(uint8_t cls,unsigned n,uint8_t *out)
{
    DrawEvidence e;unsigned i;memset(&e,0,sizeof(e));
    for(i=0;i<n;i++){e.raw_class[i]=cls;e.rank[i]=cnn_class_rank(cls);e.red_score[i]=BOARD_PIP_COLOUR_UNKNOWN;}
    e.count=n;return decode_draw_evidence(&e,out);
}
static void expect(const char *what,uint8_t cls,int want_rc,int want_rank,int want_suit)
{
    uint8_t out=255;int rc=run(cls,5,&out);
    int ok=rc==want_rc&&(rc!=1||(cnn_class_rank(out)==want_rank&&cnn_class_suit(out)==want_suit));
    printf("%-4s %-46s rc=%2d",ok?"PASS":"FAIL",what,rc);
    if(rc==1){printf(" -> ");print_card(out);}
    printf("\n");if(!ok)fails++;
}
int main(void)
{
    const unsigned CL=CNN_SUIT_CLUB,DI=CNN_SUIT_DIAMOND,HE=CNN_SUIT_HEART,SP=CNN_SUIT_SPADE;
    uint8_t cls[16],t;uint16_t m;
    game_reset_state();
    puts("--- fixed_deck_enable=1, seven_to_eight_enable=1");
    fixed_deck_enable=1;seven_to_eight_enable=1;
    expect("2 of clubs (off-deck, black) -> 2S",C(2,CL),1,2,SP);
    expect("2 of hearts (off-deck, red) -> 2D",C(2,HE),1,2,DI);
    expect("9 of diamonds (off-deck, red) -> 9H",C(9,DI),1,9,HE);
    expect("9 of spades (off-deck, black) -> 9C",C(9,SP),1,9,CL);
    expect("J of clubs (in deck) -> JC",C(11,CL),1,11,CL);
    expect("5 of hearts (rank not in deck) -> rejected",C(5,HE),-1,0,0);
    expect("K of spades (rank not in deck) -> rejected",C(13,SP),-1,0,0);
    expect("7 of clubs -> 8C (7-to-8 on)",C(7,CL),1,8,CL);
    expect("7 of diamonds (red) -> 8H (7-to-8 on)",C(7,DI),1,8,HE);
    puts("--- fixed_deck_enable=1, seven_to_eight_enable=0");
    seven_to_eight_enable=0;
    expect("7 of clubs -> rejected (7-to-8 off)",C(7,CL),-1,0,0);
    puts("--- fixed_deck_enable=0 (no fixed deck)");
    fixed_deck_enable=0;seven_to_eight_enable=1;
    expect("2 of clubs -> 2C as read",C(2,CL),1,2,CL);
    expect("5 of hearts -> 5H as read",C(5,HE),1,5,HE);
    expect("7 of clubs -> 7C (no 7-to-8 without deck)",C(7,CL),1,7,CL);
    expect("K of spades -> KS as read",C(13,SP),1,13,SP);
    /* placement without a deck: 5H goes into the heart column, duplicate rejected */
    game_reset_state();
    if(game_build_insert(C(5,HE),cls,&m,&t)&&game_commit_insert(C(5,HE),cls,m)&&game_placed_count()==1u)
        puts("PASS no deck: 5H placed, placed count 1");else{puts("FAIL no deck: 5H placement");fails++;}
    if(!game_build_insert(C(5,HE),cls,&m,&t))puts("PASS no deck: second 5H rejected as duplicate");
    else{puts("FAIL no deck: duplicate accepted");fails++;}
    if(game_build_insert(C(13,HE),cls,&m,&t)&&t==2u)puts("PASS no deck: KH goes above 5H (row 1)");
    else{printf("FAIL no deck: KH target %u\n",t);fails++;}
    /* fixed deck: a placed 2S makes the next black 2 a retry (no other black 2) */
    fixed_deck_enable=1;game_reset_state();
    if(game_build_insert(C(2,SP),cls,&m,&t)&&game_commit_insert(C(2,SP),cls,m))
        expect("after 2S placed: 2 of clubs -> 2D (only 2 left)",C(2,CL),1,2,DI);
    if(game_build_insert(C(5,HE),cls,&m,&t)){puts("FAIL fixed deck accepted 5H placement");fails++;}
    else puts("PASS fixed deck: 5H placement refused");
    printf("\n%s: %d failure(s)\n",fails?"FAILED":"ALL PASS",fails);
    return fails;
}

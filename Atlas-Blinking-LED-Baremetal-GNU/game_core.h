/* V16 pure game logic: fixed 16-card programmable deck, D/C/H/S columns,
 * top-packed descending-rank insertion, two own jokers.
 */
#ifndef GAME_CORE_H
#define GAME_CORE_H
#include "game_config.h"

static uint8_t game_board_class[16];       /* valid only where occupied bit = 1 */
static uint16_t game_occupied_mask;
static uint16_t game_placed_config_mask;   /* one bit per configured normal card */
static uint8_t game_own_jokers_left;
static uint8_t game_opp_jokers_left;      /* opponent jokers not yet played on our board */
static unsigned game_round;

static uint8_t cnn_class_rank(uint8_t c)
{
    if(c<=3)return 10;
    if(c<=35)return (uint8_t)(2u+((c-4u)>>2));
    if(c<=39)return 1;
    if(c<=43)return 11;
    if(c==44||c==45)return 0; /* joker */
    if(c<=49)return 13;
    if(c<=53)return 12;
    return 255;
}
static uint8_t cnn_class_suit(uint8_t c)
{
    if(c<=43)return c&3u;
    if(c==44||c==45)return 255;
    if(c<=49)return (uint8_t)((c-46u)&3u);
    if(c<=53)return (uint8_t)((c-50u)&3u);
    return 255;
}
static int cnn_class_is_joker(uint8_t c){return c==44u||c==45u;}
static int suit_is_red(uint8_t suit){return suit==CNN_SUIT_DIAMOND||suit==CNN_SUIT_HEART;}

static int class_from_rank_suit(uint8_t rank,uint8_t suit,uint8_t *out)
{
    if(suit>3u||!out)return 0;
    if(rank==10u)*out=suit;
    else if(rank>=2u&&rank<=9u)*out=(uint8_t)(4u+(rank-2u)*4u+suit);
    else if(rank==1u)*out=(uint8_t)(36u+suit);
    else if(rank==11u)*out=(uint8_t)(40u+suit);
    else if(rank==13u)*out=(uint8_t)(46u+suit);
    else if(rank==12u)*out=(uint8_t)(50u+suit);
    else return 0;
    return 1;
}

static int game_column_for_suit(uint8_t suit)
{
    unsigned c;
    for(c=0;c<4;c++)if(game_column_suit[c]==suit)return (int)c;
    return -1;
}

static int game_config_class(unsigned col,unsigned k,uint8_t *cls)
{
    if(col>=4u||k>=4u)return 0;
    return class_from_rank_suit(game_deck_ranks[col][k],game_column_suit[col],cls);
}

static int game_config_index_for_class(uint8_t cls)
{
    unsigned c,k; uint8_t v;
    for(c=0;c<4;c++)for(k=0;k<4;k++)
        if(game_config_class(c,k,&v)&&v==cls)return (int)(c*4u+k);
    return -1;
}

/* Recover the one known CNN rank confusion for this deck: an observed 7 is
 * counted as 8 only when the programmable deck contains 8 but no 7.  Do NOT
 * affect the underlying 54-class CNN mapping, and preserve custom decks that
 * deliberately include 7.  The source card is still reported raw in Arm DS. */
static uint8_t game_vote_rank(uint8_t observed_rank)
{
    unsigned col,k; int seven_present=0,eight_present=0;
    if(observed_rank!=7u||!fixed_deck_enable||!seven_to_eight_enable)return observed_rank;
    for(col=0;col<4u;col++)for(k=0;k<4u;k++) {
        if(game_deck_ranks[col][k]==7u)seven_present=1;
        if(game_deck_ranks[col][k]==8u)eight_present=1;
    }
    return (uint8_t)((!seven_present&&eight_present)?8u:observed_rank);
}

/* Fixed deck only: is this rank anywhere in game_deck_ranks? */
static int game_rank_in_deck(uint8_t rank)
{
    unsigned c,k;
    for(c=0;c<4;c++)for(k=0;k<4;k++)if(game_deck_ranks[c][k]==rank)return 1;
    return 0;
}

static int game_config_validate(void)
{
    unsigned c,i,j; uint8_t cls;
    if(!fixed_deck_enable)return 1;   /* no fixed deck to check */
    for(c=0;c<4;c++)for(i=0;i<4;i++) {
        uint8_t r=game_deck_ranks[c][i];
        if(r<1u||r>13u||!class_from_rank_suit(r,game_column_suit[c],&cls))return 0;
        for(j=i+1;j<4;j++)if(game_deck_ranks[c][j]==r)return 0;
    }
    return 1;
}

static void game_reset_state(void)
{
    memset(game_board_class,0,sizeof(game_board_class));
    game_occupied_mask=0;
    game_placed_config_mask=0;
    game_own_jokers_left=GAME_OWN_JOKERS;
    game_opp_jokers_left=GAME_OPP_JOKERS;
    game_round=1u;
}

static unsigned game_placed_count(void)
{
    unsigned i,n=0;
    uint16_t m=fixed_deck_enable?game_placed_config_mask:game_occupied_mask;
    for(i=0;i<16;i++)if(m&(1u<<i))n++;
    return n;
}

static int game_card_already_placed(uint8_t cls)
{
    int idx;unsigned i;
    if(!fixed_deck_enable) {
        for(i=0;i<16;i++)if((game_occupied_mask&(1u<<i))&&game_board_class[i]==cls)return 1;
        return 0;
    }
    idx=game_config_index_for_class(cls);
    return idx>=0 && (game_placed_config_mask&(1u<<idx));
}

/* Return remaining configured classes with this rank. */
static unsigned game_remaining_candidates_for_rank(uint8_t rank,uint8_t out[4])
{
    unsigned c,k,n=0; uint8_t cls;
    for(c=0;c<4;c++)for(k=0;k<4;k++) {
        unsigned idx=c*4u+k;
        if(game_deck_ranks[c][k]!=rank)continue;
        if(game_placed_config_mask&(1u<<idx))continue;
        if(game_config_class(c,k,&cls)&&n<4u)out[n++]=cls;
    }
    return n;
}

static unsigned game_column_count(unsigned col)
{
    unsigned r,n=0;
    if(col>=4u)return 0;
    for(r=0;r<4;r++)if(game_occupied_mask&(1u<<(r*4u+col)))n++;
    return n;
}

/* Choose one opponent joker in the column with the fewest placed normal
 * cards. Equal columns are resolved deterministically left-to-right then top.
 * Returns 16 if no joker exists. */
static unsigned game_choose_joker(uint16_t jokers)
{
    unsigned i,chosen=16u,smallest=5u;
    for(i=0;i<16u;i++)if(jokers&(1u<<i)) {
        unsigned count=game_column_count(i%4u);
        if(count<smallest){smallest=count;chosen=i;}
    }
    return chosen;
}

/* Return a repeatedly observed incorrect rank, or 0 for uncertain.
 * This distinguishes an actual wrong card from an unstable CNN result. */
static unsigned game_wrong_rank(const unsigned votes[14],unsigned expected)
{
    unsigned k,best_rank=0,best_votes=0;
    for(k=1;k<=13;k++)if(k!=expected && votes[k]>best_votes) {
        best_votes=votes[k];best_rank=k;
    }
    return best_votes>=2u?best_rank:0u;
}

/* Build expected board after inserting new_class. Existing board is copied to
 * out_classes/out_mask. Cards in that suit are sorted descending and packed
 * from row 1. target_cell is the new card's row after sorting. */
static int game_build_insert(uint8_t new_class,uint8_t out_classes[16],uint16_t *out_mask,uint8_t *target_cell)
{
    int cfg=game_config_index_for_class(new_class),col;
    uint8_t list[5],tmp; unsigned n=0,r,i,j,target=255;
    if(fixed_deck_enable?cfg<0:(cnn_class_rank(new_class)<1u||cnn_class_rank(new_class)>13u))return 0;
    if(game_card_already_placed(new_class))return 0;
    col=game_column_for_suit(cnn_class_suit(new_class)); if(col<0)return 0;
    memcpy(out_classes,game_board_class,16); *out_mask=game_occupied_mask;
    for(r=0;r<4;r++) {
        unsigned cell=r*4u+(unsigned)col;
        if(game_occupied_mask&(1u<<cell))list[n++]=game_board_class[cell];
    }
    if(n>=4u)return 0;
    list[n++]=new_class;
    for(i=1;i<n;i++) {
        tmp=list[i]; j=i;
        while(j&&cnn_class_rank(list[j-1])<cnn_class_rank(tmp)){list[j]=list[j-1];j--;}
        list[j]=tmp;
    }
    for(r=0;r<4;r++) {
        unsigned cell=r*4u+(unsigned)col;
        *out_mask&=(uint16_t)~(1u<<cell); out_classes[cell]=0;
    }
    for(r=0;r<n;r++) {
        unsigned cell=r*4u+(unsigned)col;
        out_classes[cell]=list[r]; *out_mask|=(uint16_t)(1u<<cell);
        if(list[r]==new_class)target=cell;
    }
    if(target>=16u)return 0;
    *target_cell=(uint8_t)target;
    return 1;
}

static int game_commit_insert(uint8_t new_class,const uint8_t classes[16],uint16_t mask)
{
    int idx=game_config_index_for_class(new_class);
    if(fixed_deck_enable&&idx<0)return 0;
    memcpy(game_board_class,classes,16); game_occupied_mask=mask;
    if(fixed_deck_enable)game_placed_config_mask|=(uint16_t)(1u<<idx);
    return 1;
}

#endif

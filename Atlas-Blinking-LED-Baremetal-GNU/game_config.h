/* V16 programmable bare-metal deck configuration.
 * Edit ONLY the four rank lists below to change the 16-card game deck.
 *
 * Column order is fixed by the game rule:
 *   0 = Diamonds, 1 = Clubs, 2 = Hearts, 3 = Spades.
 *
 * Rank values: A=1, 2..10, J=11, Q=12, K=13.
 * Each suit column must contain exactly four UNIQUE ranks.
 */
#ifndef GAME_CONFIG_H
#define GAME_CONFIG_H

#define GAME_COL_DIAMOND 0u
#define GAME_COL_CLUB    1u
#define GAME_COL_HEART   2u
#define GAME_COL_SPADE   3u

#define CNN_SUIT_CLUB    0u
#define CNN_SUIT_DIAMOND 1u
#define CNN_SUIT_HEART   2u
#define CNN_SUIT_SPADE   3u

static const uint8_t game_column_suit[4] = {
    CNN_SUIT_DIAMOND,
    CNN_SUIT_CLUB,
    CNN_SUIT_HEART,
    CNN_SUIT_SPADE
};

/* Initial competition deck requested by the user. */
static const uint8_t game_deck_ranks[4][4] = {
    { 1u, 2u, 3u, 4u },     /* Diamonds */
    { 8u, 9u,10u,11u },     /* Clubs    */
    { 8u, 9u,10u,11u },     /* Hearts   */
    { 1u, 2u, 3u, 4u }      /* Spades   */
};

#define GAME_OWN_JOKERS 2u
#define GAME_OPP_JOKERS 2u      /* jokers the opponent can place on our board */
#define GAME_NORMAL_CARDS 16u

#endif

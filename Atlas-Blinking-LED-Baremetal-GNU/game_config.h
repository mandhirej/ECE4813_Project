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

/* FIXED DECK SWITCHES (change here, or live in the Arm DS Variables view;
 * change them only between games).
 *
 * fixed_deck_enable = 1: the game uses the fixed deck below.
 *   - A drawn card whose rank is in the deck but whose suit is not (for example
 *     2C or 9D) is auto-corrected to the remaining deck card of the same rank
 *     and colour (2C -> 2S, 9D -> 9H). The suit the CNN reads does not matter.
 *   - A drawn card whose rank is not in the deck (for example 5, 6, Q, K) is
 *     rejected: take it out of DRAW.
 *   - The game ends when all 16 deck cards are placed.
 * fixed_deck_enable = 0: no fixed deck. Every card 2..10, J, Q, K, A of every
 *   suit is accepted exactly as the CNN reads it (rank and suit), with no
 *   auto-correction. A card already on the board is rejected as a duplicate.
 *   The game ends when the board is full (16 cards).
 *
 * seven_to_eight_enable = 1: with the fixed deck on, a CNN "7" counts as 8
 *   (the CNN confuses the two), but only when the deck has an 8 and no 7.
 *   0 = a 7 is treated as a wrong-rank card and rejected.
 *   Has no effect when fixed_deck_enable = 0.
 */
volatile int fixed_deck_enable = 0;
volatile int seven_to_eight_enable = 1;

/* THE FIXED DECK: edit the ranks here. Used only when fixed_deck_enable = 1. */
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

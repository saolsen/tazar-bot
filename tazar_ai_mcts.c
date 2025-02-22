// Rewrite of the MCTS AI for Tazar
// Plans
// * Faster implementation. Changes to gamestate to make it faster.
// * Better memory usage and re-use the search tree accross turns.
// * Better evaluation function.
// * Changes to chance and other things to better weigh volleys and charges.
// * Q: Does it make sense to handle the whole turn as a decision? It means that the tree branches
//      out way more, but it also means that the policy code would make more sense. The way it is
//      now I can't just handle threatened pieces and targets because what does that mean mid-turn?
//      I think probably no, just need a good evaluation function.
// * How do I handle chance nodes? In selection I'm selecting on probability which means I explore
//   the miss more, that's obv not good.
//   I could weigh the probability with the mct.
//   Is there also some usage of the probability I should be doing in backpropagation? Yeah probably.
//   Rollout policy and next child node selection are not the same thing. Think about that.

// changes to game state
// pieces on a 16x16 board
// invalid, empty, id? What needs to be fast and what doesn't I guess is the question. Design around
//   that.
// if applying actions is super fast I think I can avoid cloning the game state. That would be great.


#include "tazar_ai_mcts.h"


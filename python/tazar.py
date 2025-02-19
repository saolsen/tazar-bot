# I wish to train a model to estimate the value of a tazar state.
# Current plan.
# Design and build the model with pytorch.
# Export the model with onnx and load it into the tazar bot.
# Use the model for state estimation in the tazar bot.
# Have the agent play itself and record the gamestates and the outcomes.
# Use the recorded data to train the model (in python).
# Export the model and load it into the tazar bot, keep iterating until it's good.
# I want to do the inference in c because I'll be plugging it into a mcts algorithm.
# I want to do the training in python because it's easier to do the modeling and the training.
#
# I think step 1 is to get any model working in the tazar bot.
# It needs to take some representation of a game state as input, and return a value between -1 and 1.
#
# What should the representation of the game state be?
# The board is 64x64 and there are 3 tile types.
# Then there are pieces on the board, that can be another dimension for each data point.
# The tile kind, the piece kind, the piece player.

# 64 x 64 x n
# the n is the number of features for each tile.
# what features do I want?
# the tile kind.
# who's turn it is 1 for red, -1 for blue.
# the piece kind. 1 for red pike, 2 for red bow, 3 for red knight 4 for red crown, -1 for blue pike, -2 for blue bow, -3 for blue knight, -4 for blue crown.

# One thing that's important to understand for the strategy but hard to represent is how you get to move 2 pieces and a piece can both
# shoot a volley and move in either order.
# A position where your bow can shoot a volley and then move out of range in this same turn is a lot better than
# if you just have a bow in range of a bad guy.
from torch import nn 

if __name__ == '__main__':
    print('Tazar Bot')
    print(nn.Module)


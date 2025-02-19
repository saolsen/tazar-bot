from model import TazarValueNetwork
from tazar import new_game, game_input

game = new_game()
input = game_input(game)

value_network = TazarValueNetwork()
pred = value_network(input.unsqueeze(0))

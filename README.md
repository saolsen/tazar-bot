# Tazar Bot

This is an "AI" bot for the board game Tazar.

* You can play Tazar here https://kelleherbros.itch.io/tazar.
* You can play against this bot here https://steveolsen.itch.io/tazar-bot.

The bot only works with 2 players, game mode attrition and the hex-field-small map for now.

There are 3 different "difficulties" which are really 3 different bots that use different strategies for picking a move.


# Easy

The Easy bot picks it's next move based on which resulting board has the most "value".
It goes through the possible moves it could make, and checks what the value of the next board is.
The "value" function just looks at the amount of pieces on the board for each player and the amount of gold they have.
So something like killing an opponents rook would add 1 to your value score and subtract one from your opponents.
The easy bot doesn't do any "thinking ahead", it greedily picks the next move that maximizes it's value.

# Medium

The medium bot picks it's next move by playing out a number of simulated games and picking the move with the best average value.
While the easy bot checks moves by taking them and then checking the resulting value, the medium bot checks moves
by taking them, and then simulating the rest of the game as if two easy bots were playing. Once the game is over it records the value at the end of the game. It does this a bunch of times for each move, and then picks the move that had the greatest average value at the end of all the simulations.

# Hard

The hard bot picks it's next move in the same way that the medium bot does, by simulating games from various actions and checking the resulting values. But the games it chooses to simulate are different. The medium bot simulates the same number of games for each possible action it could take. This mean it'll spend a lot of time simulating games for obviously horrible moves.
The hard bot keeps track of a tree of actions and expands it where the average value is looking good. This means it does a much higher percentage of it's simulations on the "likely" future and it explores much more relevant gamestates, thus makes much better decisions. It has the best ability to "think ahead".
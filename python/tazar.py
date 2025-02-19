import enum

import pydantic
import torch


class CPos(pydantic.BaseModel, frozen=True):
    q: int
    r: int
    s: int


RIGHT_UP = CPos(q=1, r=-1, s=0)
RIGHT = CPos(q=1, r=0, s=-1)
RIGHT_DOWN = CPos(q=0, r=1, s=-1)
LEFT_DOWN = CPos(q=-1, r=1, s=0)
LEFT = CPos(q=-1, r=0, s=1)
LEFT_UP = CPos(q=0, r=-1, s=1)


def cpos_add(a: CPos, b: CPos) -> CPos:
    return CPos(q=a.q + b.q, r=a.r + b.r, s=a.s + b.s)


class Player(enum.Enum):
    NONE = 0
    RED = 1
    BLUE = 2


class PieceKind(enum.Enum):
    NONE = 0
    PIKE = 1
    BOW = 2
    HORSE = 3
    CROWN = 4


def piece_movement(kind: PieceKind) -> int:
    match kind:
        case PieceKind.CROWN:
            return 1
        case PieceKind.PIKE:
            return 2
        case PieceKind.HORSE:
            return 3
        case PieceKind.BOW:
            return 2
        case _:
            return 0


def piece_strength(kind: PieceKind) -> int:
    match kind:
        case PieceKind.CROWN:
            return 0
        case PieceKind.PIKE:
            return 3
        case PieceKind.HORSE:
            return 2
        case PieceKind.BOW:
            return 1
        case _:
            return -1


class Piece(pydantic.BaseModel):
    kind: PieceKind
    player: Player
    id: int


class OrderKind(enum.Enum):
    NONE = 0
    MOVE = 1
    VOLLEY = 2


class Order(pydantic.BaseModel):
    kind: OrderKind
    target: CPos


class Activation(pydantic.BaseModel):
    piece_id: int
    orders: list[Order]
    order_i: int


class Turn(pydantic.BaseModel):
    player: Player
    activations: list[Activation]
    activation_i: int


class Status(enum.Enum):
    NONE = 0
    IN_PROGRESS = 1
    OVER = 2


class Game(pydantic.BaseModel):
    pieces: dict[CPos, Piece]
    status: Status
    winner: Player
    turn: Turn


def new_game() -> Game:
    pieces = {
        CPos(q=-4, r=0, s=4): Piece(
            kind=PieceKind.CROWN,
            player=Player.RED,
            id=1,
        ),
        CPos(q=-2, r=-2, s=4): Piece(
            kind=PieceKind.BOW,
            player=Player.RED,
            id=2,
        ),
        CPos(q=-1, r=-3, s=4): Piece(
            kind=PieceKind.HORSE,
            player=Player.RED,
            id=3,
        ),
        CPos(q=0, r=-3, s=3): Piece(
            kind=PieceKind.PIKE,
            player=Player.RED,
            id=4,
        ),
        CPos(q=-1, r=-2, s=3): Piece(
            kind=PieceKind.PIKE,
            player=Player.RED,
            id=5,
        ),
        CPos(q=-2, r=-1, s=3): Piece(
            kind=PieceKind.BOW,
            player=Player.RED,
            id=6,
        ),
        CPos(q=-1, r=-1, s=2): Piece(
            kind=PieceKind.PIKE,
            player=Player.RED,
            id=7,
        ),
        CPos(q=-2, r=0, s=2): Piece(
            kind=PieceKind.PIKE,
            player=Player.RED,
            id=8,
        ),
        CPos(q=-3, r=0, s=3): Piece(
            kind=PieceKind.BOW,
            player=Player.RED,
            id=9,
        ),
        CPos(q=-3, r=1, s=2): Piece(
            kind=PieceKind.HORSE,
            player=Player.RED,
            id=10,
        ),
        CPos(q=-2, r=1, s=1): Piece(
            kind=PieceKind.PIKE,
            player=Player.RED,
            id=11,
        ),
        CPos(q=4, r=0, s=-4): Piece(
            kind=PieceKind.CROWN,
            player=Player.BLUE,
            id=12,
        ),
        CPos(q=4, r=-1, s=-3): Piece(
            kind=PieceKind.BOW,
            player=Player.BLUE,
            id=13,
        ),
        CPos(q=4, r=-2, s=-2): Piece(
            kind=PieceKind.HORSE,
            player=Player.BLUE,
            id=14,
        ),
        CPos(q=3, r=-2, s=-1): Piece(
            kind=PieceKind.PIKE,
            player=Player.BLUE,
            id=15,
        ),
        CPos(q=3, r=-1, s=-2): Piece(
            kind=PieceKind.PIKE,
            player=Player.BLUE,
            id=16,
        ),
        CPos(q=3, r=0, s=-3): Piece(
            kind=PieceKind.BOW,
            player=Player.BLUE,
            id=17,
        ),
        CPos(q=2, r=0, s=-2): Piece(
            kind=PieceKind.PIKE,
            player=Player.BLUE,
            id=18,
        ),
        CPos(q=2, r=1, s=-3): Piece(
            kind=PieceKind.PIKE,
            player=Player.BLUE,
            id=19,
        ),
        CPos(q=3, r=1, s=-4): Piece(
            kind=PieceKind.BOW,
            player=Player.BLUE,
            id=20,
        ),
        CPos(q=2, r=2, s=-4): Piece(
            kind=PieceKind.HORSE,
            player=Player.BLUE,
            id=21,
        ),
        CPos(q=1, r=2, s=-3): Piece(
            kind=PieceKind.PIKE,
            player=Player.BLUE,
            id=22,
        ),
    }

    return Game(
        pieces=pieces,
        status=Status.IN_PROGRESS,
        winner=Player.NONE,
        turn=Turn(
            player=Player.RED,
            activations=[
                Activation(
                    piece_id=0,
                    orders=[],
                    order_i=0,
                ),
                Activation(
                    piece_id=0,
                    orders=[],
                    order_i=0,
                ),
            ],
            activation_i=1,
        ),
    )


class CommandKind(enum.Enum):
    NONE = 0
    MOVE = 1
    VOLLEY = 2
    END_TURN = 3


class Command(pydantic.BaseModel):
    kind: CommandKind
    piece_pos: CPos
    target_pos: CPos


def piece_allowed_order_kinds(game: Game, piece: Piece) -> list[OrderKind]:
    # Can't do anything if the game is over.
    if game.status != Status.IN_PROGRESS:
        return []

    # Can't do anything if it's not the player's turn.
    if game.turn.player != piece.player:
        return []

    piece_already_used = False
    for i in range(game.turn.activation_i):
        activation = game.turn.activations[i]
        if activation.piece_id == piece.id:
            piece_already_used = True
            break
    if piece_already_used:
        return []

    assert (game.turn.activation_i < 2)
    activation = game.turn.activations[game.turn.activation_i]

    piece_can_move: bool = True
    piece_can_act: bool = True

    if activation.piece_id == 0:
        pass
    elif activation.piece_id == piece.id:
        assert activation.order_i > 0

        for i in range(activation.order_i):
            if activation.orders[i].kind == OrderKind.MOVE:
                piece_can_move = False
            elif activation.orders[i].kind == OrderKind.VOLLEY:
                piece_can_act = False

    elif activation.piece_id != 0:
        # Using this piece would end the current activation and start a new one.
        # Can only do this if there are more activations left.
        if game.turn.activation_i + 1 >= 2:
            piece_can_move = False
            piece_can_act = False
    else:
        assert False, "unreachable"

    # Horses and pikes don't have an action, they can only move.
    if piece.kind == PieceKind.HORSE or piece.kind == PieceKind.PIKE:
        piece_can_act = False
    # Also true for crowns in attrition.
    if piece.kind == PieceKind.CROWN:
        piece_can_act = False

    kinds = []
    if piece_can_move:
        kinds.append(OrderKind.MOVE)
    if piece_can_act:
        kinds.append(OrderKind.VOLLEY)
    return kinds


def volley_targets(game: Game, from_pos: CPos) -> list[CPos]:
    piece = game.pieces[from_pos]
    if piece is None:
        return []

    targets = []

    for r in range(-2, 3):
        for q in range(-2, 3):
            for s in range(-2, 3):
                if q + r + s != 0:
                    continue
                assert s == -q - r
                offset = CPos(q=q, r=r, s=s)

                if offset == CPos(q=0, r=0, s=0):
                    # Can't shoot yourself.
                    continue

                cpos = cpos_add(from_pos, offset)
                target_piece = game.pieces.get(cpos)

                if target_piece is None:
                    continue
                if target_piece.player == piece.player:
                    continue

                targets.append(cpos)

    return targets


def move_targets(game: Game, from_pos: CPos) -> list[CPos]:
    piece = game.pieces[from_pos]
    if piece is None:
        return []

    movement = piece_movement(piece.kind)
    max_strength = piece_strength(piece.kind) - 1

    # Horses can move into the tile of any other piece.
    if piece.kind == PieceKind.HORSE:
        max_strength = 3

    visited = [from_pos]

    i = 0
    for steps in range(movement):
        visited_count = len(visited)
        while i < visited_count:
            current_pos = visited[i]

            # Don't continue moving through another piece.
            current_piece = game.pieces.get(current_pos)
            if i > 0 and current_piece is not None:
                continue

            # Check neighboring tiles.
            neighbors = [
                cpos_add(current_pos, RIGHT_UP),
                cpos_add(current_pos, RIGHT),
                cpos_add(current_pos, RIGHT_DOWN),
                cpos_add(current_pos, LEFT_DOWN),
                cpos_add(current_pos, LEFT),
                cpos_add(current_pos, LEFT_UP),
            ]
            for n in neighbors:
                # Skip if we've already visited this tile.
                if n in visited:
                    continue

                # Don't step off the board.
                if n.q < -4 or n.q > 4 or n.r < -4 or n.r > 4 or n.s < -4 or n.s > 4:
                    continue

                neighbor_piece = game.pieces.get(n)
                if neighbor_piece is not None:
                    # Can't move into a tile with own piece.
                    if neighbor_piece.player == piece.player:
                        continue

                    # Can't move into a tile with a stronger piece.
                    if piece_strength(neighbor_piece.kind) > max_strength:
                        continue

                visited.append(n)

            i += 1

    return visited[1:]


def game_valid_commands(game: Game) -> list[Command]:
    if game.status != Status.IN_PROGRESS:
        return []

    commands = [Command(
        kind=CommandKind.END_TURN,
        piece_pos=CPos(q=0, r=0, s=0),
        target_pos=CPos(q=0, r=0, s=0),
    )]

    return commands


# map, pieces (8), turn, can_move, can_act
def game_input(game: Game) -> torch.Tensor:
    input = torch.zeros(12, 16, 16)

    def set(channel, q, r):
        input[channel][(r + 8)][(q + 8)] = 1

    # channel 0, terrain
    for q in range(-4, 5):
        for r in range(-4, 5):
            for s in range(-4, 5):
                if q + r + s != 0:
                    continue
                set(0, q, r)

    # 8 channels for the various piece locations.
    for q in range(-4, 5):
        for r in range(-4, 5):
            for s in range(-4, 5):
                if q + r + s != 0:
                    continue
                pos = CPos(q=q, r=r, s=s)
                if pos in game.pieces:
                    piece = game.pieces[pos]

                    # 8 channels for the various piece locations.
                    channel = 4 * (piece.player.value - 1) + piece.kind.value
                    set(channel, q, r)

                    allowed_order_kinds = piece_allowed_order_kinds(game, piece)

                    # channel 10, can move
                    if OrderKind.MOVE in allowed_order_kinds:
                        set(10, q, r)
                    # channel 11, can volley
                    if OrderKind.VOLLEY in allowed_order_kinds:
                        set(11, q, r)

    # channel 9, player turn
    if game.turn.player == Player.RED:
        input[9] = 1

    return input

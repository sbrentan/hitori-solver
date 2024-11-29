import random
import os
from enum import Enum
from collections import deque 

class NumberState(Enum):
    Normal = 0
    Shaded = 1
    Marked = 2
    LAShaded = 3
    LAMarked = 4

    @staticmethod
    def is_clear(state):
        return state in (NumberState.Normal, NumberState.Marked, NumberState.LAMarked)

class BoardNumber:
    def __init__(self, value: int, index: int):
        assert value > 0
        self.value = value
        self.index = index
        self.state = NumberState.Normal
    
    def get_value(self) -> int:
        return self.value
    
    def get_index(self) -> int:
        return self.index
    
    def get_state(self) -> NumberState:
        return self.state
    
    def set_state(self, state: NumberState):
        self.state = state

class BoardState(Enum):
    Incomplete = 0
    Complete = 1
    Invalid = 2

class Board:
    def __init__(self, board: list[BoardNumber], size: int):
        self.board = board
        self.size = size

    def get_size(self) -> int:
        return self.size
    
    def get_row(self, number: BoardNumber) -> int:
        return number.get_index() // self.size
    
    def get_column(self, number: BoardNumber) -> int:
        return number.get_index() % self.size
    
    def get(self, row: int, column: int) -> BoardNumber:
        return self.board[row * self.size + column]
    
    def print(self):
        for i in range(self.size):
            for j in range(self.size):
                if j == self.size - 1:
                    print(f"{self.get(i, j).get_value()}", end="")
                else:
                    print(f"{self.get(i, j).get_value()} ", end="")
            print()
    
    def save(self):
        filename = "test-" + str(self.size) + "x" + str(self.size) + ".txt"
        path = os.path.join(os.path.dirname(os.path.realpath(__file__)), filename)
        with open(path, "w") as file:
            for i in range(self.size):
                for j in range(self.size):
                    if j == self.size - 1:
                        file.write(f"{self.get(i, j).get_value()}")
                    else:
                        file.write(f"{self.get(i, j).get_value()} ")
                file.write("\n")

    def visit(self, row: int, col: int, visited: list[bool]) -> int:
        if row < 0 or row >= self.size or col < 0 or col >= self.size or visited[row * self.size + col] or not NumberState.is_clear(self.get(row, col).get_state()):
            return 0
        
        visited[row * self.size + col] = True
        return 1 + self.visit(row - 1, col, visited) + self.visit(row + 1, col, visited) + self.visit(row, col - 1, visited) + self.visit(row, col + 1, visited)

    def get_state(self) -> BoardState:
        ''' 
            1. No more than 1 of each number in each row and column
            2. No shaded cells are adjacent (horizontally or vertically)
            3. All unshaded cells form a single connected group
        '''

        is_incomplete = False
        
        # Check for duplicates
        for row in range(self.size):
            row_mask = 0
            col_mask = 0
            row_last_shaded = False
            col_last_shaded = False
            
            for col in range(self.size):
                row_value = self.get(row, col).get_value()
                row_is_shaded = not NumberState.is_clear(self.get(row, col).get_state())

                col_value = self.get(col, row).get_value()
                col_is_shaded = not NumberState.is_clear(self.get(col, row).get_state())
                
                if not row_is_shaded:
                    if (row_mask & (1 << row_value)) != 0:
                        is_incomplete = True
                    else:
                        row_mask |= (1 << row_value)
                elif row_last_shaded:
                    return BoardState.Invalid
                
                row_last_shaded = row_is_shaded

                if not col_is_shaded:
                    if (col_mask & (1 << col_value)) != 0:
                        is_incomplete = True
                    else:
                        col_mask |= (1 << col_value)
                elif col_last_shaded:
                    return BoardState.Invalid
                
                col_last_shaded = col_is_shaded

        # Check for connected group
        group_size = 0
        visited = [self.size][self.size]
        
        for row in range(self.size):
            for col in range(self.size):
                visited[row][col] = False
                if NumberState.is_clear(self.get(row, col).get_state()):
                    group_size += 1
        
        if group_size != self.visit(0, 0, visited):
            return BoardState.Invalid

        if is_incomplete:
            return BoardState.Incomplete
        
        return BoardState.Complete

class DuplicateFiller:

    def fill(self, base: list[int], shade_map: list[bool], size: int, random: random.Random):
        self.data = base
        self.shade_map = shade_map
        self.size = size
        self.random = random

        self.apply_duplicates()

    def apply_duplicates(self):
        for i in range(self.size):
            for j in range(self.size):
                if not self.shade_map[i * self.size + j]:
                    continue

                mask = self.generate_row_mask(i, self.shade_map) | self.generate_column_mask(j, self.shade_map)
                value = self.random_number(mask)
                self.data[i * self.size + j] = value
    
    def generate_row_mask(self, row: int, shade_map: list[bool]) -> int:
        mask = 0
        for i in range(self.size):
            if not shade_map[row * self.size + i]:
                mask |= 1 << self.data[row * self.size + i]
        return mask
    
    def generate_column_mask(self, column: int, shade_map: list[bool]) -> int:
        mask = 0
        for i in range(self.size):
            if not shade_map[i * self.size + column]:
                mask |= 1 << self.data[i * self.size + column]
        return mask
    
    def random_number(self, mask: int) -> int:
        choices = 0
        for i in range(1, self.size + 1):
            if (mask & (1 << i)) != 0:
                choices += 1
        
        choice = self.random.randint(0, choices - 1)
        for i in range(1, self.size + 1):
            if (mask & (1 << i)) != 0:
                if choice == 0:
                    return i
                choice -= 1

class SwapPopulator:
    def populate(self, data: list[int], size: int, random: random.Random):
        for i in range(size):
            for j in range(size):
                data[i * size + j] = (i + j) % size + 1

        count = 10
        for i in range(count):
            col_from = random.randint(0, size - 1)
            col_to = random.randint(0, size - 2)

            if col_to >= col_from:
                col_to += 1

            for i in range(size):
                temp = data[col_to + i * size]
                data[col_to + i * size] = data[col_from + i * size]
                data[col_from + i * size] = temp

            row_from = random.randint(0, size - 1)
            row_to = random.randint(0, size - 2)

            if row_to >= row_from:
                row_to += 1

            for i in range(size):
                temp = data[i + row_to * size]
                data[i + row_to * size] = data[i + row_from * size]
                data[i + row_from * size] = temp

class RandomShader:
    def __init__(self, frequency: float):
        assert frequency >= 0
        self.frequency = frequency
        self.is_frequency_fixed = True
    
    def __init__(self):
        self.is_frequency_fixed = False

    def generate(self, size: int, random: random.Random):

        if not self.is_frequency_fixed:
            self.frequency = (size * size) // 2
        
        self.shade_map: bool = [False] * size * size
        self.size = size
        self.random = random

        self.random_shade()
        self.make_contiguous()
        self.fill_empty()

        return self.shade_map

    def random_shade(self):
        added = 0

        pos = [(0, -1), (0, 1), (-1, 0), (1, 0)]

        while added < self.frequency:
            index = self.random.randint(0, len(self.shade_map) - 1)
            ok = True

            for p in pos:
                next = self.get_index(index, p[0], p[1])
                if next >= 0 and self.shade_map[next]:
                    ok = False
            
            if ok:
                self.shade_map[index] = True
                added += 1
    
    def make_contiguous(self):
        visited = self.shade_map.copy()
        can_reach_all = False

        while not can_reach_all:
            search = deque()

            for i in range(len(self.shade_map)):
                if not self.shade_map[i]:
                    search.append(i)
                    break

            while search:
                index = search.pop()
                visited[index] = True

                pos = [(0, -1), (0, 1), (-1, 0), (1, 0)]

                for p in pos:
                    next = self.get_index(index, p[0], p[1])
                    if next >= 0 and not visited[next] and not self.shade_map[next]:
                        search.append(next)
                
            can_reach_all = True

            pos = [(-1, -1), (-1, 1), (1, -1), (1, 1)]

            for i in range(len(visited)):
                if not visited[i]:
                    can_reach_all = False
                    done = False

                    for p in pos:
                        next = self.get_index(i, p[0], p[1])
                        if next >= 0 and visited[next] and not self.shade_map[next]:
                            self.unshade(i, p[0], p[1])
                            done = True
                    
                    if done:
                        break
            
            visited = self.shade_map.copy()
    
    def fill_empty(self):
        potentials = []
        for i in range(len(self.shade_map)):
            if self.shade_map[i]:
                continue

            next = self.get_index(i, 0, -1)
            if next >= 0 and self.shade_map[next]:
                continue

            next = self.get_index(i, 0, 1)
            if next >= 0 and self.shade_map[next]:
                continue

            next = self.get_index(i, -1, 0)
            if next >= 0 and self.shade_map[next]:
                continue

            next = self.get_index(i, 1, 0)
            if next >= 0 and self.shade_map[next]:
                continue  
            
            potentials.append(i)
        
        while potentials:
            array_index = self.random.randint(0, len(potentials) - 1)
            index = potentials.pop(array_index)

            ok = True

            pos = [(0, -1), (0, 1), (-1, 0), (1, 0)]

            for p in pos:
                next = self.get_index(index, p[0], p[1])
                if next >= 0 and self.shade_map[next]:
                    ok = False
            
            if not ok:
                continue

            if self.can_reach_neighbors(self.shade_map, index):
                self.shade_map[index] = True

    def get_index(self, start: int, dx: int, dy: int) -> int:
        r = start // self.size
        c = start % self.size

        r += dx
        c += dy

        if r < 0 or r >= self.size or c < 0 or c >= self.size:
            return -1
        
        return r * self.size + c

    def unshade(self, index: int, dx: int, dy: int):
        r = index // self.size
        c = index % self.size

        r += dx
        c += dy

        isHorizontal = random.choice([True, False])

        if r < 0 or r >= self.size:
            isHorizontal = True
        elif c < 0 or c >= self.size:
            isHorizontal = False
        
        if isHorizontal:
            self.shade_map[self.get_index(index, 0, dy)] = False
        else:
            self.shade_map[self.get_index(index, dx, 0)] = False
    
    def can_reach_neighbors(self, shade_map: list[bool], index: int) -> bool:
        visited = shade_map.copy()
        visited[index] = True

        left = self.get_index(index, 0, -1)
        right = self.get_index(index, 0, 1)
        up = self.get_index(index, -1, 0)
        down = self.get_index(index, 1, 0)

        search = deque()

        if left >= 0:
            search.append(left)
        elif right >= 0:
            search.append(right)
        elif up >= 0:
            search.append(up)
        elif down >= 0:
            search.append(down)

        while search:
            item = search.pop()
            visited[item] = True

            pos = [(0, -1), (0, 1), (-1, 0), (1, 0)]

            for p in pos:
                next = self.get_index(item, p[0], p[1])
                if next >= 0 and not visited[next] and not shade_map[next]:
                    search.append(next)

        if left >= 0 and not visited[left]:
            return False
        
        if right >= 0 and not visited[right]:
            return False
        
        if up >= 0 and not visited[up]:
            return False
        
        if down >= 0 and not visited[down]:
            return False
        
        return True

class Generator:
    def __init__(self, size, initial_populator: SwapPopulator, shade_strategy: RandomShader, duplicate_strategy: DuplicateFiller, random_seed=None):
        assert size > 2
        
        self.size = size
        self.initial_populator = initial_populator
        self.shade_strategy = shade_strategy
        self.duplicate_strategy = duplicate_strategy
        
        if random_seed is None:
            random_seed = random.randint(0, 2**32 - 1)
        
        print(f"Seed: {random_seed}")
        self.random = random.Random(random_seed)
        self.data: int = [0] * size * size
    
    def finalize_board(self) -> list[BoardNumber]:
        final_data: BoardNumber = [None] * self.size * self.size
        for i in range(len(self.data)):
            final_data[i] = BoardNumber(self.data[i], i)
        return final_data
    
    def generate(self) -> Board:
        self.initial_populator.populate(self.data, self.size, self.random)
        shade_map = self.shade_strategy.generate(self.size, self.random)
        self.duplicate_strategy.fill(self.data, shade_map, self.size, self.random)
        
        return Board(self.finalize_board(), self.size)

if __name__ == "__main__":
    generator = Generator(26, SwapPopulator(), RandomShader(), DuplicateFiller())
    board = generator.generate()
    # board.print()
    board.save()
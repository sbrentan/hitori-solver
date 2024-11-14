# Hitori Generator

# Size NxN

# 1. From 0,0 randomly put next cell
# 2. If next cell is already in the same row or column, with some probability set white cell of those
# 3. Repeat 1 and 2 until all cells are filled

import random
import os
import enum
import sys
# sys.setrecursionlimit(10000)

class CellValue(enum.Enum):
    WHITE = 0
    BLACK = 1

    def __str__(self):
        return "X" if self == CellValue.BLACK else "O"

class HitoriGenerator:
    def __init__(self, size, black_whites=None):
        self.size = size
        self.board = None
        self.visited = [[False for i in range(size)] for j in range(size)]
        self.board = [[0 for i in range(size)] for j in range(size)]
        self.numbers = [[0 for i in range(size)] for j in range(size)]
        if not black_whites:
            self.fill_board(0, 0)
            while not self.all_white_cells_connected():
                self.fix_non_connected_cells()
        else:
            self.board = black_whites

    def __str__(self):
        result = ""
        for row in self.board:
            for cell in row:
                result += str(cell) + " "
            result += "\n"
        return result
    
    @property
    def grid(self):
        result = ""
        for row in self.numbers:
            for cell in row:
                result += str(cell) + " " if cell > 9 else str(cell) + "  "
            result += "\n"
        return result
    
    def __repr__(self):
        return self.__str__()

    def fill_board(self, row, col):
        if row == self.size:
            return True

        next_row = row
        next_col = col + 1
        if next_col == self.size:
            next_row += 1
            next_col = 0

        cell_values = [CellValue.WHITE, CellValue.BLACK]
        random.shuffle(cell_values)
        for cell_value in cell_values:
            self.board[row][col] = cell_value
            ok = True
            if cell_value == CellValue.BLACK:
                ok = self.is_black_cell_valid(row, col)
            if ok and self.fill_board(next_row, next_col):
                return True
        return False
    
    def _dfs(self, row, col, visited):
        if row < 0 or row >= self.size or col < 0 or col >= self.size or visited[row][col]:
            return
        visited[row][col] = True
        if self.board[row][col] == CellValue.BLACK:
            return
        self._dfs(row-1, col, visited)
        self._dfs(row+1, col, visited)
        self._dfs(row, col-1, visited)
        self._dfs(row, col+1, visited)
    
    def all_white_cells_connected(self):
        self.visited = [[False for i in range(self.size)] for j in range(self.size)]

        # find first cell white 
        for row in range(self.size):
            for col in range(self.size):
                if self.board[row][col] == CellValue.WHITE:
                    break
            if self.board[row][col] == CellValue.WHITE:
                break
        
        self._dfs(row, col, self.visited)

        # print("checking visited")
        # for row in self.visited:
        #     print(row)
        # input()

        for row in range(self.size):
            for col in range(self.size):
                if self.board[row][col] == CellValue.WHITE and not self.visited[row][col]:
                    return False
        
        return True
    
    def fix_non_connected_cells(self):
        # print visited
        # for row in self.visited:
        #     print(row)
        # print()
        # print board
        # print(self)
        # print()
        # find first cell not visited and white
        for row in range(self.size):
            for col in range(self.size):
                if self.board[row][col] == CellValue.WHITE and not self.visited[row][col]:
                    break
            if self.board[row][col] == CellValue.WHITE and not self.visited[row][col]:
                break

        # do a dfs from that cell looking for the other white cells
        visited2 = [[False for i in range(self.size)] for j in range(self.size)]
        self._dfs(row, col, visited2)

        # print visited2
        # print("visited2")
        # for row in visited2:
        #     print(row)
        
        # for each non-visited cell in visited2, see if setting to white a neighbor cell makes it connected
        skip = False
        found = False
        for row in range(self.size):
            for col in range(self.size):
                if self.board[row][col] == CellValue.WHITE and not visited2[row][col]:
                    found = True
                    if row > 0 and visited2[row-1][col] and self.visited[row-1][col]:
                        self.board[row-1][col] = CellValue.WHITE
                    elif row < self.size-1 and visited2[row+1][col] and self.visited[row+1][col]:
                        self.board[row+1][col] = CellValue.WHITE
                    elif col > 0 and visited2[row][col-1] and self.visited[row][col-1]:
                        self.board[row][col-1] = CellValue.WHITE
                    elif col < self.size-1 and visited2[row][col+1] and self.visited[row][col+1]:
                        self.board[row][col+1] = CellValue.WHITE
                    else:
                        continue
                    skip = True
                    break
            if skip:
                break
        if found:
            for row in range(self.size):
                for col in range(self.size):
                    if self.board[row][col] == CellValue.WHITE and visited2[row][col]:
                        self.visited[row][col] = True                      
            return self.fix_non_connected_cells()
        
    def assign_cell_numbers(self):
        # cell_numbers = [[0 for i in range(self.size)] for j in range(self.size)]
        list_to_assigns = []
        for row in range(self.size):
            row_white_cells = sum([1 for cell in self.board[row] if cell == CellValue.WHITE])
            list_to_assigns.append((0, row, row_white_cells))
        for col in range(self.size):
            col_white_cells = sum([1 for row in self.board if row[col] == CellValue.WHITE])
            list_to_assigns.append((1, col, col_white_cells))
        list_to_assigns.sort(key=lambda x: x[2], reverse=True)
        row_values = {i: [] for i in range(self.size)}
        col_values = {i: [] for i in range(self.size)}
        for lst in list_to_assigns:
            item = lst[1]
            # print("lst", lst)
            # random.shuffle(random_numbers)
            possible_numbers = {i: list(range(1, self.size+1)) for i in range(self.size)}
            numbers_count = {i: 0 for i in range(1, self.size + 1)}
            for i in range(self.size):
                # print("i - lst", i, lst[0])
                if lst[0] == 0 and self.board[item][i] == CellValue.WHITE and self.numbers[item][i] == 0: row, col = item, i
                elif lst[0] == 1 and self.board[i][item] == CellValue.WHITE and self.numbers[i][item] == 0: row, col = i, item
                else: continue
                updated_numbers = list(filter(lambda x: x not in row_values[row], possible_numbers[item]))
                updated_numbers = list(filter(lambda x: x not in col_values[col], updated_numbers))
                possible_numbers[i] = updated_numbers
                for number in updated_numbers:
                    numbers_count[number] += 1
                # print(row_values[row] + col_values[col])
                # print(updated_numbers)
                # self.numbers[row][col] = updated_numbers.pop()
                # col_values[col].append(self.numbers[row][col])
                # row_values[row].append(self.numbers[row][col])

            ordered_possible_numbers = [(key, possible_numbers[key]) for key in sorted(possible_numbers, key=lambda x: len(possible_numbers[x]), reverse=True)]
            # print("ordered_possible_numbers", ordered_possible_numbers)
            # print("numbers_count", numbers_count)
            # print("row_values", row_values)
            # print("col_values", col_values)
            for key, values in ordered_possible_numbers:
                # print(key, values)
                if lst[0] == 0 and self.board[item][key] == CellValue.WHITE and self.numbers[item][key] == 0: row, col = item, key
                elif lst[0] == 1 and self.board[key][item] == CellValue.WHITE and self.numbers[key][item] == 0: row, col = key, item
                else: continue
                updated_values = list(filter(lambda x: x not in row_values[row], values))
                updated_values = list(filter(lambda x: x not in col_values[col], updated_values))
                least_occurring = min(updated_values, key=lambda x: numbers_count[x])
                self.numbers[row][col] = least_occurring
                col_values[col].append(least_occurring)
                row_values[row].append(least_occurring)
            # print(self.grid)
                            

        black_cells = [(i, j) for i in range(self.size) for j in range(self.size) if self.board[i][j] == CellValue.BLACK]
        for row, col in black_cells:
            row_white_values = [self.numbers[row][i] for i in range(self.size) if self.board[row][i] == CellValue.WHITE]
            col_white_values = [self.numbers[i][col] for i in range(self.size) if self.board[i][col] == CellValue.WHITE]
            possible_values = row_white_values + col_white_values
            print("possible_values", possible_values)
            random.shuffle(possible_values)
            self.numbers[row][col] = possible_values[0]

        # for row in range(self.size):
        #     number_range = list(range(1, self.size+1))
        #     number_range = list(filter(lambda x: x not in column_numbers[row], number_range))
        #     random.shuffle(number_range)
        #     for col in range(self.size):
        #         if self.board[row][col] == CellValue.WHITE:
        #             # assign a random possible number
        #             self.numbers[row][col] = number_range.pop()
        #             column_numbers[col].append(self.numbers[row][col])
        pass
    
    def is_black_cell_valid(self, row, col):
        if row > 0 and self.board[row-1][col] == CellValue.BLACK:
            return False
        if row < self.size-1 and self.board[row+1][col] == CellValue.BLACK:
            return False
        if col > 0 and self.board[row][col-1] == CellValue.BLACK:
            return False
        if col < self.size-1 and self.board[row][col+1] == CellValue.BLACK:
            return False
        return True


def read_hitori_example(file_name):
    current_dir = os.path.dirname(__file__)
    file_path = os.path.join(current_dir, file_name)
    with open(file_path, 'r') as file:
        lines = file.readlines()
        example = []
        for line in lines:
            row = line.strip().split()
            row = [CellValue.BLACK if cell == "X" else CellValue.WHITE for cell in row]
            example.append(row)
    return example

if __name__ == "__main__":
    # hitori = HitoriGenerator(10)
    hitori = HitoriGenerator(size=10, black_whites=read_hitori_example("black_whites/10.txt"))
    print(hitori)
    hitori.assign_cell_numbers()
    # print(hitori.grid)

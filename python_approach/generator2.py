# Hitori Generator

# Size NxN

# 1. From 0,0 randomly put next cell
# 2. If next cell is already in the same row or column, with some probability set white cell of those
# 3. Repeat 1 and 2 until all cells are filled

import random
import copy
import enum
import sys
sys.setrecursionlimit(10000)

class CellValue(enum.Enum):
    WHITE = 0
    BLACK = 1

    def __str__(self):
        return "X" if self == CellValue.BLACK else "O"

class HitoriGenerator:
    def __init__(self, size):
        self.size = size
        self.board = None
        self.visited = [[False for i in range(size)] for j in range(size)]
        self.board = [[0 for i in range(size)] for j in range(size)]
        self.grid = [[0 for i in range(size)] for j in range(size)]
        self.fill_board(0, 0)
        while not self.all_white_cells_connected():
            self.fix_non_connected_cells()

    def __str__(self):
        result = ""
        for row in self.board:
            for cell in row:
                result += str(cell) + " "
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
        # for row in range(self.size):
        #     number_range = range(1, self.size+1)
        #     random.shuffle(number_range)
        #     for col in range(self.size):
        #         if self.board[row][col] == CellValue.WHITE:
        #             # assign a random possible number
        #             cell_numbers[row][col] = number_range.pop()
        #             pass
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

if __name__ == "__main__":
    hitori = HitoriGenerator(20)
    print(hitori)

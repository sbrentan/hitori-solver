from enum import Enum
from log import Log, LogLevel

log = Log(LogLevel.INFO)

class CellState(Enum):
    WHITE = 0
    BLACK = 1
    UNKNOWN = 2

    def __str__(self):
        return 'X' if self == CellState.BLACK else 'O' if self == CellState.WHITE else '?'
    
class HitoriState(Enum):
    SOLVED = 0
    VALID = 1
    INVALID = 2


class HitoriSolution:
    def __init__(self, hitori: list[list[int]]):
        self.hitori: list[list[int]] = hitori
        self.rows: int = len(hitori)
        self.cols: int = len(hitori[0])
        self.solution: list[list[CellState]] = [[CellState.UNKNOWN for _ in range(self.cols)] for _ in range(self.rows)]

        self.unkown_count: int
        self.rows_count: list[dict[int, int]]
        self.cols_count: list[dict[int, int]]
        self.visited: list[list[bool]]
        self._set_initial_counts()

    def __str__(self):
        return self._solution_to_str()
    
    def _solution_to_str(self, current_cell: tuple[int, int] = None) -> str:
        result = ''
        for i in range(self.rows):
            for j in range(self.cols):
                if current_cell and i == current_cell[0] and j == current_cell[1]:
                    result += f'[{self[i, j]}] '
                else:
                    result += f' {self[i, j]}  '
            result += '\n'
        return result
    
    def __getitem__(self, key: tuple[int, int]) -> CellState:
        return self.solution[key[0]][key[1]]
    
    def __setitem__(self, key: tuple[int, int], value: CellState) -> None:
        self.solution[key[0]][key[1]] = value
    
    def _set_initial_counts(self) -> None:
        self.unkown_count = 0
        self.rows_count = [{i: row.count(i) for i in set(row)} for row in self.hitori]
        self.cols_count = [{i: col.count(i) for i in set(col)} for col in [[row[i] for row in self.hitori] for i in range(self.cols)]]
        self.visited = [[False for _ in range(self.cols)] for _ in range(self.rows)]
    
    def _is_valid(self, i: int, j: int) -> bool:
        if self.visited[i][j]:
            return True
        self.visited[i][j] = True
        value = self.hitori[i][j]
        cell_state = self.solution[i][j]
        log.debug('--------------------------------------')
        log.debug(f'visiting {i}, {j} with value {value} and state {cell_state}')

        log.debug(self._solution_to_str((i, j)))  # TODO: better display the visited cell

        # check if the cell is black and has a neighbour black cell
        if cell_state == CellState.BLACK:
            self.rows_count[i][value] -= 1
            self.cols_count[j][value] -= 1
            if i < self.rows - 1 and self.solution[i+1][j] == CellState.BLACK:
                log.debug('false on down black')
                return False
            if j < self.cols - 1 and self.solution[i][j+1] == CellState.BLACK:
                log.debug('false on right black')
                return False
            # is it needed to check for up and left?
            if i > 0 and self.solution[i-1][j] == CellState.BLACK:
                log.debug('false on up black')
                return False
            if j > 0 and self.solution[i][j-1] == CellState.BLACK:
                log.debug('false on left black')
                return False
        if cell_state == CellState.WHITE or cell_state == CellState.UNKNOWN or (i == 0 and j == 0):
            if cell_state == CellState.UNKNOWN:
                self.rows_count[i][value] -= 1
                self.cols_count[j][value] -= 1
                self.unkown_count += 1
            
            # visit recursively the neighbours
            if i < self.rows - 1 and not self._is_valid(i+1, j):
                log.debug('not valid down')
                return False
            if j < self.cols - 1 and not self._is_valid(i, j+1):
                log.debug('not valid right')
                return False
            if i > 0 and not self._is_valid(i-1, j):
                log.debug('not valid up')
                return False
            if j > 0 and not self._is_valid(i, j-1):
                log.debug('not valid left')
                return False
        log.debug(f'valid {i}, {j}')
        return True
    
    @property
    def state(self) -> HitoriState:
        log.debug('================= Validating solution =================')
        log.debug(self)
        self._set_initial_counts()
        valid = self._is_valid(0, 0)
        # check rows and columns count are all 1
        for i in range(len(self.rows_count)):
            for count in self.rows_count[i].values():
                if count > 1:
                    log.debug('invalid row count for row [{i}]: {row}'.format(i=i, row=self.rows_count[i]))
                    return HitoriState.INVALID
        for j in range(len(self.cols_count)):
            for count in self.cols_count[j].values():
                if count > 1:
                    log.debug('invalid col count for col [{j}]: {col}'.format(j=j, col=self.cols_count[j]))
                    return HitoriState.INVALID
                
        # check if all cells are visited
        for i in range(self.rows):
            for j in range(self.cols):
                if not self.visited[i][j]:
                    log.debug('not visited {i}, {j}'.format(i=i, j=j))
                    return HitoriState.INVALID

        if not valid:
            return HitoriState.INVALID
        return HitoriState.SOLVED if self.unkown_count == 0 else HitoriState.VALID


class HitoriSolver:
    def __init__(self, hitori):
        self.hitori = hitori
        self.rows = hitori
        self.cols = [[row[i] for row in hitori] for i in range(len(hitori[0]))]
        self.solution = HitoriSolution(hitori)

        self.sorted_row_indexes = {}
        for i in range(len(self.rows)):
            self.sorted_row_indexes[i] = sorted(range(len(self.rows[i])), key=lambda x: self.rows[i][x])
        self.sorted_col_indexes = {}
        for i in range(len(self.cols)):
            self.sorted_col_indexes[i] = sorted(range(len(self.cols[i])), key=lambda x: self.cols[i][x])

    def __str__(self):
        return self._hitori_to_str()
    
    def __getitem__(self, key: tuple[int, int]) -> int:
        return self.hitori[key[0]][key[1]]
    
    def _hitori_to_str(self, current_cell: tuple[int, int] = None) -> str:
        result = ''
        for i in range(len(self.rows)):
            for j in range(len(self.cols)):
                if current_cell and i == current_cell[0] and j == current_cell[1]:
                    result += f'{self[i, j]}] '
                else:
                    result += f' {self[i, j]}  '
            result += '\n'
        result += '\n'
        return result
    
    def _pruning(self, rows_count, cols_count):
        log.info('================= Performing pruning')
        black_cells = []
        for i in range(len(self.rows)):
            for j in range(len(self.cols)):
                value = self.hitori[i][j]

                # Rule 1: if a cell contains a number unique in the row and column,
                #         it must be white (must be applied to full rows/columns)
                if self.solution[i, j] == CellState.UNKNOWN:
                    if rows_count[i][value] <= 1 and cols_count[j][value] <= 1:
                        self.solution[i, j] = CellState.WHITE
                    
                # Rule 2: if a cell and its two following cells in the row or column contain the same number,
                #         the cell between them must be white and all the other occurrences black
                #         IGNORES unknown cells
                if i < len(self.rows) - 2 and self.hitori[i+1][j] == self.hitori[i+2][j] == value:
                    for k in range(len(self.cols)):
                        if self.hitori[k][j] == value:
                            self.solution[k, j] = CellState.BLACK
                            black_cells.append((k, j))
                    self.solution[i+1, j] = CellState.WHITE
                if j < len(self.cols) - 2 and self.hitori[i][j+1] == self.hitori[i][j+2] == value:
                    for k in range(len(self.rows)):
                        if self.hitori[i][k] == value:
                            self.solution[i, k] = CellState.BLACK
                            black_cells.append((i, k))
                    self.solution[i, j+1] = CellState.WHITE

        # Rule 3: in a corner, if the three neighbouring cells contain the same number,
        #           the cells in the diagonal must be black and the other cell white
        #           All other occurrences of the number must be black
        #           IGNORES unknown cells
        points_to_check = [(0, 0), (0, len(self.cols)-2), (len(self.rows)-2, 0), (len(self.rows)-2, len(self.cols)-2)]
        for i, j in points_to_check:
            if (self.hitori[i][j] == self.hitori[i][j+1] and self.hitori[i+1][j] == self.hitori[i+1][j+1] or 
                self.hitori[i][j] == self.hitori[i+1][j] and self.hitori[i][j+1] == self.hitori[i+1][j+1]):
                value1 = self.hitori[i][j]
                value2 = self.hitori[i+1][j+1]
                for k in range(len(self.rows)):
                    if self.hitori[k][j] == value1:
                        self.solution[k, j] = CellState.BLACK
                        black_cells.append((k, j))
                    if self.hitori[k][j+1] == value2:
                        self.solution[k, j+1] = CellState.BLACK
                        black_cells.append((k, j+1))
                for k in range(len(self.cols)):
                    if self.hitori[i][k] == value1:
                        self.solution[i, k] = CellState.BLACK
                        black_cells.append((i, k))
                    if self.hitori[i+1][k] == value2:
                        self.solution[i+1, k] = CellState.BLACK
                        black_cells.append((i+1, k))
                self.solution[i, j] = CellState.WHITE
                self.solution[i+1, j+1] = CellState.WHITE

        # Rule 4: all the neighbouring cells of a black cell must be white
        #         IGNORES unknown cells
        for i, j in black_cells:
            if i < len(self.rows) - 1:
                self.solution[i+1, j] = CellState.WHITE
            if j < len(self.cols) - 1:
                self.solution[i, j+1] = CellState.WHITE
            if i > 0:
                self.solution[i-1, j] = CellState.WHITE
            if j > 0:
                self.solution[i, j-1] = CellState.WHITE

        log.info(self.solution)
                

    def test(self):
        # ex1
        # self.solution[0, 2] = CellState.BLACK
        # self.solution[2, 1] = CellState.BLACK

        # ex2
        for i in range(len(self.rows)):
            for j in range(len(self.cols)):
                self.solution[i, j] = CellState.WHITE
        self.solution[0, 0] = CellState.BLACK
        self.solution[1, 2] = CellState.BLACK
        self.solution[1, 4] = CellState.BLACK
        self.solution[2, 1] = CellState.BLACK
        self.solution[3, 4] = CellState.BLACK
        self.solution[4, 0] = CellState.BLACK
        self.solution[4, 2] = CellState.BLACK

        log.info(self.solution.state)


    def combinations_for_row(self, row_id: int = 0, local_position: int = 0) -> list[dict[int, int]]:
        if local_position == len(self.sorted_row_indexes[row_id]):
            # TODO: check if count correct?
            return {}
    
        indexes = self.sorted_row_indexes[row_id]
        
        i = local_position
        idx = indexes[i]
        # if not last cell, check next item if it is the same
        value = self.hitori[row_id][idx]
        next_value = None
        if i < len(indexes) - 1:
            next_value = self.hitori[row_id][indexes[i+1]]

        result_white_selections: list[dict[int, int]] = []
        log.debug(f'row {row_id} idx {idx} value {value}')
        if value == next_value:# or value == prev_value:  # to avoid using the sum values
            log.debug('in')
            forced_solution = None
            for k in range(i, len(indexes) + 1):
                # TODO: check if black in position row_id, indexes[k] is valid, otherwise is a forced solution
                if False: # forced solution
                    forced_solution = k  # TODO: check
                if k == len(indexes) or self.hitori[row_id][indexes[k]] != value:
                    break

            log.debug("k", k)
            next_white_selections = self.combinations_for_row(row_id, k)
            
            # Get my solutions
            if forced_solution:
                start_range = forced_solution
                end_range = forced_solution + 1
            else:
                start_range = i
                end_range = k

            log.debug("checking from {i} to {k}".format(i=i, k=k))
            for j in range(start_range, end_range):
            
                # from i to k, set solutions
                if next_white_selections:
                    for next_white_selection in next_white_selections:
                        log.debug("---", {value: indexes[j]})
                        result_white_selections.append({value: indexes[j], **next_white_selection})
                else:
                    result_white_selections.append({value: indexes[j]})
            log.debug("result_white_selections", result_white_selections)
            return result_white_selections
        else:
            r = self.combinations_for_row(row_id, i+1)
            log.debug(f"{local_position}: r for {row_id} and {i+1}", r)
            return r
            
    def aggregate_solutions(self, row, row2, solutions: list[dict[int, int]], solutions2: list[dict[int, int]]) -> list[dict[int, dict[int, int]]]:
        # this function does not check for previously set black cells (directly on the hitori)
        # it is expected that the generated solutions are valid

        # !!=== CHANGE THE AGGREGATION TO MANAGE MULTIPLE ROWS AND NOT ONLY 2 ===!!

        result = []
        for solution in solutions:
            for solution2 in solutions2:
                skip = False
                for k, v in solution.items():
                    # If same value in the same column, break
                    if k in solution2 and solution2[k] == v:
                        skip = True
                        break
                if skip: continue

                # check if any black cell is in the same column
                for i in range(len(self.hitori[row])):
                    value = self.hitori[row][i]
                    value2 = self.hitori[row2][i]
                    if value in solution and solution[value] != i:  # i for solution is black
                        if value2 in solution2 and solution2[value2] != i:  # i for solution2 is black
                            skip = True
                            break
                if skip: continue

                new_result = {
                    row: solution,
                    row2: solution2
                }
                result.append(new_result)
        return result

    def solve(self):
        log.info('================= Solving Hitori =================')
        log.info(self)

        rows_count = [{i: row.count(i) for i in set(row)} for row in self.hitori]
        cols_count = [{i: col.count(i) for i in set(col)} for col in [[row[i] for row in self.hitori] for i in range(len(self.cols))]]
        self._pruning(rows_count, cols_count)

        # keep only row values bigger than 1
        rows_count = [{k: v for k, v in row.items() if v > 1} for row in rows_count]
        log.debug(rows_count)
        # multiply values
        combinations = 1
        s = 0
        for row in rows_count:
            for _, count in row.items():
                combinations *= count
                s += count
        log.debug("combinations", combinations)
        log.debug("sum", s)
        log.debug()

        log.info("sorted_row_indexes", self.sorted_row_indexes)
        log.info('\n================= Combinations =================')
        solutions = self.combinations_for_row()
        solutions2 = self.combinations_for_row(1)
        solutions3 = self.combinations_for_row(2)
        solutions4 = self.combinations_for_row(3)
        log.info("\ncombinations for row 1:\n", solutions)
        log.info("\ncombinations for row 2:\n", solutions2)
        log.info("\ncombinations for row 3:\n", solutions3)
        log.info("\ncombinations for row 4:\n", solutions4)

        aggregations = self.aggregate_solutions(0, 1, solutions, solutions2)
        aggregations2 = self.aggregate_solutions(2, 3, solutions3, solutions4)
        log.info("\naggregate_solutions for row 1 and 2:\n", aggregations)
        log.info("\naggregate_solutions for row 3 and 4:\n", aggregations2)

        aggr = aggregations[0]
        log.info("\naggr", aggr)

        # check if any black cell is in the same column
        for row in range(2):
            for col in range(len(self.hitori[row])):
                value = self.hitori[row][col]
                print(row, col, value, aggr[row])
                if value in aggr[row]:
                    if aggr[row][value] == col:
                        self.solution[row, col] = CellState.WHITE
                    else:
                        self.solution[row, col] = CellState.BLACK

        aggr = aggregations2[0]
        log.info("\naggr", aggr)

        # check if any black cell is in the same column
        for row in range(2, 4):
            for col in range(len(self.hitori[row])):
                value = self.hitori[row][col]
                print(row, col, value, aggr[row])
                if value in aggr[row]:
                    if aggr[row][value] == col:
                        self.solution[row, col] = CellState.WHITE
                    else:
                        self.solution[row, col] = CellState.BLACK

        log.info('\n\n================= Solution =================')
        log.info(self.solution)
        log.info(self.solution.state)
        

ex1 = [
    [3, 2, 2],
    [1, 3, 2],
    [2, 3, 1]
]
ex2 = [
    [1, 1, 2, 5, 4],
    # [1, 1, 2, 2, 4],
    [2, 3, 3, 1, 2],
    [1, 3, 4, 3, 5],
    [3, 4, 1, 2, 3],
    [1, 5, 2, 4, 1]
]
HitoriSolver(ex2).solve()

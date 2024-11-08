import copy
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

class Combination:
    rows: dict[int, dict[int, int]]
    min_row: int
    max_row: int

    def __init__(self, rows: dict[int, dict[int, int]]):
        self.rows = rows
        self.min_row = min(rows.keys())
        self.max_row = max(rows.keys())

    def __getitem__(self, key: tuple[int, int]) -> int:
        return self.rows[key[0]][key[1]]
    
    def __str__(self):
        return str(self.rows)
    
    @staticmethod
    def combinations_to_str(combinations) -> str:
        return str(len(combinations)) + '\n' + '\n'.join([str(c) for c in combinations])
    

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


    def combinations_for_row(self, row_id: int = 0, local_position: int = 0, transposed: bool = False) -> list[dict[int, int]]:
    
        indexes = self.sorted_row_indexes[row_id]

        local_hitori = self.hitori
        if transposed:
            local_hitori = self.cols
            indexes = self.sorted_col_indexes[row_id]

        if local_position == len(indexes):
            # TODO: check if count correct?
            return {}
        
        i = local_position
        idx = indexes[i]
        # if not last cell, check next item if it is the same
        value = local_hitori[row_id][idx]
        next_value = None
        if i < len(indexes) - 1:
            next_value = local_hitori[row_id][indexes[i+1]]

        result_white_selections: list[dict[int, int]] = []
        log.debug(f'row {row_id} idx {idx} value {value}')
        if value == next_value:# or value == prev_value:  # to avoid using the sum values
            log.debug('in')
            forced_solution = None
            skip_blacks = []
            for k in range(i, len(indexes) + 1):
                if k == len(indexes) or local_hitori[row_id][indexes[k]] != value:
                    break

                idx_k = indexes[k]
                if not forced_solution:
                    # TODO: check if black in position row_id, indexes[k] is valid, otherwise is a forced solution
                    #       or if the hitori cell is WHITE
                    #       of if the hitori cell is BLACK, add k to skip_blacks

                    if not transposed:
                        if self.solution[row_id, idx_k] == CellState.WHITE:
                            forced_solution = k
                        elif self.solution[row_id, idx_k] == CellState.BLACK:
                            skip_blacks.append(k)
                        else:
                            if row_id > 0 and self.solution[row_id - 1, idx_k] == CellState.BLACK:
                                forced_solution = k
                            elif row_id < len(local_hitori) - 1 and self.solution[row_id + 1, idx_k] == CellState.BLACK:
                                forced_solution = k
                            elif idx_k > 0 and self.solution[row_id, idx_k - 1] == CellState.BLACK:
                                forced_solution = k
                            elif idx_k < len(local_hitori[0]) - 1 and self.solution[row_id, idx_k + 1] == CellState.BLACK:
                                forced_solution = k
                    else:
                        if self.solution[idx_k, row_id] == CellState.WHITE:
                            forced_solution = k
                        elif self.solution[idx_k, row_id] == CellState.BLACK:
                            skip_blacks.append(k)
                        else:
                            if row_id > 0 and self.solution[idx_k, row_id - 1] == CellState.BLACK:
                                forced_solution = k
                            elif row_id < len(local_hitori) - 1 and self.solution[idx_k, row_id + 1] == CellState.BLACK:
                                forced_solution = k
                            elif idx_k > 0 and self.solution[idx_k - 1, row_id] == CellState.BLACK:
                                forced_solution = k
                            elif idx_k < len(local_hitori[0]) - 1 and self.solution[idx_k + 1, row_id] == CellState.BLACK:
                                forced_solution = k


            log.debug("k", k)
            next_white_selections = self.combinations_for_row(row_id, k, transposed=transposed)
            
            # Get my solutions
            if forced_solution:
                start_range = forced_solution
                end_range = forced_solution + 1
            else:
                start_range = i
                end_range = k

            log.debug("checking from {i} to {k}".format(i=i, k=k))
            for j in range(start_range, end_range):
                if j in skip_blacks:
                    continue
            
                # from i to k, set solutionsx0-90
                if next_white_selections:
                    for next_white_selection in next_white_selections:
                        log.debug("---", {value: indexes[j]})
                        result_white_selections.append({value: indexes[j], **next_white_selection})
                else:
                    result_white_selections.append({value: indexes[j]})
            log.debug("result_white_selections", result_white_selections)
            return result_white_selections
        else:
            r = self.combinations_for_row(row_id, i+1, transposed=transposed)
            log.debug(f"{local_position}: r for {row_id} and {i+1}", r)
            return r

    def aggregate_multi(self, combinations: list[list[Combination]], local_position: int = 0, transposed: bool = False) -> list[Combination]:
        # expects a list of adjacent rows combinations ([{0, 1}, {0, 1}], [{2, 3}], ....)
        # expects non overlapping combinations
        if local_position == len(combinations):
            return []
        local_combinations = []
        my_combinations = combinations[local_position]
        next_combinations = self.aggregate_multi(combinations, local_position + 1)

        local_hitori = self.hitori
        if transposed:
            local_hitori = self.cols

        if next_combinations:
            for next_combination in next_combinations:
                for my_combination in my_combinations:
                    skip = False

                    # check value sums vertically
                    # ========== CANT CHECK BECAUSE WE NEED TO CONSIDER UNKNOWNS ==========
                    # for col in range(len(self.hitori[0])):
                    #     values_to_check = []
                    #     for row in my_combination.rows.keys():
                    #         values_to_check.append(self.hitori[row][col])
                    #     for row in next_combination.rows.keys():
                    #         if self.hitori[row][col] in values_to_check:
                    #             skip = True
                    #             break
                    #     if skip: break
                    # if skip: continue

                    # check if any black cell is in the same column
                    row1 = my_combination.max_row
                    row2 = next_combination.min_row
                    # if row1 + 1 != row2:
                    #     raise Exception("Invalid combination rows " + str(row1) + " " + str(row2))
                    for i in range(len(local_hitori[row1])):
                        value = local_hitori[row1][i]
                        value2 = local_hitori[row2][i]
                        a = my_combination.rows[row1]
                        # i for row is black
                        if self.solution[row1, i] == CellState.BLACK or (value in my_combination.rows[row1] and my_combination[row1, value] != i):
                            # i for row2 is black
                            if self.solution[row2, i] == CellState.BLACK or (value2 in next_combination.rows[row2] and next_combination[row2, value2] != i):
                                skip = True
                                break
                    if skip:
                        continue

                    # union of combinations
                    new_combination_rows = {**my_combination.rows, **next_combination.rows}
                    new_combination = Combination(new_combination_rows)
                    local_combinations.append(new_combination)
            return local_combinations
        else:
            return my_combinations
        

    def test_combinations(self):
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


    def process_cells(self, transposed: bool = False) -> list[Combination]:
        log.info('\n================= Row Combinations =================')

        # divide rows by three
        # step = 4
        # list_to_check = self.hitori
        # temp = True
        # while(len(list_to_check) > step):
        #     multi = []
        #     print('a')
        #     for i in range(0, len(list_to_check) - 1, step):
        #         if temp:
        #             sol = self.combinations_for_row(i, transposed=transposed)
        #             sol = [Combination({i: s}) for s in sol]
        #             sol2 = self.combinations_for_row(i + 1, transposed=transposed)
        #             sol2 = [Combination({i+1: s}) for s in sol2]
        #             multi.append(self.aggregate_multi([sol, sol2]))
        #         else:
        #             multi.append(self.aggregate_multi(list_to_check[i:i+step]))
        #     temp = False
        #     list_to_check = multi

        # multi_multi = self.aggregate_multi(list_to_check, transposed=transposed)
        # if len(self.hitori) % 2 != 0:
        #     last_sol = self.combinations_for_row(len(self.hitori) - 1, transposed=transposed)
        #     last_sol = [Combination({len(self.hitori) - 1: s}) for s in last_sol]
        #     mmm = self.aggregate_multi([multi_multi, last_sol])
        # else:
        #     mmm = list_to_check




        

        solutions = self.combinations_for_row(transposed=transposed)
        solutions2 = self.combinations_for_row(1, transposed=transposed)
        solutions3 = self.combinations_for_row(2, transposed=transposed)
        solutions4 = self.combinations_for_row(3, transposed=transposed)
        solutions5 = self.combinations_for_row(4, transposed=transposed)
        log.debug("\ncombinations for row 1:\n", solutions)
        log.debug("\ncombinations for row 2:\n", solutions2)
        log.debug("\ncombinations for row 3:\n", solutions3)
        log.debug("\ncombinations for row 4:\n", solutions4)
        log.debug("\ncombinations for row 5:\n", solutions5)

        solutions = [Combination({0: s}) for s in solutions]
        solutions2 = [Combination({1: s}) for s in solutions2]
        multi = self.aggregate_multi([solutions, solutions2])
  
        solutions3 = [Combination({2: s}) for s in solutions3]
        solutions4 = [Combination({3: s}) for s in solutions4]
        multi2 = self.aggregate_multi([solutions3, solutions4])

        multi_multi = self.aggregate_multi([multi, multi2])

        solutions5 = [Combination({4: s}) for s in solutions5]
        mmm = self.aggregate_multi([multi_multi, solutions5])
        log.info("\naggregate_multi:\n", Combination.combinations_to_str(mmm))
        return mmm





        # solutions = self.combinations_for_row(transposed=transposed)
        # solutions2 = self.combinations_for_row(1, transposed=transposed)
        # solutions3 = self.combinations_for_row(2, transposed=transposed)
        # solutions4 = self.combinations_for_row(3, transposed=transposed)
        # solutions5 = self.combinations_for_row(4, transposed=transposed)
        # solutions6 = self.combinations_for_row(4, transposed=transposed)
        # solutions7 = self.combinations_for_row(4, transposed=transposed)
        # solutions8 = self.combinations_for_row(4, transposed=transposed)
        # solutions9 = self.combinations_for_row(4, transposed=transposed)
        # log.debug("\ncombinations for row 1:\n", solutions)
        # log.debug("\ncombinations for row 2:\n", solutions2)
        # log.debug("\ncombinations for row 3:\n", solutions3)
        # log.debug("\ncombinations for row 4:\n", solutions4)
        # log.debug("\ncombinations for row 5:\n", solutions5)
        # log.debug("\ncombinations for row 6:\n", solutions6)
        # log.debug("\ncombinations for row 7:\n", solutions7)
        # log.debug("\ncombinations for row 8:\n", solutions8)
        # log.debug("\ncombinations for row 9:\n", solutions9)

        # solutions = [Combination({0: s}) for s in solutions]
        # solutions2 = [Combination({1: s}) for s in solutions2]
        # multi = self.aggregate_multi([solutions, solutions2])
  
        # solutions3 = [Combination({2: s}) for s in solutions3]
        # solutions4 = [Combination({3: s}) for s in solutions4]
        # multi2 = self.aggregate_multi([solutions3, solutions4])

        # multi_multi = self.aggregate_multi([multi, multi2])

        # solutions5 = [Combination({4: s}) for s in solutions5]
        # solutions6 = [Combination({5: s}) for s in solutions6]
        # multi3 = self.aggregate_multi([solutions5, solutions6])
  
        # solutions7 = [Combination({6: s}) for s in solutions7]
        # solutions8 = [Combination({7: s}) for s in solutions8]
        # multi4 = self.aggregate_multi([solutions7, solutions8])

        # multi_multi = self.aggregate_multi([multi3, multi4])

        # mmm = self.aggregate_multi([multi_multi, multi_multi])

        # solutions9 = [Combination({8: s}) for s in solutions9]
        # mmm2 = self.aggregate_multi([mmm, solutions9])
        # log.info("\naggregate_multi:\n", Combination.combinations_to_str(mmm2))

        # return mmm2


    def validate_combination(self, combination: Combination, transposed: bool = False) -> bool:
        local_hitori = self.hitori
        if transposed:
            local_hitori = self.cols

        solution_copy = copy.deepcopy(self.solution)

        # check if any black cell is in the same column
        for row in range(len(local_hitori)):
            for col in range(len(local_hitori[row])):
                value = local_hitori[row][col]
                if row in combination.rows and value in combination.rows[row]:
                    if combination[row, value] == col:
                        if not transposed: solution_copy[row, col] = CellState.WHITE
                        else: solution_copy[col, row] = CellState.WHITE
                    else:
                        if not transposed: solution_copy[row, col] = CellState.BLACK
                        else: solution_copy[col, row] = CellState.BLACK

        log.info("Combination state", solution_copy.state)
        log.info(solution_copy)


    def solve(self):
        log.info('================= Solving Hitori =================')
        log.info(self)

        rows_count = [{i: row.count(i) for i in set(row)} for row in self.hitori]
        cols_count = [{i: col.count(i) for i in set(col)} for col in [[row[i] for row in self.hitori] for i in range(len(self.cols))]]
        self._pruning(rows_count, cols_count)
        
        row_combinations = self.process_cells()
        col_combinations = self.process_cells(transposed=True)

        for row_combination in row_combinations:
            # create a copy of the solution
            copy_solution = HitoriSolution(self.hitori)
            for i in range(self.solution.rows):
                for j in range(self.solution.cols):
                    if self.solution[i, j] == CellState.UNKNOWN:
                        # apply row_combination white and black cells
                        value = self.hitori[i][j]
                        if value in row_combination.rows[i]:
                            if row_combination[i, value] == j:
                                copy_solution[i, j] = CellState.WHITE
                            else:
                                copy_solution[i, j] = CellState.BLACK
                    else:
                        copy_solution[i, j] = self.solution[i, j]
            # print("row_solution\n" + str(copy_solution))
            # checking if col_combination fits row_combination
            for col_combination in col_combinations:
                skip = False
                moves = []
                for col, col_values in col_combination.rows.items():
                    for i in range(len(self.hitori)):
                        value = self.hitori[i][col]
                        if value in col_values:
                            col_value = col_values[value]  # index of white element with value=value
                            if col_value == i:
                                col_combination_value = CellState.WHITE
                            else:
                                col_combination_value = CellState.BLACK
                            if copy_solution[i, col] == CellState.UNKNOWN:
                                copy_solution[i, col] = col_combination_value
                                moves.append((i, col, col_combination_value))
                            elif copy_solution[i, col] != col_combination_value:
                                    skip = True
                                    break
                    if skip: break
                if skip: continue
                for move in moves:
                    copy_solution[move[0], move[1]] = move[2]
                if not skip:
                    print("copy_solution\n" + str(copy_solution))
                    print("state", copy_solution.state)
                    input()

        log.info('\n\n================= Solution =================')
        self.validate_combination(row_combinations[12])  # 2, 5, 11, 12
        self.validate_combination(col_combinations[5], transposed=True)  # 5, 13


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

"""
ex2 soluion:
    X O O O O
    O O X O X
    O X O O O
    O O O O X
    X O X O O
"""

ex3 = [
    [3, 9, 2, 4, 7, 7, 8, 1, 6],
    [9, 5, 1, 2, 6, 8, 7, 4, 2],
    [4, 9, 3, 8, 3, 7, 7, 5, 1],
    [1, 7, 6, 4, 1, 5, 1, 9, 1],
    [1, 9, 9, 7, 8, 6, 5, 3, 2],
    [7, 8, 1, 4, 3, 1, 9, 6, 9],
    [8, 9, 7, 5, 9, 3, 1, 8, 4],
    [6, 2, 9, 1, 5, 1, 3, 8, 8],
    [7, 1, 8, 6, 9, 2, 9, 7, 3],
]

ex4 = [
    [1, 1, 5, 4, 4],
    [5, 1, 2, 3, 2],
    [2, 5, 3, 1, 3],
    [2, 4, 1, 2, 5],
    [4, 4, 2, 5, 3],
]

"""
ex2 soluion:
    O X O O X
    O O X O O
    X O O O X
    O O O X O
    O X O O O

{0: {1: 1, 4: 4}, 1: {2: 2}, 2: {3: 4}, 3: {2: 3}, 4: {4: 1}} row aggregation not present
"""
HitoriSolver(ex4).solve()


class Grid {
  construct new() { _cells = {} }
  label=(value as String) { _label = value }
  label as String { _label }
  [x as Num, y as Num] as String or Null { _cells["%(x),%(y)"] }
  [x as Num, y as Num]=(value as String) { _cells["%(x),%(y)"] = value }
}

var grid = Grid.new()
grid.label = "board"
System.print(grid.label) // expect: board
grid[1, 2] = "X"
System.print(grid[1, 2]) // expect: X
System.print(grid[0, 0]) // expect: null

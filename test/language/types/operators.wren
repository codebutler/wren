class Vec {
  construct new(x, y) {
    _x = x
    _y = y
  }
  x { _x }
  y { _y }
  +(other as Vec) as Vec { Vec.new(_x + other.x, _y + other.y) }
  - as Vec { Vec.new(-_x, -_y) }
  -(other as Vec) as Vec { this + -other }
  ! as Bool { _x == 0 && _y == 0 }
  ==(other as Vec) as Bool { other is Vec && _x == other.x && _y == other.y }
  toString as String { "(%(_x), %(_y))" }
}

var a = Vec.new(1, 2)
var b = Vec.new(3, 5)
System.print(a + b) // expect: (4, 7)
System.print(-a) // expect: (-1, -2)
System.print(b - a) // expect: (2, 3)
System.print(!Vec.new(0, 0)) // expect: true
System.print(a == Vec.new(1, 2)) // expect: true

class Stop {
  foreign static at(line, action, arg)
  foreign static onError(line)
  foreign static result
}

class Inspect {
  foreign static fields(object)
  foreign static entries(map)
}

class Point {
  construct new(x, y) {
    _x = x
    _y = y
  }
  x { _x }
  sum(extra) {
    var total = _x + _y
    return total + extra
  }
}

class Point3 is Point {
  construct new(x, y, z) {
    super(x, y)
    _z = z
  }
  depth(scale) {
    var d = _z * scale
    return d
  }
}

// Evaluating in a method frame: its locals and parameters, this, the class's
// fields and implicit calls on this.
var p = Point.new(1, 2)
Stop.at(20, "eval", "total * 10 + extra + _x + x + this.x")
p.sum(5)
System.print(Stop.result) // expect: 38

// Assigning a local from the frame's scope changes the frame's variable.
Stop.at(20, "exec", "total = 100")
System.print(p.sum(5)) // expect: 105
System.print(Stop.result) // expect: null

// A field the class does not have is a compile error, not a new field.
Stop.at(20, "eval", "_nope")
p.sum(5)
System.print(Stop.result) // expect: compile error: Error at '_nope': Point has no field named '_nope'.

// A runtime error is returned; the program goes on.
Stop.at(20, "eval", "extra.nope")
System.print(p.sum(5)) // expect: 8
System.print(Stop.result) // expect: runtime error: Num does not implement 'nope'.

// A subclass's own fields come after the inherited ones.
var p3 = Point3.new(1, 2, 3)
Stop.at(31, "eval", "[_z, d, scale].toString")
p3.depth(2)
System.print(Stop.result) // expect: [3, 6, 2]
System.print(Inspect.fields(p3)) // expect: _x=1 _y=2 _z=3
System.print(Inspect.entries({"a": 1})) // expect: a=1
System.print(Inspect.entries({})) // expect:

// Top-level code: block locals and module variables; captured variables.
Stop.at(70, "eval", "local + p.x")
{
  var local = 4
  var shown = local
}
System.print(Stop.result) // expect: 5
var outer = 10
var make = Fn.new {|a|
  var captured = a
  return Fn.new {
    return captured + outer
  }
}
Stop.at(77, "eval", "captured * 2")
make.call(7).call()
System.print(Stop.result) // expect: 14

// Set next statement: run lines again.
var trace = []
var again = Fn.new {
  trace.add(1)
  trace.add(2)
  trace.add(3)
}
Stop.at(89, "set", 87)
again.call()
System.print(Stop.result) // expect: moved
System.print(trace) // expect: [1, 2, 1, 2, 3]

// Out of a loop is fine; its locals are dropped.
var leave = Fn.new {
  var kept = "kept"
  for (i in 1..3) {
    var inner = i
  }
  return kept
}
Stop.at(100, "set", 102)
System.print(leave.call()) // expect: kept
System.print(Stop.result) // expect: moved

// Into a loop is not; nor is a line with no statement.
var into = Fn.new {
  var before = 1
  for (i in 1..2) {
    var inner = i
  }
  // a comment
  return before
}
Stop.at(110, "set", 112)
into.call()
System.print(Stop.result) // expect: out-of-scope
Stop.at(110, "set", 114)
into.call()
System.print(Stop.result) // expect: no-statement

// Static fields: the ones the frame's method uses, read and assigned; one
// it does not use is an error, and nothing is left behind in the module.
class Tally {
  static bump() {
    __count = (__count == null ? 0 : __count) + 1
    return __count
  }
  static other() {
    return 0
  }
}
Stop.at(129, "eval", "__count * 10")
Tally.bump()
System.print(Stop.result) // expect: 10
Stop.at(129, "exec", "__count = 41")
System.print(Tally.bump()) // expect: 41
System.print(Stop.result) // expect: null
Stop.at(132, "eval", "__count")
Tally.other()
System.print(Stop.result) // expect: compile error: Error at '__count': Static field '__count' is not used by this method.
Stop.at(132, "eval", "__count")
Tally.other()
System.print(Stop.result) // expect: compile error: Error at '__count': Static field '__count' is not used by this method.

// Nested top-level code from inside the hook.
Stop.at(20, "interpret", "System.print(\"nested\")")
p.sum(0) // expect: nested
System.print(Stop.result) // expect: interpreted

// The error hook sees the failing frame before anything unwinds, and moving
// the frame discards the error.
var risky = Fn.new {
  var a = 1
  a = a.nope
  return "resumed " + a.toString
}
Stop.onError(158)
System.print(risky.call()) // expect: resumed 1
System.print(Stop.result) // expect: [Num does not implement 'nope'. at new(_) block argument:157] a=1 moved

// A caught error does not call the hook.
Stop.onError(0)
Fiber.new { 1.nope }.try()
System.print(Stop.result) // expect:

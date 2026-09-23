class Replace {
  foreign static methods(target, source)
  foreign static otherModule()
}

class Base {
  construct new() {}
  greet() { "base" }
}

class A is Base {
  construct new() { _count = 0 }
  count { _count }
  bump() { _count = _count + 1 }
  label() { "old" }
  greet() { "old " + super.greet() }
  static make() { "old static" }
  static tally { __tally }
  static tally=(value) { __tally = value }
  run() {
    System.print("run 1 " + label())
    System.print(Replace.methods("A", "Running"))
    // This frame is still the old body; calls it makes see the new methods.
    System.print("run 2 " + label())
  }
}

class Sub is A {
  construct new() { super() }
}

class Over is A {
  construct new() { super() }
  label() { "over" }
}

var a = A.new()
var sub = Sub.new()
var over = Over.new()
a.bump()
a.bump()
A.tally = 5

// The replacement: new bodies, the same field, one added method.
class A2 is Base {
  construct new() { _count = 0 }
  count { _count }
  bump() { _count = _count + 10 }
  label() { "new" }
  greet() { "new " + super.greet() }
  added() { "added %(_count)" }
  static make() { "new static" }
  static tally { __tally }
  static tally=(value) { __tally = value }
  run() { System.print("new run") }
}

System.print(Replace.methods("A", "A2")) // expect: success
System.print(a.count) // expect: 2
a.bump()
System.print(a.count) // expect: 12
System.print(a.label()) // expect: new
System.print(a.greet()) // expect: new base
System.print(a.added()) // expect: added 12
System.print(A.make()) // expect: new static
System.print(A.tally) // expect: 5
A.tally = 6
System.print(A.tally) // expect: 6

// A subclass created before the replacement inherits the new methods, added
// ones too; an override stays.
System.print(sub.label()) // expect: new
System.print(sub.added()) // expect: added 0
System.print(over.label()) // expect: over
System.print(over.greet()) // expect: new base

// A new instance gets the new methods too.
System.print(A.new().label()) // expect: new

// A running call keeps its old body.
class Running is Base {
  construct new() { _count = 0 }
  count { _count }
  bump() { _count = _count + 10 }
  label() { "swapped" }
  greet() { "swapped " + super.greet() }
  added() { "added" }
  static make() { "swapped static" }
  static tally { __tally }
  static tally=(value) { __tally = value }
  run() { System.print("swapped run") }
}

class Old is Base {
  construct new() { _count = 0 }
  count { _count }
  bump() { _count = _count + 1 }
  label() { "old" }
  greet() { "old" }
  added() { "added" }
  static make() { "old static" }
  static tally { __tally }
  static tally=(value) { __tally = value }
  run() {
    System.print("run 1 " + label())
    System.print(Replace.methods("A", "Running"))
    System.print("run 2 " + label())
  }
}
System.print(Replace.methods("A", "Old")) // expect: success
a.run()
// expect: run 1 old
// expect: success
// expect: run 2 swapped
a.run() // expect: swapped run

// Rejected replacements change nothing.
class Fields is Base {
  construct new() { _count = 0 }
  count { _count + _other }
}
System.print(Replace.methods("A", "Fields")) // expect: fields changed: _other

class Reordered is Base {
  construct new() { _other = 0 }
  count { _count }
}
System.print(Replace.methods("A", "Reordered")) // expect: fields changed: _other

class Renamed is Base {
  construct new() { _total = 0 }
}
System.print(Replace.methods("A", "Renamed")) // expect: fields changed: _total

class NoFields is Base {}
System.print(Replace.methods("A", "NoFields")) // expect: fields changed: _count

class Parent is Object {
  construct new() { _count = 0 }
}
System.print(Replace.methods("A", "Parent")) // expect: superclass changed

class Removed is Base {
  construct new() { _count = 0 }
  count { _count }
  bump() { _count = _count + 10 }
  greet() { "removed" }
  added() { "added" }
  static make() { "removed static" }
  static tally { __tally }
  static tally=(value) { __tally = value }
  run() {}
}
System.print(Replace.methods("A", "Removed")) // expect: method removed: label()

class Arity is Base {
  construct new() { _count = 0 }
  count { _count }
  bump() { _count = _count + 10 }
  label(prefix) { prefix }
  greet() { "arity" }
  added() { "added" }
  static make() { "arity static" }
  static tally { __tally }
  static tally=(value) { __tally = value }
  run() {}
}
System.print(Replace.methods("A", "Arity")) // expect: method removed: label()

class NoStatic is Base {
  construct new() { _count = 0 }
  count { _count }
  bump() { _count = _count + 10 }
  label() { "no static" }
  greet() { "no static" }
  added() { "added" }
  static tally { __tally }
  static tally=(value) { __tally = value }
  run() {}
}
System.print(Replace.methods("A", "NoStatic")) // expect: method removed: static make()

System.print(a.label()) // expect: swapped
System.print(A.make()) // expect: swapped static

var notAClass = 1
System.print(Replace.methods("A", "notAClass")) // expect: not found
System.print(Replace.methods("A", "Missing")) // expect: not found
System.print(Replace.methods("A", "A")) // expect: unsupported
System.print(Replace.otherModule()) // expect: not found

// Static fields keep their values across every replacement.
System.print(A.tally) // expect: 6

// Subclasses defined in a function can be garbage when the replacement walks
// every class; a live one still inherits an added method.
class G is Base {
  construct new() {}
  label() { "g" }
}
var makeInner = Fn.new {
  class Inner is G {
    construct new() { super() }
  }
  return Inner.new()
}
var inner = makeInner.call()
for (i in 0...50) makeInner.call()

class G2 is Base {
  construct new() {}
  label() { "g2" }
  addedLater() { "later" }
}
System.print(Replace.methods("G", "G2")) // expect: success
System.print(inner.label()) // expect: g2
System.print(inner.addedLater()) // expect: later

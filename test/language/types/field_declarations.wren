// A field declaration types a field; the field is created where methods use
// it, as always. Declared fields that no method uses do not exist.
class Counter {
  _count as Num
  _unused as String or Null
  __instances as Num

  construct new() {
    _count = 0
    __instances = __instances == null ? 1 : __instances + 1
  }

  _label as String

  increment() { _count = _count + 1 }
  count { _count }
  static instances { __instances }
}

class Named is Counter {
  _name as String
  construct new(name) {
    super()
    _name = name
  }
  name { _name }
}

var c = Counter.new()
c.increment()
c.increment()
System.print(c.count) // expect: 2
var n = Named.new("n")
n.increment()
System.print(n.name) // expect: n
System.print(n.count) // expect: 1
System.print(Counter.instances) // expect: 2

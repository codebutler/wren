class Store(of K) {
  construct new() { _keys = [] }
  keep(key as K) { _keys.add(key) }
  keys { _keys }
}

class Registry(of K, V as Object) is Store(of K) {
  construct new() { super() }
}

class Names is Store(of String) {
  construct new() { super() }
}

var r = Registry.new()
r.keep("x")
System.print(r.keys) // expect: [x]
System.print(r is Store) // expect: true
System.print(Names.new() is Store) // expect: true

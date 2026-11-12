class Foo {
  construct new() {}
}

var a = Foo.new()
var b = Foo.new()
System.print(Object.hash(a) == Object.hash(a)) // expect: true
System.print(Object.hash(a) == Object.hash(b)) // expect: false
System.print(Object.hash([1]) == Object.hash([1])) // expect: false

// Value types agree with same(_,_).
System.print(Object.hash("ab") == Object.hash("a" + "b")) // expect: true
System.print(Object.hash(1.5) == Object.hash(3 / 2)) // expect: true
System.print(Object.hash(1..3) == Object.hash(1..3)) // expect: true
System.print(Object.hash(true) == Object.hash(true)) // expect: true
System.print(Object.hash(null) == Object.hash(null)) // expect: true
System.print(Object.hash(Foo) == Object.hash(Foo)) // expect: true

// A number, usable as a map key for the object.
var seen = {}
seen[Object.hash(a)] = a
System.print(Object.same(seen[Object.hash(a)], a)) // expect: true
System.print(Object.hash(a) is Num) // expect: true

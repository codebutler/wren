// A record describes a Map with known string keys. It compiles to nothing and
// declares no variable.
record Contact { name as String, phone as String or Null }

record Page(of T) {
  items as List(of T)
  next as String or Null,
  total as Num
}

record Empty {}

var c as Contact = {"name": "Ada", "phone": null}
System.print(c["name"]) // expect: Ada

if (true) {
  record Local { x as Num }
  System.print("in block") // expect: in block
}

record Last { done as Bool }

class Box {
  construct new(value) { _value = value }
  value as Num { _value }
  find(name as String) as Box or Null { name == "me" ? this : null }
  describe() as String { "Box(%(_value))" }
  static make(value as Num) as Box { Box.new(value) }
}

var box = Box.make(3)
System.print(box.value) // expect: 3
System.print(box.find("me").describe()) // expect: Box(3)
System.print(box.find("you")) // expect: null

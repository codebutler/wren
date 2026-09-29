class Stack(of T) {
  construct new() { _items = [] }
  push(item as T) { _items.add(item) }
  pop() as T or Null { _items.count == 0 ? null : _items.removeAt(-1) }
  count as Num { _items.count }
}

var undo as Stack(of String) = Stack.new()
undo.push("a")
undo.push("b")
System.print(undo.pop()) // expect: b
System.print(undo.count) // expect: 1
System.print(undo is Stack) // expect: true

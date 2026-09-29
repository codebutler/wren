// Type annotations are parsed and erased. Every annotated program below must
// compile to exactly what the same program compiles to with its annotations
// deleted in place (line breaks kept, so line numbers agree).

class Annotations {
  foreign static errors(source) as String
  foreign static compare(annotated as String, plain as String) as String
}

var check = Fn.new {|name as String, annotated as List(of String), plain as List(of String)|
  System.print("%(name): %(Annotations.compare(annotated.join("\n"), plain.join("\n")))")
}

// The comparison is not vacuous: a real difference is reported.
System.print(Annotations.compare("var a = 1", "var a = 2")) // expect: different constants in (script)
System.print(Annotations.compare("var a = 1", "var a = 1\nvar b")) // expect: different bytecode in (script)

check.call("parameters", [
  "class A {",
  "  f(a as Num, b as List(of String), c as Map(of String, List(of Num or Null))) { a + b.count }",
  "  construct new(name as String) { _name = name }",
  "  +(other as A) { this }",
  "  -(other as A) { this }",
  "  name=(value as String) { _name = value }",
  "  [index as Num, other as Num] { index }",
  "  [index as Num]=(value as A) { value }",
  "}"
], [
  "class A {",
  "  f(a, b, c) { a + b.count }",
  "  construct new(name) { _name = name }",
  "  +(other) { this }",
  "  -(other) { this }",
  "  name=(value) { _name = value }",
  "  [index, other] { index }",
  "  [index]=(value) { value }",
  "}"
]) // expect: parameters: same

check.call("returns", [
  "class A {",
  "  count as Num { 1 }",
  "  find(name as String) as A or Null { null }",
  "  name=(value) as String { value }",
  "  [index] as Num { index }",
  "  +(other) as A { this }",
  "  - as A { this }",
  "  ! as Bool { false }",
  "  static load(path) as List(of A) { [] }",
  "  foreign static now() as Num",
  "  is(other) as Bool { true }",
  "}"
], [
  "class A {",
  "  count { 1 }",
  "  find(name) { null }",
  "  name=(value) { value }",
  "  [index] { index }",
  "  +(other) { this }",
  "  - { this }",
  "  ! { false }",
  "  static load(path) { [] }",
  "  foreign static now()",
  "  is(other) { true }",
  "}"
]) // expect: returns: same

check.call("variables", [
  "var total as Num = 0",
  "var pending as String or Null",
  "{",
  "  var local as List(of Num) = [1, 2]",
  "  var none as Num",
  "  for (item as Num in local) total = total + item",
  "  for (i as Num or Null in 1..2) System.print(i)",
  "}"
], [
  "var total = 0",
  "var pending",
  "{",
  "  var local = [1, 2]",
  "  var none",
  "  for (item in local) total = total + item",
  "  for (i in 1..2) System.print(i)",
  "}"
]) // expect: variables: same

check.call("blocks", [
  "var list = [1, 2]",
  "list.each {|n as Num| System.print(n) }",
  "list.each {|n as Num or String, i as Num| n }",
  "var fn = Fn.new {|a as Fn(Num) as Bool, b as Fn()| a.call(1) }",
  "var nested = Fn.new {|x as Num| Fn.new {|y as Num| x + y } }"
], [
  "var list = [1, 2]",
  "list.each {|n| System.print(n) }",
  "list.each {|n, i| n }",
  "var fn = Fn.new {|a, b| a.call(1) }",
  "var nested = Fn.new {|x| Fn.new {|y| x + y } }"
]) // expect: blocks: same

// Field declarations must not create fields: the class's field count, the
// slot of each field and the recorded field names stay as the methods make
// them. Here they are declared in another order than they are used, and one
// is declared but never used.
check.call("fields", [
  "class A {",
  "  _unused as Num",
  "  _second as List(of Num)",
  "  __count as Num",
  "  _first as String or Null",
  "  construct new() {",
  "    _first = \"a\"",
  "    _second = []",
  "    __count = 1",
  "  }",
  "  _late as Fn(Num) as Num",
  "}",
  "class B is A {",
  "  _own as Num",
  "  construct new() { _own = 1 }",
  "}"
], [
  "class A {",
  "",
  "",
  "",
  "",
  "  construct new() {",
  "    _first = \"a\"",
  "    _second = []",
  "    __count = 1",
  "  }",
  "",
  "}",
  "class B is A {",
  "",
  "  construct new() { _own = 1 }",
  "}"
]) // expect: fields: same

check.call("types", [
  "var a as Fn = null",
  "var b as Fn() = null",
  "var c as Fn(Num, String) as Fn() as Bool or Null = null",
  "var d as (Fn(Num) as Bool) or Null = null",
  "var e as \"left\" or \"right\" = \"left\"",
  "var f as ((Num)) = 1",
  "var g as Map(of String, Map(of String, List(of Fn(Num) as Num))) = {}",
  "var h as Any or Null = null"
], [
  "var a = null",
  "var b = null",
  "var c = null",
  "var d = null",
  "var e = \"left\"",
  "var f = 1",
  "var g = {}",
  "var h = null"
]) // expect: types: same

check.call("generics", [
  "class Store(of K) {}",
  "class Stack(of T, V as Object or Null) is Store(of T) {",
  "  _items as List(of T)",
  "  construct new() { _items = [] }",
  "  push(item as T) { _items.add(item) }",
  "  map(of U)(fn as Fn(T) as U) as Stack(of U) { this }",
  "  static empty(of X) as List(of X) { [] }",
  "  static pair(of X, Y as Num)(x as X, y as Y) { [x, y] }",
  "}",
  "class Names is List(of String) {}",
  "var s as Stack(of Map(of String, Num), Null) = Stack.new()"
], [
  "class Store {}",
  "class Stack is Store {",
  "",
  "  construct new() { _items = [] }",
  "  push(item) { _items.add(item) }",
  "  map(fn) { this }",
  "  static empty { [] }",
  "  static pair(x, y) { [x, y] }",
  "}",
  "class Names is List {}",
  "var s = Stack.new()"
]) // expect: generics: same

// A record is a type: no code, no variable, and no statement, even as the
// last thing in a block.
check.call("records", [
  "record Contact { name as String, phone as String or Null }",
  "var a = 1",
  "record Page(of T) {",
  "  items as List(of T),",
  "  next as String or Null",
  "",
  "  total as Num",
  "}",
  "record Empty {}",
  "if (a == 1) {",
  "  System.print(a)",
  "  record Local { a as Num }",
  "}",
  "var b = Fn.new {",
  "  record Inner { a as Num }",
  "}",
  "record Last { x as Num }"
], [
  "",
  "var a = 1",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "if (a == 1) {",
  "  System.print(a)",
  "",
  "}",
  "var b = Fn.new {",
  "",
  "}",
  ""
]) // expect: records: same

// `as` still renames an import, beside annotations that use it too.
check.call("imports", [
  "import \"a\" for Water as H2O, Salt",
  "var w as H2O = H2O",
  "class S is Salt { f(x as H2O) as Salt { x } }"
], [
  "import \"a\" for Water as H2O, Salt",
  "var w = H2O",
  "class S is Salt { f(x) { x } }"
]) // expect: imports: same

// Error messages for malformed annotations.
System.print(Annotations.errors("var a as = 1"))
// expect: line 1: Error at 'as': Expect a type.
System.print(Annotations.errors("var a as\nNum"))
// expect: line 1: Error at 'as': Expect a type.
System.print(Annotations.errors("var a as List(Num) = 1"))
// expect: line 1: Error at '(': Expect 'of' after '(' in type arguments.
System.print(Annotations.errors("var a as List(of Num = 1"))
// expect: line 1: Error at '=': Expect ')' after type arguments.
System.print(Annotations.errors("var a as List(of ) = 1"))
// expect: line 1: Error at 'of': Expect a type.
System.print(Annotations.errors("var a as Fn(of Num) = 1"))
// expect: line 1: Error at 'Num': Expect ')' after function parameter types.
System.print(Annotations.errors("var a as Num or = 1"))
// expect: line 1: Error at 'or': Expect a type.
System.print(Annotations.errors("var a as (Num = 1"))
// expect: line 1: Error at '=': Expect ')' after type.
System.print(Annotations.errors("class A {\n  construct new() as A {}\n}"))
// expect: line 2: Error at 'as': A constructor cannot have a return type.
System.print(Annotations.errors("class A {\n  _x\n}"))
// expect: line 2: Error at '_x': Expect 'as' and a type after field name.
System.print(Annotations.errors("class A {\n  #hidden\n  _x as Num\n}"))
// expect: line 3: Error at '_x': A field declaration cannot have attributes.
System.print(Annotations.errors("class A {\n  static _x as Num\n}"))
// expect: line 2: Error at '_x': Expect method definition.
System.print(Annotations.errors("class A(T) {}"))
// expect: line 1: Error at '(': Expect 'of' after '(' in a class's type parameters.
System.print(Annotations.errors("class A(of) {}"))
// expect: line 1: Error at ')': Expect type parameter name.
System.print(Annotations.errors("class A is B(C) {}"))
// expect: line 1: Error at '(': Expect 'of' after '(' in type arguments.
System.print(Annotations.errors("record A(T) {}"))
// expect: line 1: Error at '(': Expect 'of' after '(' in a record's type parameters.
System.print(Annotations.errors("record A { name }"))
// expect: line 1: Error at 'name': Expect 'as' and a type after record field name.
System.print(Annotations.errors("record A { name as String phone as String }"))
// expect: line 1: Error at 'phone': Expect '}' after record fields.
System.print(Annotations.errors("record A {\n  name as String"))
// expect: line 2: Error at end of file: Expect '}' after record fields.
System.print(Annotations.errors("record A"))
// expect: line 1: Error at end of file: Expect '{' after record name.
System.print(Annotations.errors("#tag\nrecord A {}"))
// expect: line 1: Error at newline: Attributes can only specified before a class or a method
System.print(Annotations.errors("var a = 1 as Num"))
// expect: line 1: Error at 'as': Expect end of file.
System.print(Annotations.errors("class S { construct new() {} }\nvar s = S(of Num).new()"))
// expect: line 2: Error at '(': Expect end of file.
System.print(Annotations.errors("var a = [1]\na as Num = 2"))
// expect: line 2: Error at 'as': Expect end of file.
System.print(Annotations.errors("var a as " + "(" * 70 + "Num" + ")" * 70))
// expect: line 1: Error at '(': Type annotation is nested too deeply.

var deep as Map(of String, Map(of String, List(of Num or Null))) = {"a": {"b": [1, null]}}
System.print(deep["a"]["b"]) // expect: [1, null]
var grouped as List(of (Fn(Num) as Num) or Null) = [null]
System.print(grouped) // expect: [null]

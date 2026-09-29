// `of`, `or`, `record` and `Fn` are names everywhere but inside a type.
var of = 1
var or = 2
var record = 3
System.print(of + or + record) // expect: 6

class Words {
  static one(of) { of }
  static two(of, or) { of + or }
  static typed(of as Num) as Num { of }
  static record { "getter" }
}

System.print(Words.one(4)) // expect: 4
System.print(Words.two(1, 2)) // expect: 3
System.print(Words.typed(5)) // expect: 5
System.print(Words.record) // expect: getter

record = [record]
System.print(record) // expect: [3]
System.print(Fn.new { "fn" }.call()) // expect: fn

var doubled = [1, 2, 3].map {|n as Num| n * 2 }.toList
System.print(doubled) // expect: [2, 4, 6]

var join = Fn.new {|a as String or Num, b as String| "%(a)%(b)" }
System.print(join.call(1, "x")) // expect: 1x
System.print(join.arity) // expect: 2

var apply = Fn.new {|f as Fn(Num) as Num, x as Num| f.call(x) }
System.print(apply.call(Fn.new {|n as Num| n + 1 }, 41)) // expect: 42

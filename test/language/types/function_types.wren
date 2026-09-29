class Filter {
  static keep(items as List(of Num), test as Fn(Num) as Bool) as List(of Num) {
    return items.where(test).toList
  }
  static later(make as Fn() as (Fn(Num) as Num) or Null) { make.call() }
}

System.print(Filter.keep([1, 2, 3, 4]) {|n as Num| n % 2 == 0 }) // expect: [2, 4]
System.print(Filter.later { null }) // expect: null
var bare as Fn = Fn.new { "bare" }
System.print(bare.call()) // expect: bare

class Seq {
  static map(of T, U)(items as List(of T), fn as Fn(T) as U) as List(of U) {
    return items.map(fn).toList
  }
  static empty(of T as Object) as List(of T) { [] }
  static first(of T)(items as List(of T)) as T or Null {
    return items.count == 0 ? null : items[0]
  }
}

System.print(Seq.map([1, 2]) {|n as Num| n * 3 }) // expect: [3, 6]
System.print(Seq.empty) // expect: []
System.print(Seq.first(["a"])) // expect: a

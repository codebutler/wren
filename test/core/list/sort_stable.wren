// Elements the comparer does not order keep their order.
var pairs = [[2, "a"], [1, "b"], [2, "c"], [1, "d"], [0, "e"], [2, "f"]]
pairs.sort {|x, y| x[0] < y[0] }
System.print(pairs.map {|p| p[1] }.join()) // expect: ebdacf

// Sorted, reversed and all-equal input sort without going quadratic.
var sorted = (1..20000).toList
sorted.sort()
System.print(sorted[0]) // expect: 1
System.print(sorted[-1]) // expect: 20000
var reversed = (1..20000).toList[-1..0]
reversed.sort()
System.print(reversed[0]) // expect: 1
var same = List.filled(20000, 7)
same.sort()
System.print(same.count) // expect: 20000

// A range of one, and an empty list.
System.print([3].sort()) // expect: [3]
System.print([].sort()) // expect: []

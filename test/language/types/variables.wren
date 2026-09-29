var total as Num = 0
var pending as String or Null
System.print(pending) // expect: null

{
  var items as List(of Num) = [1, 2, 3]
  for (item as Num in items) total = total + item
}
System.print(total) // expect: 6

var byName as Map(of String, List(of Num)) = {"a": [1]}
System.print(byName["a"]) // expect: [1]

for (i as Num or Null in 1..2) System.print(i)
// expect: 1
// expect: 2

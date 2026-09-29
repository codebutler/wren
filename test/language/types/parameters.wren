class Cart {
  construct new(owner as String) { _owner = owner }
  owner { _owner }
  add(item as String, count as Num) { "%(count) x %(item)" }
  total(items as List(of Num), rate as Num or Null) {
    var sum = 0
    for (i in items) sum = sum + i
    return rate == null ? sum : sum * rate
  }
}

var cart = Cart.new("Ada")
System.print(cart.owner) // expect: Ada
System.print(cart.add("pear", 2)) // expect: 2 x pear
System.print(cart.total([1, 2, 3], null)) // expect: 6
System.print(cart.total([1, 2, 3], 2)) // expect: 12

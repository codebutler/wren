class Debug {
  foreign static start(watchLine)
  foreign static stop()
  foreign static abortAt(line)
  foreign static stack()
  foreign static moduleVariables(first)
}

class Counter {
  construct new(start) { _count = start }
  add(amount) {
    var before = _count
    _count = _count + amount
    return before
  }
}

var sum = Fn.new {|a, b|
  var total = a + b
  return total
}

// A call reports its callee's lines one frame deeper. Returning to the rest of
// the calling line does not report that line again. Locals are visible from
// the line after their declaration, and a block's end out of scope again.
var counter = Counter.new(1)
Debug.start(12)
counter.add(2)
{
  var inner = "block"
  var total = sum.call(3, 4)
}
System.print(Debug.stop()) // expect: 27@1 28@1 12@2[add(_):12 this=<Counter> amount=2 | (script):28] 13@2 14@2 30@1 31@1 19@2 20@2 32@1 33@1

// Each loop iteration reports its first line again. The compiler's hidden
// loop variables are not reported.
Debug.start(39)
for (i in 1..2) {
  var x = i
}
System.print(Debug.stop()) // expect: 37@1 38@1 39@1[(script):39 i=1] 40@1 38@1 39@1[(script):39 i=2] 40@1 38@1 40@1 41@1

// A line that loops back to itself is reported every time.
Debug.start(0)
var n = 0
while (n < 3) n = n + 1
System.print(Debug.stop()) // expect: 44@1 45@1 46@1 46@1 46@1 46@1 47@1

// Foreign methods can walk the stack too. Captured variables follow locals.
var greet = Fn.new {|name|
  var greeting = "hello " + name
  return Fn.new {
    var shout = greeting + "!"
    return Debug.stack()
  }
}
System.print(greet.call("fn").call()) // expect: [new(_) block argument:54 greeting="hello fn" shout="hello fn!" | (script):57]

// Recursion reports each frame's depth.
class Fib {
  static of(n) {
    if (n < 2) return n
    return of(n - 1) + of(n - 2)
  }
}
Debug.start(62)
Fib.of(2)
System.print(Debug.stop()) // expect: 66@1 67@1 62@2[of(_):62 this=<Fib metaclass> n=2 | (script):67] 63@2 62@3[of(_):62 this=<Fib metaclass> n=1 | of(_):63 this=<Fib metaclass> n=2 | (script):67] 62@3[of(_):62 this=<Fib metaclass> n=0 | of(_):63 this=<Fib metaclass> n=2 | (script):67] 68@1

// Other fibers are reported with the fibers that called them.
Debug.start(73)
Fiber.new {
  var y = 1
}.call()
System.print(Debug.stop()) // expect: 71@1 72@1 74@1 73@2[new(_) block argument:73 | (script):74] 74@2 75@1

// The hook may abort the fiber at a line.
var reached = "start"
var result = Fiber.new {
  Debug.abortAt(82)
  reached = "before"
  reached = "after"
}.try()
System.print(result) // expect: Stopped by the line hook.
System.print(reached) // expect: before

System.print(Debug.moduleVariables("Debug")) // expect: Debug=<Debug metaclass> Counter=<Counter metaclass> sum=<Fn> counter=<Counter> n=3 greet=<Fn> Fib=<Fib metaclass> reached="before" result="Stopped by the line hook." Interrupt=null spin=null Deep=null dive=null k=null

class Interrupt {
  foreign static every(interval, abortAt, watchAt)
  foreign static count()
}

// The interrupt hook runs every so many loop iterations and calls.
Interrupt.every(2, 0, 0)
for (i in 1..10) {}
System.print(Interrupt.count()) // expect: 5

// It can end a loop that never calls anything, and a recursion that never
// loops. The VM carries on.
var spin = Fiber.new {
  Interrupt.every(1000, 5, 0)
  while (true) {}
}
System.print(spin.try()) // expect: Interrupted.
class Deep {
  static down(n) { down(n + 1) }
}
var dive = Fiber.new {
  Interrupt.every(1, 50, 0)
  Deep.down(0)
}
System.print(dive.try()) // expect: Interrupted.

// It can install the line hook mid-loop; the loop carries on hooked.
Interrupt.every(1, 0, 3)
var k = 0
while (k < 5) k = k + 1
System.print(Debug.stop()) // expect: 118@1 118@1 118@1 119@1
Interrupt.count()

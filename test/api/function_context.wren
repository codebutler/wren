var Log = []
class FunctionContext {
  foreign static tag(fn, id)
  foreign static current
  foreign static check(source)
}

Log.add(FunctionContext.current)
var inner = Fn.new { FunctionContext.current }
FunctionContext.tag(inner, 2)
var read = Fn.new { FunctionContext.current }
var outer = Fn.new {
  Log.add(read.call())
  Log.add(inner.call())
  Log.add(read.call())
  return 42
}
FunctionContext.tag(outer, 1)
Log.add(outer.call())
Log.add(FunctionContext.current)

// Reentrant calls of the same closure retain the innermost context.
var recurse
recurse = Fn.new {|n|
  if (n > 0) return recurse.call(n-1)
  return FunctionContext.current
}
FunctionContext.tag(recurse, 3)
Log.add(recurse.call(20))
Log.add(FunctionContext.current)

// An untagged child's fiber inherits the running caller; yielding restores it.
var child = Fiber.new {
  Log.add(read.call())
  Fiber.yield("yielded")
  Log.add(read.call())
}
var parent = Fn.new {
  Log.add(child.call())
  return FunctionContext.current
}
FunctionContext.tag(parent, 4)
Log.add(parent.call())
child.call()
Log.add(FunctionContext.current)

// A suspended tagged frame keeps its data when resumed from a different caller.
var parked = Fn.new {
  Log.add(FunctionContext.current)
  Fiber.yield()
  Log.add(FunctionContext.current)
}
FunctionContext.tag(parked, 5)
var fiber = Fiber.new(parked)
fiber.call()
Log.add(FunctionContext.current)
fiber.call()
Log.add(FunctionContext.current)

// No reporting wrapper: the error is the same value, caught by the user's try.
var errorValue = ["original error"]
var fail = Fn.new {
  Log.add(FunctionContext.current)
  Fiber.abort(errorValue)
}
FunctionContext.tag(fail, 6)
var caught = Fiber.new { fail.call() }.try()
Log.add(Object.same(caught, errorValue))
Log.add(FunctionContext.current)

// Each closure has independent data even when it shares its compiled function.
var factory = Fn.new { Fn.new { FunctionContext.current } }
var first = factory.call()
var second = factory.call()
FunctionContext.tag(first, 7)
FunctionContext.tag(second, 8)
System.gc()
Log.add(first.call())
Log.add(second.call())
FunctionContext.tag(first, 0)
Log.add(first.call())

System.print(Log) // expect: [0, 1, 2, 1, 42, 0, 3, 0, 4, yielded, 4, 0, 0, 5, 0, 5, 0, 6, true, 0, 7, 8, 0]

// A tagged function's uncaught error still reaches the debugger before unwind,
// on the original user line, and produces exactly one error report.
var declarations = "class Context {\nforeign static tag(fn, id)\n}\n"
var source = declarations + "var fn = Fn.new {\nFiber.abort(\"original\")\n}\nContext.tag(fn, 12)\nfn.call()\n"
System.print(FunctionContext.check(source)) // expect: 2 errors=1 hook=12 line=5 after=0

// The same error caught by the user's Fiber.try is neither reported nor stopped.
var caughtSource = declarations + "var fn = Fn.new {\nFiber.abort(\"caught\")\n}\nContext.tag(fn, 13)\nFiber.new { fn.call() }.try()\n"
System.print(FunctionContext.check(caughtSource)) // expect: 0 errors=0 hook=0 line=0 after=0

// A transfer follows its actual fiber caller chain, not the transfer site's
// context; transferring back resumes the tagged frame with its context intact.
var transferLog = []
var target
var originFn = Fn.new {
  transferLog.add(FunctionContext.current)
  target.transfer()
  transferLog.add(FunctionContext.current)
}
FunctionContext.tag(originFn, 14)
var origin = Fiber.new(originFn)
target = Fiber.new {
  transferLog.add(FunctionContext.current)
  origin.transfer()
}
origin.call()
transferLog.add(FunctionContext.current)
System.print(transferLog) // expect: [14, 0, 14, 0]

// Interrupting the body reports the original active context and user line.
var interruptedSource = "class Context {\nforeign static tag(fn, id)\nforeign static interrupt()\n}\nvar fn = Fn.new {\nContext.interrupt()\nwhile (true) {}\n}\nContext.tag(fn, 15)\nfn.call()\n"
System.print(FunctionContext.check(interruptedSource)) // expect: 2 errors=1 hook=15 line=7 after=0

class Reporter {
  foreign static run(source, moveLine)
}

var header = "class Mark {\n  foreign static mark(f)\n  foreign static unmark(f)\n  foreign static interruptOnce()\n}\n"
var check = Fn.new {|lines, moveLine|
  return Reporter.run(header + lines.join("\n"), moveLine)
}

// 1. A reporting fiber returns its error to try, reports a one-fiber trace,
// and its caller carries on.
System.print(check.call([
  "var f = Fiber.new {",
  "  Fiber.abort(\"boom\")",
  "}",
  "Mark.mark(f)",
  "System.print(f.try())",
  "System.print(\"after\")"
], 0)) // expect: rc=0 out=boom|after| error=boom trace=1@7 hook=1@7 moved=0

// 2. A try around the caller does not hide the report or catch its error.
System.print(check.call([
  "var f = Fiber.new {",
  "  Fiber.abort(\"nested\")",
  "}",
  "Mark.mark(f)",
  "var outer = Fiber.new {",
  "  System.print(f.try())",
  "  return null",
  "}",
  "System.print(outer.try())"
], 0)) // expect: rc=0 out=nested|null| error=nested trace=1@7 hook=1@7 moved=0

// 3. A try inside the marked fiber still catches its own error.
System.print(check.call([
  "var f = Fiber.new {",
  "  var inner = Fiber.new { Fiber.abort(\"caught\") }",
  "  System.print(inner.try())",
  "  System.print(\"continued\")",
  "}",
  "Mark.mark(f)",
  "System.print(f.try())"
], 0)) // expect: rc=0 out=caught|continued|null| error=- trace=0@0 hook=0@0 moved=0

// 4. A plain try still suppresses the error hook and errorFn.
System.print(check.call([
  "var f = Fiber.new { Fiber.abort(\"plain\") }",
  "System.print(f.try())"
], 0)) // expect: rc=0 out=plain| error=- trace=0@0 hook=0@0 moved=0

// 5. Moving the failing frame in the hook discards the boundary's error.
System.print(check.call([
  "var f = Fiber.new {",
  "  System.print(\"before\")",
  "  Fiber.abort(\"moved\")",
  "  System.print(\"after move\")",
  "}",
  "Mark.mark(f)",
  "System.print(f.try())"
], 9)) // expect: rc=0 out=before|after move|null| error=- trace=0@0 hook=1@8 moved=1

// 6. A reporting fiber with no caller is uncaught.
System.print(check.call([
  "var f = Fiber.new { Fiber.abort(\"root\") }",
  "Mark.mark(f)",
  "f.transfer()"
], 0)) // expect: rc=2 out= error=root trace=1@6 hook=1@6 moved=0

// 7. Yielding is not an error, and the fiber remains resumable.
System.print(check.call([
  "var f = Fiber.new { Fiber.yield(\"pause\") }",
  "Mark.mark(f)",
  "System.print(f.try())",
  "System.print(f.isDone)"
], 0)) // expect: rc=0 out=pause|false| error=- trace=0@0 hook=0@0 moved=0

// 8. call, like try, returns the reported error and continues the caller.
System.print(check.call([
  "var f = Fiber.new { Fiber.abort(\"viaCall\") }",
  "Mark.mark(f)",
  "System.print(f.call())",
  "System.print(f.error)",
  "System.print(\"after\")"
], 0)) // expect: rc=0 out=viaCall|viaCall|after| error=viaCall trace=1@6 hook=1@6 moved=0

// 9. An error in a fiber called by the boundary ends both fibers, not its caller.
System.print(check.call([
  "var inner = Fiber.new { Fiber.abort(\"deep\") }",
  "var f = Fiber.new {",
  "  inner.call()",
  "  System.print(\"unreached\")",
  "}",
  "Mark.mark(f)",
  "System.print(f.try())",
  "System.print(inner.error)",
  "System.print(f.error)"
], 0)) // expect: rc=0 out=deep|deep|deep| error=deep trace=1@6 hook=1@6 moved=0

// 10. Each reporting fiber reports the error that reaches it.
System.print(check.call([
  "var b2 = Fiber.new { Fiber.abort(\"two\") }",
  "Mark.mark(b2)",
  "var b1 = Fiber.new {",
  "  System.print(b2.try())",
  "  Fiber.abort(\"one\")",
  "}",
  "Mark.mark(b1)",
  "System.print(b1.try())",
  "System.print(\"end\")"
], 0)) // expect: rc=0 out=two|one|end| error=one trace=2@6 hook=2@10 moved=0

// 11. Clearing the flag restores ordinary try behavior: no report or hook.
System.print(check.call([
  "var f = Fiber.new { Fiber.abort(\"cleared\") }",
  "Mark.mark(f)",
  "Mark.unmark(f)",
  "System.print(f.try())"
], 0)) // expect: rc=0 out=cleared| error=- trace=0@0 hook=0@0 moved=0

// 12. An interrupt-hook abort is reported, then the caller continues.
System.print(check.call([
  "var f = Fiber.new {",
  "  Mark.interruptOnce()",
  "  while (true) {}",
  "}",
  "Mark.mark(f)",
  "System.print(f.try())",
  "System.print(\"after interrupt\")"
], 0)) // expect: rc=0 out=interrupted|after interrupt| error=interrupted trace=1@8 hook=1@8 moved=0

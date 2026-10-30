class Reporter {
  foreign static run(source, moveLine)
}

var header = "class Mark { foreign static mark(f) }\n"
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
], 0)) // expect: rc=0 out=boom|after| error=boom trace=1@3 hook=1@3 moved=0

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
], 0)) // expect: rc=0 out=nested|null| error=nested trace=1@3 hook=1@3 moved=0

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
], 5)) // expect: rc=0 out=before|after move|null| error=- trace=0@0 hook=1@4 moved=1

// 6. A reporting fiber with no caller is uncaught.
System.print(check.call([
  "var f = Fiber.new { Fiber.abort(\"root\") }",
  "Mark.mark(f)",
  "f.transfer()"
], 0)) // expect: rc=2 out= error=root trace=1@2 hook=1@2 moved=0

// 7. Yielding is not an error, and the fiber remains resumable.
System.print(check.call([
  "var f = Fiber.new { Fiber.yield(\"pause\") }",
  "Mark.mark(f)",
  "System.print(f.try())",
  "System.print(f.isDone)"
], 0)) // expect: rc=0 out=pause|false| error=- trace=0@0 hook=0@0 moved=0

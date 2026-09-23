// A hex literal at the end of a line used to count that newline twice, so
// every later line number (errors, stack traces, debugger lines) was one too
// high.
var a = 0xFF
var b = 0xab
System.print(a + b) // expect: 426
Fiber.abort("boom") // expect runtime error: boom

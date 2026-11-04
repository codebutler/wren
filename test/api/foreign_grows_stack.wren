// Regression test.
//
// A foreign method or a foreign class's allocator may ask for more slots than
// the fiber's stack holds, and growing the stack moves it. The interpreter
// kept its pointer to the running frame's slots from before the call, so after
// it the frame read and wrote its parameters and locals in the freed stack,
// until the next call into Wren reloaded the frame.
//
// So each case below writes a local straight after the growing call, then
// makes a call (which reloads the frame from the moved stack) and checks it.
// Each runs in a new fiber, whose stack starts small: growing a small block
// moves it, where an allocator may extend a large one in place.

class GrowStack {
  foreign static grow(slots)
}

foreign class GrowingForeign {
  construct new(slots) {
    // The allocator has run.
    var isInstance = this is GrowingForeign

    // A local declared after the allocator ran must be in its own slot, not
    // above the slots the allocator asked for.
    var local = "local"
    var other = "other"
    System.print([isInstance, slots, local, other])
  }

  foreign value
}

class Test {
  static parameters(a, b) {
    var local = "local"
    GrowStack.grow(1024)
    local = "changed"
    a = "changed a"
    Fn.new {}.call()
    System.print([a, b, local])
  }

  static upvalue() {
    var captured = "captured"
    var block = Fn.new { captured }
    GrowStack.grow(1024)
    captured = "captured again"
    System.print(block.call())
  }

  static result() {
    var result = GrowStack.grow(1024)
    var after = "after"
    System.print([result, after])
  }
}

Fiber.new { Test.parameters("a", "b") }.call()
// expect: [changed a, b, changed]

Fiber.new { Test.upvalue() }.call() // expect: captured again

Fiber.new { Test.result() }.call() // expect: [1024, after]

Fiber.new {
  var grown = GrowingForeign.new(1024)
  // expect: [true, 1024, local, other]
  System.print(grown.value) // expect: 1024
}.call()

// An allocator that asks for a few more slots, without moving the stack, must
// not move the constructor's locals either.
GrowingForeign.new(8) // expect: [true, 8, local, other]

// Code in a block at the top level keeps its locals on the stack too.
Fiber.new {
  var x = "x"
  GrowStack.grow(1024)
  x = "y"
  Fn.new {}.call()
  System.print(x) // expect: y
}.call()

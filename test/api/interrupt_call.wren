class Interrupt {
  foreign static every(interval, abortAt, watchAt)
}

// An abort from the interrupt hook reports the line of the call.
class Deep {
  static down(n) {
    return down(n + 1) // expect runtime error: Interrupted.
  }
}
Interrupt.every(1, 50, 0)
Deep.down(0)

class Interrupt {
  foreign static every(interval, abortAt, watchAt)
}

// An abort from the interrupt hook reports the line of the loop.
Interrupt.every(1000, 3, 0)
System.print("spinning") // expect: spinning
while (true) {} // expect runtime error: Interrupted.

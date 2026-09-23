System.print(~0) // expect: 4294967295
System.print(~1) // expect: 4294967294
System.print(~23) // expect: 4294967272

// Max u32 value.
System.print(~0xffffffff) // expect: 0

// Floating point values.
System.print(~1.23) // expect: 4294967294
System.print(~0.00123) // expect: 4294967295
System.print(~345.67) // expect: 4294966950

// Operands are truncated and wrapped modulo 2^32 (JavaScript's ToUint32):
// negative, fractional and out-of-range numbers, NaN and infinity.
System.print(~(-1)) // expect: 0
System.print(~(-2)) // expect: 1
System.print(~0x100000000) // expect: 4294967295
System.print(~(-1.5)) // expect: 0

System.print(0 ^ 0) // expect: 0
System.print(1 ^ 1) // expect: 0
System.print(0 ^ 1) // expect: 1
System.print(1 ^ 0) // expect: 1
System.print(0xaaaaaaaa ^ 0x55555555) // expect: 4294967295
System.print(0xf0f0f0f0 ^ 0x3c3c3c3c) // expect: 3435973836

// Max u32 value.
System.print(0xffffffff ^ 0xffffffff) // expect: 0

// Operands are truncated and wrapped modulo 2^32 (JavaScript's ToUint32):
// negative, fractional and out-of-range numbers, NaN and infinity.
System.print(-1 ^ 0xff) // expect: 4294967040
System.print(2.9 ^ 1.1) // expect: 3
System.print(0x100000003 ^ 1) // expect: 2

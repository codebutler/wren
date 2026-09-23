System.print(0 | 0) // expect: 0
System.print(0xaaaaaaaa | 0x55555555) // expect: 4294967295
System.print(0xcccccccc | 0x66666666) // expect: 4008636142

// Max u32 value.
System.print(0xffffffff | 0xffffffff) // expect: 4294967295

// Operands are truncated and wrapped modulo 2^32 (JavaScript's ToUint32):
// negative, fractional and out-of-range numbers, NaN and infinity.
System.print(-1 | 0) // expect: 4294967295
System.print(-2 | 1) // expect: 4294967295
System.print(1.9 | 2.9) // expect: 3
System.print(0x100000001 | 2) // expect: 3

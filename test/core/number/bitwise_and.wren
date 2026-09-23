System.print(0 & 0) // expect: 0
System.print(0xaaaaaaaa & 0x55555555) // expect: 0
System.print(0xf0f0f0f0 & 0x3c3c3c3c) // expect: 808464432

// Max u32 value.
System.print(0xffffffff & 0xffffffff) // expect: 4294967295

// Operands are truncated and wrapped modulo 2^32 (JavaScript's ToUint32):
// negative, fractional and out-of-range numbers, NaN and infinity.
System.print(-1 & 255) // expect: 255
System.print(-256 & 0xffff) // expect: 65280
System.print(3.9 & 2.1) // expect: 2
System.print(-1.5 & 7) // expect: 7
System.print(0x100000005 & 0xff) // expect: 5
System.print(4294967296 & 1) // expect: 0
System.print((0/0) & 1) // expect: 0
System.print((1/0) & 1) // expect: 0

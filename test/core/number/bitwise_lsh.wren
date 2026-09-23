System.print(0 << 0) // expect: 0
System.print(1 << 0) // expect: 1
System.print(0 << 1) // expect: 0
System.print(1 << 1) // expect: 2
System.print(0xaaaaaaaa << 1) // expect: 1431655764
System.print(0xf0f0f0f0 << 1) // expect: 3789677024

// Max u32 value.
System.print(0xffffffff << 0) // expect: 4294967295

// Operands (the count too) are truncated and wrapped modulo 2^32 (JavaScript's ToUint32):
// negative, fractional and out-of-range numbers, NaN and infinity.
// Only the low five bits of the count are used.
System.print(1 << 31) // expect: 2147483648
System.print(1 << 32) // expect: 1
System.print(1 << 33) // expect: 2
System.print(3 << -1) // expect: 2147483648
System.print(-1 << 4) // expect: 4294967280
System.print(1.9 << 2.9) // expect: 4

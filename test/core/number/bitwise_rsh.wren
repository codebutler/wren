System.print(0 >> 0) // expect: 0
System.print(1 >> 0) // expect: 1
System.print(0 >> 1) // expect: 0
System.print(1 >> 1) // expect: 0
System.print(0xaaaaaaaa >> 1) // expect: 1431655765
System.print(0xf0f0f0f0 >> 1) // expect: 2021161080

// Max u32 value.
System.print(0xffffffff >> 1) // expect: 2147483647

// Operands (the count too) are truncated and wrapped modulo 2^32 (JavaScript's ToUint32):
// negative, fractional and out-of-range numbers, NaN and infinity.
// Only the low five bits of the count are used.
System.print(-1 >> 28) // expect: 15
System.print(-2 >> 1) // expect: 2147483647
System.print(0x80000000 >> 32) // expect: 2147483648
System.print(8 >> 33) // expect: 4
System.print(7.9 >> 1.9) // expect: 3

class Align {
  static name(side as "left" or "right" or "center") as String { side }
}

System.print(Align.name("left")) // expect: left
var mode as "on" or "off" or Null = "on"
System.print(mode) // expect: on

class Heading {
  static size(level as 1 or 2 or 3) as 24 or 18 or 14 { [24, 18, 14][level - 1] }
}

System.print(Heading.size(2)) // expect: 18
var ratio as 0.5 or 1.5 or 0x10 = 0.5
System.print(ratio) // expect: 0.5
for (n as 1 or 2 in [1, 2]) System.print(n) // expect: 1
// expect: 2

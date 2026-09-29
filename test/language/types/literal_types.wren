class Align {
  static name(side as "left" or "right" or "center") as String { side }
}

System.print(Align.name("left")) // expect: left
var mode as "on" or "off" or Null = "on"
System.print(mode) // expect: on

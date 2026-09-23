class VM {
  foreign static nullConfig()
  foreign static multipleInterpretCalls()
  foreign static gcEveryAllocation(source)
}
// TODO: Other configuration settings.

System.print(VM.nullConfig()) // expect: true
System.print(VM.multipleInterpretCalls()) // expect: true

// Compiling runtime attributes allocates maps, lists and strings the compiler
// holds while it keeps lexing. Each must stay rooted, or a collection frees it
// and the class loses attribute groups, keys or values.
System.print(VM.gcEveryAllocation("
#!author = \"Ada\"
#!tags(color = \"red\", size = 3, color = \"blue\")
#!flags(on)
class Sheet {
  #!pinned
  #!doc(summary = \"draws\")
  draw() {}
  #!group(a = 1, b = 2)
  static make() {}
}
var a = Sheet.attributes
System.write(a.self[null][\"author\"])
System.write(a.self[\"tags\"][\"color\"])
System.write(a.self[\"tags\"][\"size\"])
System.write(a.self[\"flags\"][\"on\"])
System.write(a.methods[\"draw()\"][null][\"pinned\"])
System.write(a.methods[\"draw()\"][\"doc\"][\"summary\"])
System.write(a.methods[\"static make()\"][\"group\"][\"b\"])
")) // expect: [Ada][red, blue][3][null][null][draws][2]

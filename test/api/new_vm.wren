class VM {
  foreign static nullConfig()
  foreign static multipleInterpretCalls()
  foreign static gcEveryAllocation(source)
  foreign static seededRandom(source, seed)
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

// wrenSetRandomSeed makes unseeded Randoms deterministic: the same seed draws
// the same numbers, a different seed different ones, two unseeded Randoms in
// one program differ, the first matches Random.new(seed), and a Random given
// its own seed is unaffected.
var randomSource = "
import \"random\" for Random
var a = Random.new()
var b = Random.new()
var c = Random.new(99)
System.write(\"\%(a.int(1000000)) \%(b.int(1000000)) \%(c.int(1000000)) \")
System.write(Random.new(7).int(1000000))
"
var seven = VM.seededRandom(randomSource, 7).split(" ")
var again = VM.seededRandom(randomSource, 7).split(" ")
var eight = VM.seededRandom(randomSource, 8).split(" ")
System.print(seven.count) // expect: 4
System.print(seven[0] == again[0] && seven[1] == again[1]) // expect: true
System.print(seven[0] != seven[1]) // expect: true
System.print(seven[0] != eight[0] && seven[1] != eight[1]) // expect: true
System.print(seven[0] == seven[3]) // expect: true
System.print(seven[2] == eight[2]) // expect: true

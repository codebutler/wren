// `as` renames an import and annotates a declaration in the same program.
import "../module/import_as/module" for ValueA as A, ValueB // expect: ran module

var a as String = A
class Holder {
  static hold(value as String) as String { value }
}
System.print(Holder.hold(a)) // expect: module A
System.print(ValueB) // expect: module B

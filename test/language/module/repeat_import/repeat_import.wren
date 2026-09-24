// Importing the same variable from the same module again is allowed: the
// import just stores it once more.
import "./module" for Module // expect: ran module
import "./module" for Module
import "./module" for Module as Alias, Other
import "./module" for Module as Alias
import "./module" for Other

System.print(Module) // expect: module
System.print(Alias)  // expect: module
System.print(Other)  // expect: other

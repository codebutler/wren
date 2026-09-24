// A name a `var` declared cannot be bound by an import, even twice.
var Module = "here"
import "./module" for Module // expect error

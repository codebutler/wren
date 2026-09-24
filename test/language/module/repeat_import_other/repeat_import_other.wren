// A name one import bound cannot be bound again to a different variable.
import "./module" for Module
import "./module" for Other as Module // expect error

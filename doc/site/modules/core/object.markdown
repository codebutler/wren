^title Object Class

## Static Methods

### **hash**(obj)

Returns a number that agrees with `same`: if `Object.same(a, b)` is true,
`Object.hash(a) == Object.hash(b)`. An object compared by reference has a
number of its own for as long as it lives, so it can stand for the object
as a map key. A value type (a number, string, range, boolean or null) gives
a hash of its value, which other values may share, so check `same` on a
match.

<pre class="snippet">
var a = [1]
System.print(Object.hash(a) == Object.hash(a)) //> true
System.print(Object.hash(a) == Object.hash([1])) //> false
</pre>

### **same**(obj1, obj2)

Returns `true` if *obj1* and *obj2* are the same. For [value
types](../../values.html), this returns `true` if the objects have equivalent
state. In other words, numbers, strings, booleans, and ranges compare by value.

For all other objects, this returns `true` only if *obj1* and *obj2* refer to
the exact same object in memory.

This is similar to the built in `==` operator in Object except that this cannot
be overriden. It allows you to reliably access the built-in equality semantics
even on user-defined classes.

## Methods

### **!** operator

Returns `false`, since most objects are considered [true][].

[true]: control-flow.html#truth

### **==**(other) and **!=**(other) operators

Compares two objects using built-in equality. This compares [value
types](../../values.html) by value, and all other objects are compared by
identity&mdash;two objects are equal only if they are the exact same object.

### **is**(class) operator

Returns `true` if this object's class or one of its superclasses is `class`.

<pre class="snippet">
System.print(123 is Num)     //> true
System.print("s" is Num)     //> false
System.print(null is String) //> false
System.print([] is List)     //> true
System.print([] is Sequence) //> true
</pre>

It is a runtime error if `class` is not a [Class][].

### **toString**

A default string representation of the object.

### **type**

The [Class][] of the object.

[class]: class.html

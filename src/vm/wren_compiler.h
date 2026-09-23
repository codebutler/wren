#ifndef wren_compiler_h
#define wren_compiler_h

#include "wren.h"
#include "wren_value.h"

typedef struct sCompiler Compiler;

// This module defines the compiler for Wren. It takes a string of source code
// and lexes, parses, and compiles it. Wren uses a single-pass compiler. It
// does not build an actual AST during parsing and then consume that to
// generate code. Instead, the parser directly emits bytecode.
//
// This forces a few restrictions on the grammar and semantics of the language.
// Things like forward references and arbitrary lookahead are much harder. We
// get a lot in return for that, though.
//
// The implementation is much simpler since we don't need to define a bunch of
// AST data structures. More so, we don't have to deal with managing memory for
// AST objects. The compiler does almost no dynamic allocation while running.
//
// Compilation is also faster since we don't create a bunch of temporary data
// structures and destroy them after generating code.

// Compiles [source], a string of Wren source code located in [module], to an
// [ObjFn] that will execute that code when invoked. Returns `NULL` if the
// source contains any syntax errors.
//
// If [isExpression] is `true`, [source] should be a single expression, and
// this compiles it to a function that evaluates and returns that expression.
// Otherwise, [source] should be a series of top level statements.
//
// If [printErrors] is `true`, any compile errors are output to stderr.
// Otherwise, they are silently discarded.
ObjFn* wrenCompile(WrenVM* vm, ObjModule* module, const char* source,
                   bool isExpression, bool printErrors);

// The most upvalues a function has (MAX_UPVALUES in wren_compiler.c).
#define WREN_FRAME_MAX_UPVALUES 256

// The scope of a stopped call frame, for [wrenCompileInFrame].
typedef struct
{
  // The function the frame is running, and the bytecode offset it is at. The
  // offset decides which locals are in scope.
  ObjFn* fn;
  int offset;

  // Whether the frame runs a method (slot 0 is `this`), and a static one.
  bool isMethod;
  bool isStatic;

  // For a method, the class that defines it: the code may read and assign
  // that class's own fields. NULL when unknown (fields are then an error).
  ObjClass* fieldsClass;
} WrenFrameScope;

// Compiles [source] as a function that runs in the scope of a stopped call
// frame: its locals and captured variables are the function's upvalues, so
// reading one reads the frame's variable and assigning one changes it. For a
// method, `this`, the defining class's fields and implicit self calls work
// as they do in the method. Names not found there are module variables.
//
// If [isExpression] is true, [source] is one expression and the function
// returns its value; otherwise it is statements and returns null.
//
// Returns NULL on a compile error, with the first error's message in [errorBuffer]
// (nothing is reported to the error callback). Otherwise [upvalues] receives
// two bytes per upvalue of the function: 1 if it captures the frame's local
// in that slot, 0 if it captures the frame's upvalue with that index. It must
// have room for WREN_FRAME_MAX_UPVALUES * 2 bytes. The caller must root the result
// before allocating.
ObjFn* wrenCompileInFrame(WrenVM* vm, ObjModule* module, const char* source,
                         bool isExpression, const WrenFrameScope* scope,
                         uint8_t* upvalues, char* errorBuffer,
                         size_t errorSize);

// When a class is defined, its superclass is not known until runtime since
// class definitions are just imperative statements. Most of the bytecode for a
// a method doesn't care, but there are two places where it matters:
//
//   - To load or store a field, we need to know the index of the field in the
//     instance's field array. We need to adjust this so that subclass fields
//     are positioned after superclass fields, and we don't know this until the
//     superclass is known.
//
//   - Superclass calls need to know which superclass to dispatch to.
//
// We could handle this dynamically, but that adds overhead. Instead, when a
// method is bound, we walk the bytecode for the function and patch it up.
void wrenBindMethodCode(ObjClass* classObj, ObjFn* fn);

// Reaches all of the heap-allocated objects in use by [compiler] (and all of
// its parents) so that they are not collected by the GC.
void wrenMarkCompiler(WrenVM* vm, Compiler* compiler);

#endif

#include <stdio.h>
#include <string.h>

#include "annotations.h"

// The test reaches past the public API to read the functions the compiler
// makes, so it can hold an annotated program to the promise that it compiles
// to exactly what the program without its annotations compiles to.
#include "../../src/vm/wren_vm.h"

// Annotations.errors(source) compiles [source] in a fresh VM and returns the
// first compile error it reports as "line N: message", or "" if it compiles.
// (The errors after the first are the parser recovering, not the point.)
//
// Annotations.compare(annotated, plain) compiles each source in its own fresh
// VM and returns "same" when the two compile to identical functions: the same
// bytecode, constants (nested functions compared the same way), arity, stack
// size, upvalues, and debug information (name, line of every byte, statement
// offsets, named variables). Otherwise it returns the first difference.
static char errors[4096];

static void collectError(WrenVM* vm, WrenErrorType type, const char* module,
                         int line, const char* message)
{
  if (type != WREN_ERROR_COMPILE || errors[0] != '\0') return;

  snprintf(errors, sizeof(errors), "line %d: %s", line, message);
}

static WrenVM* newCompilerVM()
{
  WrenConfiguration config;
  wrenInitConfiguration(&config);
  config.errorFn = collectError;
  return wrenNewVM(&config);
}

static void compileErrors(WrenVM* vm)
{
  const char* source = wrenGetSlotString(vm, 1);

  errors[0] = '\0';
  WrenVM* compiler = newCompilerVM();
  wrenCompileSource(compiler, "annotated", source, false, true);
  wrenFreeVM(compiler);

  wrenSetSlotString(vm, 0, errors);
}

static char difference[512];

static bool differ(const char* what, const char* fn)
{
  snprintf(difference, sizeof(difference), "different %s in %s", what, fn);
  return false;
}

static bool sameInts(IntBuffer* a, IntBuffer* b)
{
  return a->count == b->count &&
         memcmp(a->data, b->data, sizeof(int) * a->count) == 0;
}

static bool sameFn(ObjFn* a, ObjFn* b)
{
  const char* name = a->debug->name;
  if (strcmp(a->debug->name, b->debug->name) != 0) return differ("name", name);

  if (a->code.count != b->code.count ||
      memcmp(a->code.data, b->code.data, a->code.count) != 0)
  {
    return differ("bytecode", name);
  }

  if (a->arity != b->arity) return differ("arity", name);
  if (a->maxSlots != b->maxSlots) return differ("stack size", name);
  if (a->numUpvalues != b->numUpvalues) return differ("upvalues", name);

  if (a->constants.count != b->constants.count) return differ("constants", name);
  for (int i = 0; i < a->constants.count; i++)
  {
    Value x = a->constants.data[i];
    Value y = b->constants.data[i];
    if (IS_FN(x) && IS_FN(y))
    {
      if (!sameFn(AS_FN(x), AS_FN(y))) return false;
    }
    else if (IS_OBJ(x) && IS_OBJ(y) && !IS_STRING(x))
    {
      if (AS_OBJ(x)->type != AS_OBJ(y)->type) return differ("constants", name);
    }
    else if (!wrenValuesEqual(x, y))
    {
      return differ("constants", name);
    }
  }

  if (!sameInts(&a->debug->sourceLines, &b->debug->sourceLines))
  {
    return differ("source lines", name);
  }

  if (!sameInts(&a->debug->statements, &b->debug->statements))
  {
    return differ("statements", name);
  }

  FnVariableBuffer* va = &a->debug->variables;
  FnVariableBuffer* vb = &b->debug->variables;
  if (va->count != vb->count) return differ("variables", name);
  for (int i = 0; i < va->count; i++)
  {
    FnVariable* x = &va->data[i];
    FnVariable* y = &vb->data[i];
    if (strcmp(x->name, y->name) != 0 || x->index != y->index ||
        x->start != y->start || x->end != y->end || x->hidden != y->hidden)
    {
      return differ("variables", name);
    }
  }

  return true;
}

static void compare(WrenVM* vm)
{
  const char* annotated = wrenGetSlotString(vm, 1);
  const char* plain = wrenGetSlotString(vm, 2);

  // Each source gets a VM of its own so both see the same symbol tables.
  errors[0] = '\0';
  WrenVM* first = newCompilerVM();
  WrenVM* second = newCompilerVM();
  ObjClosure* a = wrenCompileSource(first, "annotated", annotated, false, true);
  ObjClosure* b = wrenCompileSource(second, "annotated", plain, false, true);

  // Nothing below allocates, so neither VM collects its function meanwhile.
  if (a == NULL || b == NULL)
  {
    snprintf(difference, sizeof(difference), "compile error: %s", errors);
  }
  else if (sameFn(a->fn, b->fn))
  {
    strcpy(difference, "same");
  }

  wrenFreeVM(first);
  wrenFreeVM(second);

  wrenSetSlotString(vm, 0, difference);
}

WrenForeignMethodFn annotationsBindMethod(const char* signature)
{
  if (strcmp(signature, "static Annotations.errors(_)") == 0) return compileErrors;
  if (strcmp(signature, "static Annotations.compare(_,_)") == 0) return compare;

  return NULL;
}

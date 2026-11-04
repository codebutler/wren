#include <string.h>

#include "foreign_grows_stack.h"

// Asks for [count] slots, far more than the fiber's stack holds, so the stack
// is reallocated and moves. Each new slot is written to make sure it is
// usable.
static void ensureSlots(WrenVM* vm, int first, int count)
{
  wrenEnsureSlots(vm, count);
  for (int i = first; i < count; i++) wrenSetSlotDouble(vm, i, i);
}

static void grow(WrenVM* vm)
{
  int count = (int)wrenGetSlotDouble(vm, 1);
  ensureSlots(vm, 2, count);
  wrenSetSlotDouble(vm, 0, count);
}

static void growingAllocate(WrenVM* vm)
{
  int count = (int)wrenGetSlotDouble(vm, 1);
  double* data = (double*)wrenSetSlotNewForeign(vm, 0, 0, sizeof(double));
  *data = count;
  ensureSlots(vm, 2, count);
}

static void growingValue(WrenVM* vm)
{
  double* data = (double*)wrenGetSlotForeign(vm, 0);
  wrenSetSlotDouble(vm, 0, *data);
}

WrenForeignMethodFn foreignGrowsStackBindMethod(const char* signature)
{
  if (strcmp(signature, "static GrowStack.grow(_)") == 0) return grow;
  if (strcmp(signature, "GrowingForeign.value") == 0) return growingValue;

  return NULL;
}

void foreignGrowsStackBindClass(
    const char* className, WrenForeignClassMethods* methods)
{
  if (strcmp(className, "GrowingForeign") == 0)
  {
    methods->allocate = growingAllocate;
  }
}

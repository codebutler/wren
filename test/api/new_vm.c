#include <string.h>

#include "new_vm.h"

static void nullConfig(WrenVM* vm)
{
  WrenVM* otherVM = wrenNewVM(NULL);

  // We should be able to execute code.
  WrenInterpretResult result = wrenInterpret(otherVM, "main", "1 + 2");
  wrenSetSlotBool(vm, 0, result == WREN_RESULT_SUCCESS);

  wrenFreeVM(otherVM);
}

static void multipleInterpretCalls(WrenVM* vm)
{
  WrenVM* otherVM = wrenNewVM(NULL);
  WrenInterpretResult result;

  bool correct = true;

  // Handles should be valid across calls into Wren code.
  WrenHandle* absMethod = wrenMakeCallHandle(otherVM, "abs");

  result = wrenInterpret(otherVM, "main", "import \"random\" for Random");
  correct = correct && (result == WREN_RESULT_SUCCESS);

  for (int i = 0; i < 5; i++) {
    // Calling `wrenEnsureSlots()` before `wrenInterpret()` should not introduce
    // problems later.
    wrenEnsureSlots(otherVM, 2);

    // Calling a foreign function should succeed.
    result = wrenInterpret(otherVM, "main", "Random.new(12345)");
    correct = correct && (result == WREN_RESULT_SUCCESS);

    wrenEnsureSlots(otherVM, 2);
    wrenSetSlotDouble(otherVM, 0, -i);
    result = wrenCall(otherVM, absMethod);
    correct = correct && (result == WREN_RESULT_SUCCESS);

    double absValue = wrenGetSlotDouble(otherVM, 0);
    correct = correct && (absValue == (double)i);
  }

  wrenSetSlotBool(vm, 0, correct);

  wrenReleaseHandle(otherVM, absMethod);
  wrenFreeVM(otherVM);
}

// Collects everything the stress VM prints.
static char stressOutput[4096];

static void stressWrite(WrenVM* vm, const char* text)
{
  strncat(stressOutput, text, sizeof(stressOutput) - strlen(stressOutput) - 1);
}

// Runs the source in slot 1 in a VM that collects garbage on every
// allocation, and returns what it printed. Any object the compiler or the VM
// forgets to root is freed at the first allocation after it's created, so
// the result shows it (wrong or missing output) instead of depending on where
// the heap happens to cross its threshold.
static void gcEveryAllocation(WrenVM* vm)
{
  WrenConfiguration config;
  wrenInitConfiguration(&config);
  config.writeFn = stressWrite;
  config.initialHeapSize = 0;
  config.minHeapSize = 0;
  config.heapGrowthPercent = 0;

  stressOutput[0] = '\0';
  WrenVM* otherVM = wrenNewVM(&config);
  WrenInterpretResult result =
      wrenInterpret(otherVM, "main", wrenGetSlotString(vm, 1));
  wrenFreeVM(otherVM);

  if (result != WREN_RESULT_SUCCESS) strcpy(stressOutput, "error");
  wrenSetSlotString(vm, 0, stressOutput);
}

WrenForeignMethodFn newVMBindMethod(const char* signature)
{
  if (strcmp(signature, "static VM.nullConfig()") == 0) return nullConfig;
  if (strcmp(signature, "static VM.multipleInterpretCalls()") == 0) return multipleInterpretCalls;
  if (strcmp(signature, "static VM.gcEveryAllocation(_)") == 0) return gcEveryAllocation;

  return NULL;
}

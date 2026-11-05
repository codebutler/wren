#include <string.h>
#include <stdint.h>
#include <stdio.h>

#include "user_data.h"

static const char* data = "my user data";
static const char* otherData = "other user data";

void* testReallocateFn(void* ptr, size_t newSize, void* userData) {
  if (strcmp(userData, data) != 0) return NULL;

  if (newSize == 0)
  {
    free(ptr);
    return NULL;
  }

  return realloc(ptr, newSize);
}

static void test(WrenVM* vm)
{
  WrenConfiguration configuration;
  wrenInitConfiguration(&configuration);

  // Should default to NULL.
  if (configuration.userData != NULL)
  {
    wrenSetSlotBool(vm, 0, false);
    return;
  }

  configuration.reallocateFn = testReallocateFn;
  configuration.userData = (void*)data;

  WrenVM* otherVM = wrenNewVM(&configuration);

  // Should be able to get it.
  if (wrenGetUserData(otherVM) != data)
  {
    wrenSetSlotBool(vm, 0, false);
    wrenFreeVM(otherVM);
    return;
  }

  // Should be able to set it.
  wrenSetUserData(otherVM, (void*)otherData);

  if (wrenGetUserData(otherVM) != otherData)
  {
    wrenSetSlotBool(vm, 0, false);
    wrenFreeVM(otherVM);
    return;
  }

  wrenSetSlotBool(vm, 0, true);
  wrenFreeVM(otherVM);
}

static void tagFn(WrenVM* vm)
{
  bool tagged = wrenSetFnUserData(vm, 1,
      (void*)(intptr_t)wrenGetSlotDouble(vm, 2));
  wrenSetSlotBool(vm, 0, tagged);
}

static void currentFn(WrenVM* vm)
{
  wrenSetSlotDouble(vm, 0, (double)(intptr_t)wrenGetCurrentFnUserData(vm));
}

static int contextErrors;
static int contextHookLine;
static intptr_t contextAtError;

static void contextError(WrenVM* vm, WrenErrorType type, const char* module,
                         int line, const char* message)
{
  if (type == WREN_ERROR_RUNTIME) contextErrors++;
}

static void contextErrorHook(WrenVM* vm, const char* message)
{
  WrenStackFrame frame;
  contextAtError = (intptr_t)wrenGetCurrentFnUserData(vm);
  if (wrenGetStackFrame(vm, 0, &frame)) contextHookLine = frame.line;
}

static void contextInterrupt(WrenVM* vm)
{
  wrenSetInterruptHook(vm, NULL, 1);
  wrenEnsureSlots(vm, 1);
  wrenSetSlotString(vm, 0, "interrupted");
  wrenAbortFiber(vm, 0);
}

static void armContextInterrupt(WrenVM* vm)
{
  wrenSetInterruptHook(vm, contextInterrupt, 1000);
  wrenSetSlotNull(vm, 0);
}

static WrenForeignMethodFn bindContext(WrenVM* vm, const char* module,
                                       const char* className, bool isStatic,
                                       const char* signature)
{
  if (isStatic && strcmp(signature, "tag(_,_)") == 0) return tagFn;
  if (isStatic && strcmp(signature, "current") == 0) return currentFn;
  if (isStatic && strcmp(signature, "interrupt()") == 0) return armContextInterrupt;
  return NULL;
}

static void checkContextError(WrenVM* vm)
{
  const char* source = wrenGetSlotString(vm, 1);
  WrenConfiguration config;
  wrenInitConfiguration(&config);
  config.bindForeignMethodFn = bindContext;
  config.errorFn = contextError;
  WrenVM* other = wrenNewVM(&config);
  wrenSetErrorHook(other, contextErrorHook);
  contextErrors = 0;
  contextHookLine = 0;
  contextAtError = 0;
  WrenInterpretResult result = wrenInterpret(other, "context", source);
  char text[128];
  snprintf(text, sizeof(text), "%d errors=%d hook=%ld line=%d after=%ld",
           result, contextErrors, (long)contextAtError, contextHookLine,
           (long)(intptr_t)wrenGetCurrentFnUserData(other));
  wrenSetSlotString(vm, 0, text);
  wrenFreeVM(other);
}

WrenForeignMethodFn userDataBindMethod(const char* signature)
{
  if (strcmp(signature, "static UserData.test") == 0) return test;
  if (strcmp(signature, "static FunctionContext.tag(_,_)") == 0) return tagFn;
  if (strcmp(signature, "static FunctionContext.current") == 0) return currentFn;
  if (strcmp(signature, "static FunctionContext.check(_)") == 0) return checkContextError;

  return NULL;
}

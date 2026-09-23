#include <stdio.h>
#include <string.h>

#include "debug.h"

// The line hook records every line it is told about as "line@depth". On the
// line given to Debug.start(_), it also records the whole stack with each
// frame's variables.
static char log[4096];
static int watchLine;

static void append(const char* text)
{
  strncat(log, text, sizeof(log) - strlen(log) - 1);
}

// Appends a short description of the value in [slot].
static void appendValue(WrenVM* vm, int slot)
{
  char buffer[64];
  switch (wrenGetSlotType(vm, slot))
  {
    case WREN_TYPE_NUM:
      snprintf(buffer, sizeof(buffer), "%g", wrenGetSlotDouble(vm, slot));
      break;
    case WREN_TYPE_STRING:
      snprintf(buffer, sizeof(buffer), "\"%s\"", wrenGetSlotString(vm, slot));
      break;
    case WREN_TYPE_BOOL:
      snprintf(buffer, sizeof(buffer), "%s",
               wrenGetSlotBool(vm, slot) ? "true" : "false");
      break;
    case WREN_TYPE_NULL:
      snprintf(buffer, sizeof(buffer), "null");
      break;
    default:
      snprintf(buffer, sizeof(buffer), "<%s>", wrenGetSlotClassName(vm, slot));
      break;
  }
  append(buffer);
}

static void appendStack(WrenVM* vm)
{
  wrenEnsureSlots(vm, 1);
  int frames = wrenGetStackFrameCount(vm);
  append("[");
  for (int i = 0; i < frames; i++)
  {
    WrenStackFrame frame;
    if (!wrenGetStackFrame(vm, i, &frame)) break;
    if (frame.module == NULL) continue;

    char buffer[128];
    snprintf(buffer, sizeof(buffer), "%s%s:%d", i == 0 ? "" : " | ",
             frame.function, frame.line);
    append(buffer);

    int variables = wrenGetFrameVariableCount(vm, i);
    for (int j = 0; j < variables; j++)
    {
      append(" ");
      append(wrenGetFrameVariable(vm, i, j, 0));
      append("=");
      appendValue(vm, 0);
    }
  }
  append("]");
}

static void lineHook(WrenVM* vm, const char* module, int line)
{
  // Only report lines in this test's own module.
  if (module == NULL) return;

  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%s%d@%d", log[0] == '\0' ? "" : " ",
           line, wrenGetStackFrameCount(vm));
  append(buffer);

  if (line == watchLine) appendStack(vm);
}

static void start(WrenVM* vm)
{
  log[0] = '\0';
  watchLine = (int)wrenGetSlotDouble(vm, 1);
  wrenSetLineHook(vm, lineHook);
}

static void stop(WrenVM* vm)
{
  wrenSetLineHook(vm, NULL);
  wrenSetSlotString(vm, 0, log);
}

// Aborts the running fiber from inside the hook when it reaches a line.
static int abortLine;

static void abortingHook(WrenVM* vm, const char* module, int line)
{
  if (module == NULL || line != abortLine) return;
  wrenSetLineHook(vm, NULL);
  wrenEnsureSlots(vm, 1);
  wrenSetSlotString(vm, 0, "Stopped by the line hook.");
  wrenAbortFiber(vm, 0);
}

static void abortAt(WrenVM* vm)
{
  abortLine = (int)wrenGetSlotDouble(vm, 1);
  wrenSetLineHook(vm, abortingHook);
}

// The interrupt hook counts its calls. It aborts the fiber on call
// [interruptAbortAt], and installs the recording line hook on call
// [interruptWatchAt] (0 = never).
static int interrupts;
static int interruptAbortAt;
static int interruptWatchAt;

static void interruptHook(WrenVM* vm)
{
  interrupts++;
  if (interrupts == interruptWatchAt)
  {
    log[0] = '\0';
    watchLine = 0;
    wrenSetLineHook(vm, lineHook);
  }
  if (interrupts == interruptAbortAt)
  {
    wrenSetInterruptHook(vm, NULL, 1);
    wrenEnsureSlots(vm, 1);
    wrenSetSlotString(vm, 0, "Interrupted.");
    wrenAbortFiber(vm, 0);
  }
}

static void interruptEvery(WrenVM* vm)
{
  interrupts = 0;
  interruptAbortAt = (int)wrenGetSlotDouble(vm, 2);
  interruptWatchAt = (int)wrenGetSlotDouble(vm, 3);
  wrenSetInterruptHook(vm, interruptHook, (int)wrenGetSlotDouble(vm, 1));
}

static void interruptCount(WrenVM* vm)
{
  wrenSetInterruptHook(vm, NULL, 1);
  wrenSetSlotDouble(vm, 0, interrupts);
}

// Describes the stack from inside a foreign method.
static void stack(WrenVM* vm)
{
  log[0] = '\0';
  appendStack(vm);
  wrenSetSlotString(vm, 0, log);
}

// Lists this module's variables from the one named [first] onwards. Every
// module starts with the core classes, so the test's own come last.
static void moduleVariables(WrenVM* vm)
{
  const char* module = "./test/api/debug";
  wrenEnsureSlots(vm, 3);
  const char* first = wrenGetSlotString(vm, 1);
  log[0] = '\0';
  bool listing = false;
  int count = wrenGetModuleVariableCount(vm, module);
  for (int i = 0; i < count; i++)
  {
    const char* name = wrenGetModuleVariableAt(vm, module, i, 2);
    if (strcmp(name, first) == 0) listing = true;
    if (!listing) continue;
    if (log[0] != '\0') append(" ");
    append(name);
    append("=");
    appendValue(vm, 2);
  }
  if (wrenGetModuleVariableAt(vm, module, count, 2) != NULL) append(" !");
  if (wrenGetModuleVariableCount(vm, "no such module") != -1) append(" !");
  wrenSetSlotString(vm, 0, log);
}

WrenForeignMethodFn debugBindMethod(const char* signature)
{
  if (strcmp(signature, "static Debug.start(_)") == 0) return start;
  if (strcmp(signature, "static Debug.stop()") == 0) return stop;
  if (strcmp(signature, "static Debug.abortAt(_)") == 0) return abortAt;
  if (strcmp(signature, "static Debug.stack()") == 0) return stack;
  if (strcmp(signature, "static Interrupt.every(_,_,_)") == 0) return interruptEvery;
  if (strcmp(signature, "static Interrupt.count()") == 0) return interruptCount;
  if (strcmp(signature, "static Debug.moduleVariables(_)") == 0) return moduleVariables;

  return NULL;
}

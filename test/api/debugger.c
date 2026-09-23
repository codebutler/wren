#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debugger.h"

// Stop.at(line, action, arg) installs a line hook that acts once, the first
// time the program reaches [line] of this test's module:
//
//   "eval"      evaluates expression [arg] in the innermost frame
//   "exec"      runs statements [arg] in the innermost frame
//   "set"       moves the innermost frame to line [arg] (a number)
//   "interpret" runs [arg] as new top-level code with wrenInterpretInHook
//   "swapset"   [arg] is "<line>|<source>": interprets <source> (after 300
//               blank lines, so its lines are its own) with
//               wrenInterpretInHook, replaces Greeter's methods with
//               Greeter2's, then moves the innermost frame to <line>
//
// Stop.onError(line) installs an error hook that records each error it sees
// and, when [line] is not 0, moves the failing frame there. Stop.result
// removes both hooks and returns what they recorded.
static char log[2048];
static int stopLine;
static char action[16];
static char arg[1024];
static int argLine;
static int errorLine;

static void append(const char* text)
{
  strncat(log, text, sizeof(log) - strlen(log) - 1);
}

static void appendSlot(WrenVM* vm, int slot)
{
  char buffer[128];
  switch (wrenGetSlotType(vm, slot))
  {
    case WREN_TYPE_NUM:
      snprintf(buffer, sizeof(buffer), "%g", wrenGetSlotDouble(vm, slot));
      break;
    case WREN_TYPE_STRING:
      snprintf(buffer, sizeof(buffer), "%s", wrenGetSlotString(vm, slot));
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

static const char* setLineName(WrenSetLineResult result)
{
  switch (result)
  {
    case WREN_SET_LINE_SUCCESS: return "moved";
    case WREN_SET_LINE_NOT_STOPPED: return "not-stopped";
    case WREN_SET_LINE_NO_STATEMENT: return "no-statement";
    case WREN_SET_LINE_OUT_OF_SCOPE: return "out-of-scope";
  }
  return "?";
}

static void act(WrenVM* vm)
{
  wrenEnsureSlots(vm, 1);
  if (strcmp(action, "eval") == 0 || strcmp(action, "exec") == 0)
  {
    WrenInterpretResult result = wrenEvaluateInFrame(
        vm, 0, arg, strcmp(action, "eval") == 0, 0);
    if (result == WREN_RESULT_COMPILE_ERROR) append("compile error: ");
    if (result == WREN_RESULT_RUNTIME_ERROR) append("runtime error: ");
    appendSlot(vm, 0);
  }
  else if (strcmp(action, "set") == 0)
  {
    append(setLineName(wrenSetFrameLine(vm, argLine)));
  }
  else if (strcmp(action, "swapset") == 0)
  {
    static char source[2048];
    const char* bar = strchr(arg, '|');
    int line = atoi(arg);
    memset(source, '\n', 300);
    snprintf(source + 300, sizeof(source) - 300, "%s", bar ? bar + 1 : "");
    if (wrenInterpretInHook(vm, "./test/api/debugger", source) != WREN_RESULT_SUCCESS)
    {
      append("swap did not compile");
      return;
    }
    const char* detail = NULL;
    WrenReplaceResult replaced = wrenReplaceMethods(vm, "./test/api/debugger",
                                                    "Greeter", "Greeter2", &detail);
    if (replaced != WREN_REPLACE_SUCCESS)
    {
      append("not replaced");
      return;
    }
    append(setLineName(wrenSetFrameLine(vm, line)));
  }
  else if (strcmp(action, "interpret") == 0)
  {
    WrenInterpretResult result = wrenInterpretInHook(vm, "./test/api/debugger",
                                                     arg);
    append(result == WREN_RESULT_SUCCESS ? "interpreted" : "failed");
  }
}

static void lineHook(WrenVM* vm, const char* module, int line)
{
  if (module == NULL || strcmp(module, "./test/api/debugger") != 0) return;
  if (line != stopLine) return;
  stopLine = -1;
  act(vm);
}

static void errorHook(WrenVM* vm, const char* message)
{
  WrenStackFrame frame;
  char buffer[160];
  wrenGetStackFrame(vm, 0, &frame);
  snprintf(buffer, sizeof(buffer), "%s[%s at %s:%d]", log[0] ? " " : "",
           message, frame.function, frame.line);
  append(buffer);

  // The failing frame's variables are still there.
  wrenEnsureSlots(vm, 1);
  int count = wrenGetFrameVariableCount(vm, 0);
  for (int i = 0; i < count; i++)
  {
    append(" ");
    append(wrenGetFrameVariable(vm, 0, i, 0));
    append("=");
    appendSlot(vm, 0);
  }

  if (errorLine != 0)
  {
    append(" ");
    append(setLineName(wrenSetFrameLine(vm, errorLine)));
  }
}

static void at(WrenVM* vm)
{
  log[0] = '\0';
  stopLine = (int)wrenGetSlotDouble(vm, 1);
  snprintf(action, sizeof(action), "%s", wrenGetSlotString(vm, 2));
  if (wrenGetSlotType(vm, 3) == WREN_TYPE_NUM)
  {
    argLine = (int)wrenGetSlotDouble(vm, 3);
    arg[0] = '\0';
  }
  else
  {
    snprintf(arg, sizeof(arg), "%s", wrenGetSlotString(vm, 3));
  }
  wrenSetLineHook(vm, lineHook);
}

static void onError(WrenVM* vm)
{
  log[0] = '\0';
  errorLine = (int)wrenGetSlotDouble(vm, 1);
  wrenSetErrorHook(vm, errorHook);
}

static void result(WrenVM* vm)
{
  wrenSetLineHook(vm, NULL);
  wrenSetErrorHook(vm, NULL);
  wrenSetSlotString(vm, 0, log);
}

// Inspect.fields(object): "_a=1 _b=2", from wrenGetInstanceField.
static void fields(WrenVM* vm)
{
  log[0] = '\0';
  wrenEnsureSlots(vm, 3);
  int count = wrenGetInstanceFieldCount(vm, 1);
  for (int i = 0; i < count; i++)
  {
    if (i > 0) append(" ");
    append(wrenGetInstanceField(vm, 1, i, 2));
    append("=");
    appendSlot(vm, 2);
  }
  if (wrenGetInstanceField(vm, 1, count, 2) != NULL) append(" (past the end)");
  wrenSetSlotString(vm, 0, log);
}

// Inspect.entries(map): "a=1 b=2", from wrenGetMapEntry.
static void entries(WrenVM* vm)
{
  log[0] = '\0';
  wrenEnsureSlots(vm, 4);
  for (int i = 0; wrenGetMapEntry(vm, 1, i, 2, 3); i++)
  {
    if (i > 0) append(" ");
    appendSlot(vm, 2);
    append("=");
    appendSlot(vm, 3);
  }
  wrenSetSlotString(vm, 0, log);
}

WrenForeignMethodFn debuggerBindMethod(const char* signature)
{
  if (strcmp(signature, "static Stop.at(_,_,_)") == 0) return at;
  if (strcmp(signature, "static Stop.onError(_)") == 0) return onError;
  if (strcmp(signature, "static Stop.result") == 0) return result;
  if (strcmp(signature, "static Inspect.fields(_)") == 0) return fields;
  if (strcmp(signature, "static Inspect.entries(_)") == 0) return entries;
  return NULL;
}

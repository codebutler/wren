#include <stdio.h>
#include <string.h>

#include "report_errors.h"

// Reporter.run creates a separate VM so its errorFn can be inspected while
// the outer API test continues after a reported error.
static char output[512];
static char runtime[128];
static int traceCount;
static int traceLine;
static int hookCount;
static int hookLine;
static int moveLine;
static WrenSetLineResult moveResult;

static void append(char* buffer, size_t size, const char* text)
{
  strncat(buffer, text, size - strlen(buffer) - 1);
}

static void writeOutput(WrenVM* vm, const char* text)
{
  (void)vm;
  for (; *text != '\0'; text++)
  {
    char character[2] = { *text == '\n' ? '|' : *text, '\0' };
    append(output, sizeof(output), character);
  }
}

static void collectError(WrenVM* vm, WrenErrorType type, const char* module,
                         int line, const char* message)
{
  (void)vm;
  (void)module;
  if (type == WREN_ERROR_RUNTIME)
  {
    snprintf(runtime, sizeof(runtime), "%s", message);
  }
  else if (type == WREN_ERROR_STACK_TRACE)
  {
    if (traceCount == 0) traceLine = line;
    traceCount++;
  }
}

static void errorHook(WrenVM* vm, const char* message)
{
  (void)message;
  WrenStackFrame frame;
  hookCount++;
  if (wrenGetStackFrame(vm, 0, &frame)) hookLine = frame.line;
  if (moveLine != 0) moveResult = wrenSetFrameLine(vm, moveLine);
}

static void mark(WrenVM* vm)
{
  wrenSetFiberReportsErrors(vm, 1, true);
  wrenSetSlotNull(vm, 0);
}

static WrenForeignMethodFn bindInner(WrenVM* vm, const char* module,
                                     const char* className, bool isStatic,
                                     const char* signature)
{
  (void)vm;
  (void)module;
  if (isStatic && strcmp(className, "Mark") == 0 &&
      strcmp(signature, "mark(_)") == 0) return mark;
  return NULL;
}

static void run(WrenVM* vm)
{
  const char* source = wrenGetSlotString(vm, 1);
  output[0] = '\0';
  runtime[0] = '\0';
  traceCount = 0;
  traceLine = 0;
  hookCount = 0;
  hookLine = 0;
  moveLine = (int)wrenGetSlotDouble(vm, 2);
  moveResult = WREN_SET_LINE_NOT_STOPPED;

  WrenConfiguration config;
  wrenInitConfiguration(&config);
  config.writeFn = writeOutput;
  config.errorFn = collectError;
  config.bindForeignMethodFn = bindInner;
  WrenVM* inner = wrenNewVM(&config);
  wrenSetErrorHook(inner, errorHook);
  WrenInterpretResult result = wrenInterpret(inner, "report_errors", source);
  wrenFreeVM(inner);

  char summary[1024];
  snprintf(summary, sizeof(summary),
           "rc=%d out=%s error=%s trace=%d@%d hook=%d@%d moved=%d",
           result, output, runtime[0] ? runtime : "-", traceCount,
           traceLine, hookCount, hookLine,
           moveResult == WREN_SET_LINE_SUCCESS);
  wrenSetSlotString(vm, 0, summary);
}

WrenForeignMethodFn reportErrorsBindMethod(const char* signature)
{
  if (strcmp(signature, "static Reporter.run(_,_)") == 0) return run;
  if (strcmp(signature, "static Mark.mark(_)") == 0) return mark;
  return NULL;
}

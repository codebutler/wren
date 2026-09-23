#include <stdio.h>
#include <string.h>

#include "replace_methods.h"

static const char* resultName(WrenReplaceResult result)
{
  switch (result)
  {
    case WREN_REPLACE_SUCCESS: return "success";
    case WREN_REPLACE_NOT_FOUND: return "not found";
    case WREN_REPLACE_UNSUPPORTED: return "unsupported";
    case WREN_REPLACE_SUPERCLASS_CHANGED: return "superclass changed";
    case WREN_REPLACE_FIELDS_CHANGED: return "fields changed";
    case WREN_REPLACE_METHOD_REMOVED: return "method removed";
  }
  return "?";
}

static void replace(WrenVM* vm)
{
  // Copy the names: the replacement collects garbage.
  char target[64];
  char source[64];
  snprintf(target, sizeof(target), "%s", wrenGetSlotString(vm, 1));
  snprintf(source, sizeof(source), "%s", wrenGetSlotString(vm, 2));

  const char* detail = "unset";
  WrenReplaceResult result = wrenReplaceMethods(
      vm, "./test/api/replace_methods", target, source, &detail);

  char message[256];
  if (detail == NULL)
  {
    snprintf(message, sizeof(message), "%s", resultName(result));
  }
  else
  {
    snprintf(message, sizeof(message), "%s: %s", resultName(result), detail);
  }
  wrenSetSlotString(vm, 0, message);
}

static void otherModule(WrenVM* vm)
{
  WrenReplaceResult result = wrenReplaceMethods(vm, "no such module", "A", "B",
                                                NULL);
  wrenSetSlotString(vm, 0, resultName(result));
}

WrenForeignMethodFn replaceMethodsBindMethod(const char* signature)
{
  if (strcmp(signature, "static Replace.methods(_,_)") == 0) return replace;
  if (strcmp(signature, "static Replace.otherModule()") == 0) return otherModule;
  return NULL;
}

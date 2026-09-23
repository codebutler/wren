#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "wren.h"
#include "wren_common.h"
#include "wren_compiler.h"
#include "wren_core.h"
#include "wren_debug.h"
#include "wren_primitive.h"
#include "wren_vm.h"

#if WREN_OPT_META
  #include "wren_opt_meta.h"
#endif
#if WREN_OPT_RANDOM
  #include "wren_opt_random.h"
#endif

#if WREN_DEBUG_TRACE_MEMORY || WREN_DEBUG_TRACE_GC
  #include <time.h>
  #include <stdio.h>
#endif

// The behavior of realloc() when the size is 0 is implementation defined. It
// may return a non-NULL pointer which must not be dereferenced but nevertheless
// should be freed. To prevent that, we avoid calling realloc() with a zero
// size.
static void* defaultReallocate(void* ptr, size_t newSize, void* _)
{
  if (newSize == 0)
  {
    free(ptr);
    return NULL;
  }

  return realloc(ptr, newSize);
}

int wrenGetVersionNumber() 
{ 
  return WREN_VERSION_NUMBER;
}

void wrenInitConfiguration(WrenConfiguration* config)
{
  config->reallocateFn = defaultReallocate;
  config->resolveModuleFn = NULL;
  config->loadModuleFn = NULL;
  config->bindForeignMethodFn = NULL;
  config->bindForeignClassFn = NULL;
  config->writeFn = NULL;
  config->errorFn = NULL;
  config->initialHeapSize = 1024 * 1024 * 10;
  config->minHeapSize = 1024 * 1024;
  config->heapGrowthPercent = 50;
  config->userData = NULL;
}

// The countdown with no interrupt hook installed. It still runs out, rarely,
// and is simply refilled.
#define INTERRUPT_OFF (1 << 30)

WrenVM* wrenNewVM(WrenConfiguration* config)
{
  WrenReallocateFn reallocate = defaultReallocate;
  void* userData = NULL;
  if (config != NULL) {
    userData = config->userData;
    reallocate = config->reallocateFn ? config->reallocateFn : defaultReallocate;
  }
  
  WrenVM* vm = (WrenVM*)reallocate(NULL, sizeof(*vm), userData);
  memset(vm, 0, sizeof(WrenVM));

  // Copy the configuration if given one.
  if (config != NULL)
  {
    memcpy(&vm->config, config, sizeof(WrenConfiguration));

    // We choose to set this after copying, 
    // rather than modifying the user config pointer
    vm->config.reallocateFn = reallocate;
  }
  else
  {
    wrenInitConfiguration(&vm->config);
  }

  // TODO: Should we allocate and free this during a GC?
  vm->grayCount = 0;
  // TODO: Tune this.
  vm->grayCapacity = 4;
  vm->gray = (Obj**)reallocate(NULL, vm->grayCapacity * sizeof(Obj*), userData);
  vm->nextGC = vm->config.initialHeapSize;

  wrenSymbolTableInit(&vm->methodNames);

  vm->interruptInterval = INTERRUPT_OFF;
  vm->interruptCountdown = INTERRUPT_OFF;

  vm->modules = wrenNewMap(vm);
  wrenInitializeCore(vm);
  return vm;
}

void wrenFreeVM(WrenVM* vm)
{
  ASSERT(vm->methodNames.count > 0, "VM appears to have already been freed.");
  
  // Free all of the GC objects.
  Obj* obj = vm->first;
  while (obj != NULL)
  {
    Obj* next = obj->next;
    wrenFreeObj(vm, obj);
    obj = next;
  }

  // Free up the GC gray set.
  vm->gray = (Obj**)vm->config.reallocateFn(vm->gray, 0, vm->config.userData);

  // Tell the user if they didn't free any handles. We don't want to just free
  // them here because the host app may still have pointers to them that they
  // may try to use. Better to tell them about the bug early.
  ASSERT(vm->handles == NULL, "All handles have not been released.");

  wrenSymbolTableClear(vm, &vm->methodNames);

  DEALLOCATE(vm, vm);
}

void wrenCollectGarbage(WrenVM* vm)
{
#if WREN_DEBUG_TRACE_MEMORY || WREN_DEBUG_TRACE_GC
  printf("-- gc --\n");

  size_t before = vm->bytesAllocated;
  double startTime = (double)clock() / CLOCKS_PER_SEC;
#endif

  // Mark all reachable objects.

  // Reset this. As we mark objects, their size will be counted again so that
  // we can track how much memory is in use without needing to know the size
  // of each *freed* object.
  //
  // This is important because when freeing an unmarked object, we don't always
  // know how much memory it is using. For example, when freeing an instance,
  // we need to know its class to know how big it is, but its class may have
  // already been freed.
  vm->bytesAllocated = 0;

  wrenGrayObj(vm, (Obj*)vm->modules);

  // Temporary roots.
  for (int i = 0; i < vm->numTempRoots; i++)
  {
    wrenGrayObj(vm, vm->tempRoots[i]);
  }

  // The current fiber.
  wrenGrayObj(vm, (Obj*)vm->fiber);

  // The handles.
  for (WrenHandle* handle = vm->handles;
       handle != NULL;
       handle = handle->next)
  {
    wrenGrayValue(vm, handle->value);
  }

  // Any object the compiler is using (if there is one).
  if (vm->compiler != NULL) wrenMarkCompiler(vm, vm->compiler);

  // Method names.
  wrenBlackenSymbolTable(vm, &vm->methodNames);

  // Now that we have grayed the roots, do a depth-first search over all of the
  // reachable objects.
  wrenBlackenObjects(vm);

  // Collect the white objects.
  Obj** obj = &vm->first;
  while (*obj != NULL)
  {
    if (!((*obj)->isDark))
    {
      // This object wasn't reached, so remove it from the list and free it.
      Obj* unreached = *obj;
      *obj = unreached->next;
      wrenFreeObj(vm, unreached);
    }
    else
    {
      // This object was reached, so unmark it (for the next GC) and move on to
      // the next.
      (*obj)->isDark = false;
      obj = &(*obj)->next;
    }
  }

  // Calculate the next gc point, this is the current allocation plus
  // a configured percentage of the current allocation.
  vm->nextGC = vm->bytesAllocated + ((vm->bytesAllocated * vm->config.heapGrowthPercent) / 100);
  if (vm->nextGC < vm->config.minHeapSize) vm->nextGC = vm->config.minHeapSize;

#if WREN_DEBUG_TRACE_MEMORY || WREN_DEBUG_TRACE_GC
  double elapsed = ((double)clock() / CLOCKS_PER_SEC) - startTime;
  // Explicit cast because size_t has different sizes on 32-bit and 64-bit and
  // we need a consistent type for the format string.
  printf("GC %lu before, %lu after (%lu collected), next at %lu. Took %.3fms.\n",
         (unsigned long)before,
         (unsigned long)vm->bytesAllocated,
         (unsigned long)(before - vm->bytesAllocated),
         (unsigned long)vm->nextGC,
         elapsed*1000.0);
#endif
}

void* wrenReallocate(WrenVM* vm, void* memory, size_t oldSize, size_t newSize)
{
#if WREN_DEBUG_TRACE_MEMORY
  // Explicit cast because size_t has different sizes on 32-bit and 64-bit and
  // we need a consistent type for the format string.
  printf("reallocate %p %lu -> %lu\n",
         memory, (unsigned long)oldSize, (unsigned long)newSize);
#endif

  // If new bytes are being allocated, add them to the total count. If objects
  // are being completely deallocated, we don't track that (since we don't
  // track the original size). Instead, that will be handled while marking
  // during the next GC.
  vm->bytesAllocated += newSize - oldSize;

#if WREN_DEBUG_GC_STRESS
  // Since collecting calls this function to free things, make sure we don't
  // recurse.
  if (newSize > 0) wrenCollectGarbage(vm);
#else
  if (newSize > 0 && vm->bytesAllocated > vm->nextGC) wrenCollectGarbage(vm);
#endif

  return vm->config.reallocateFn(memory, newSize, vm->config.userData);
}

// Captures the local variable [local] into an [Upvalue]. If that local is
// already in an upvalue, the existing one will be used. (This is important to
// ensure that multiple closures closing over the same variable actually see
// the same variable.) Otherwise, it will create a new open upvalue and add it
// the fiber's list of upvalues.
static ObjUpvalue* captureUpvalue(WrenVM* vm, ObjFiber* fiber, Value* local)
{
  // If there are no open upvalues at all, we must need a new one.
  if (fiber->openUpvalues == NULL)
  {
    fiber->openUpvalues = wrenNewUpvalue(vm, local);
    return fiber->openUpvalues;
  }

  ObjUpvalue* prevUpvalue = NULL;
  ObjUpvalue* upvalue = fiber->openUpvalues;

  // Walk towards the bottom of the stack until we find a previously existing
  // upvalue or pass where it should be.
  while (upvalue != NULL && upvalue->value > local)
  {
    prevUpvalue = upvalue;
    upvalue = upvalue->next;
  }

  // Found an existing upvalue for this local.
  if (upvalue != NULL && upvalue->value == local) return upvalue;

  // We've walked past this local on the stack, so there must not be an
  // upvalue for it already. Make a new one and link it in in the right
  // place to keep the list sorted.
  ObjUpvalue* createdUpvalue = wrenNewUpvalue(vm, local);
  if (prevUpvalue == NULL)
  {
    // The new one is the first one in the list.
    fiber->openUpvalues = createdUpvalue;
  }
  else
  {
    prevUpvalue->next = createdUpvalue;
  }

  createdUpvalue->next = upvalue;
  return createdUpvalue;
}

// Closes any open upvalues that have been created for stack slots at [last]
// and above.
static void closeUpvalues(ObjFiber* fiber, Value* last)
{
  while (fiber->openUpvalues != NULL &&
         fiber->openUpvalues->value >= last)
  {
    ObjUpvalue* upvalue = fiber->openUpvalues;

    // Move the value into the upvalue itself and point the upvalue to it.
    upvalue->closed = *upvalue->value;
    upvalue->value = &upvalue->closed;

    // Remove it from the open upvalue list.
    fiber->openUpvalues = upvalue->next;
  }
}

// Looks up a foreign method in [moduleName] on [className] with [signature].
//
// This will try the host's foreign method binder first. If that fails, it
// falls back to handling the built-in modules.
static WrenForeignMethodFn findForeignMethod(WrenVM* vm,
                                             const char* moduleName,
                                             const char* className,
                                             bool isStatic,
                                             const char* signature)
{
  WrenForeignMethodFn method = NULL;
  
  if (vm->config.bindForeignMethodFn != NULL)
  {
    method = vm->config.bindForeignMethodFn(vm, moduleName, className, isStatic,
                                            signature);
  }
  
  // If the host didn't provide it, see if it's an optional one.
  if (method == NULL)
  {
#if WREN_OPT_META
    if (strcmp(moduleName, "meta") == 0)
    {
      method = wrenMetaBindForeignMethod(vm, className, isStatic, signature);
    }
#endif
#if WREN_OPT_RANDOM
    if (strcmp(moduleName, "random") == 0)
    {
      method = wrenRandomBindForeignMethod(vm, className, isStatic, signature);
    }
#endif
  }

  return method;
}

// Defines [methodValue] as a method on [classObj].
//
// Handles both foreign methods where [methodValue] is a string containing the
// method's signature and Wren methods where [methodValue] is a function.
//
// Aborts the current fiber if the method is a foreign method that could not be
// found.
static void bindMethod(WrenVM* vm, int methodType, int symbol,
                       ObjModule* module, ObjClass* classObj, Value methodValue)
{
  const char* className = classObj->name->value;
  if (methodType == CODE_METHOD_STATIC) classObj = classObj->obj.classObj;

  Method method;
  if (IS_STRING(methodValue))
  {
    const char* name = AS_CSTRING(methodValue);
    method.type = METHOD_FOREIGN;
    method.as.foreign = findForeignMethod(vm, module->name->value,
                                          className,
                                          methodType == CODE_METHOD_STATIC,
                                          name);

    if (method.as.foreign == NULL)
    {
      vm->fiber->error = wrenStringFormat(vm,
          "Could not find foreign method '@' for class $ in module '$'.",
          methodValue, classObj->name->value, module->name->value);
      return;
    }
  }
  else
  {
    method.as.closure = AS_CLOSURE(methodValue);
    method.type = METHOD_BLOCK;

    // Patch up the bytecode now that we know the superclass.
    wrenBindMethodCode(classObj, method.as.closure->fn);
  }

  wrenBindMethod(vm, classObj, symbol, method);
}

static void callForeign(WrenVM* vm, ObjFiber* fiber,
                        WrenForeignMethodFn foreign, int numArgs)
{
  ASSERT(vm->apiStack == NULL, "Cannot already be in foreign call.");
  vm->apiStack = fiber->stackTop - numArgs;

  foreign(vm);

  // Discard the stack slots for the arguments and temporaries but leave one
  // for the result.
  fiber->stackTop = vm->apiStack + 1;

  vm->apiStack = NULL;
}

// Handles the current fiber having aborted because of an error.
//
// Walks the call chain of fibers, aborting each one until it hits a fiber that
// handles the error. If none do, tells the VM to stop.
static void runtimeError(WrenVM* vm)
{
  ASSERT(wrenHasError(vm->fiber), "Should only call this after an error.");

  ObjFiber* current = vm->fiber;
  Value error = current->error;
  
  while (current != NULL)
  {
    // Every fiber along the call chain gets aborted with the same error.
    current->error = error;

    // If the caller ran this fiber using "try", give it the error and stop.
    if (current->state == FIBER_TRY)
    {
      // Make the caller's try method return the error message.
      current->caller->stackTop[-1] = vm->fiber->error;
      vm->fiber = current->caller;
      return;
    }
    
    // Otherwise, unhook the caller since we will never resume and return to it.
    ObjFiber* caller = current->caller;
    current->caller = NULL;
    current = caller;
  }

  // If we got here, nothing caught the error, so show the stack trace.
  wrenDebugPrintStackTrace(vm);
  vm->fiber = NULL;
  vm->apiStack = NULL;
}

// Aborts the current fiber with an appropriate method not found error for a
// method with [symbol] on [classObj].
static void methodNotFound(WrenVM* vm, ObjClass* classObj, int symbol)
{
  vm->fiber->error = wrenStringFormat(vm, "@ does not implement '$'.",
      OBJ_VAL(classObj->name), vm->methodNames.data[symbol]->value);
}

// Looks up the previously loaded module with [name].
//
// Returns `NULL` if no module with that name has been loaded.
static ObjModule* getModule(WrenVM* vm, Value name)
{
  Value moduleValue = wrenMapGet(vm->modules, name);
  return !IS_UNDEFINED(moduleValue) ? AS_MODULE(moduleValue) : NULL;
}

static ObjClosure* compileInModule(WrenVM* vm, Value name, const char* source,
                                   bool isExpression, bool printErrors)
{
  // See if the module has already been loaded.
  ObjModule* module = getModule(vm, name);
  if (module == NULL)
  {
    module = wrenNewModule(vm, AS_STRING(name));

    // It's possible for the wrenMapSet below to resize the modules map,
    // and trigger a GC while doing so. When this happens it will collect
    // the module we've just created. Once in the map it is safe.
    wrenPushRoot(vm, (Obj*)module);

    // Store it in the VM's module registry so we don't load the same module
    // multiple times.
    wrenMapSet(vm, vm->modules, name, OBJ_VAL(module));

    wrenPopRoot(vm);

    // Implicitly import the core module.
    ObjModule* coreModule = getModule(vm, NULL_VAL);
    for (int i = 0; i < coreModule->variables.count; i++)
    {
      wrenDefineVariable(vm, module,
                         coreModule->variableNames.data[i]->value,
                         coreModule->variableNames.data[i]->length,
                         coreModule->variables.data[i], NULL);
    }
  }

  ObjFn* fn = wrenCompile(vm, module, source, isExpression, printErrors);
  if (fn == NULL)
  {
    // TODO: Should we still store the module even if it didn't compile?
    return NULL;
  }

  // Functions are always wrapped in closures.
  wrenPushRoot(vm, (Obj*)fn);
  ObjClosure* closure = wrenNewClosure(vm, fn);
  wrenPopRoot(vm); // fn.

  return closure;
}

// Verifies that [superclassValue] is a valid object to inherit from. That
// means it must be a class and cannot be the class of any built-in type.
//
// Also validates that it doesn't result in a class with too many fields and
// the other limitations foreign classes have.
//
// If successful, returns `null`. Otherwise, returns a string for the runtime
// error message.
static Value validateSuperclass(WrenVM* vm, Value name, Value superclassValue,
                                int numFields)
{
  // Make sure the superclass is a class.
  if (!IS_CLASS(superclassValue))
  {
    return wrenStringFormat(vm,
        "Class '@' cannot inherit from a non-class object.",
        name);
  }

  // Make sure it doesn't inherit from a sealed built-in type. Primitive methods
  // on these classes assume the instance is one of the other Obj___ types and
  // will fail horribly if it's actually an ObjInstance.
  ObjClass* superclass = AS_CLASS(superclassValue);
  if (superclass == vm->classClass ||
      superclass == vm->fiberClass ||
      superclass == vm->fnClass || // Includes OBJ_CLOSURE.
      superclass == vm->listClass ||
      superclass == vm->mapClass ||
      superclass == vm->rangeClass ||
      superclass == vm->stringClass ||
      superclass == vm->boolClass ||
      superclass == vm->nullClass ||
      superclass == vm->numClass)
  {
    return wrenStringFormat(vm,
        "Class '@' cannot inherit from built-in class '@'.",
        name, OBJ_VAL(superclass->name));
  }

  if (superclass->numFields == -1)
  {
    return wrenStringFormat(vm,
        "Class '@' cannot inherit from foreign class '@'.",
        name, OBJ_VAL(superclass->name));
  }

  if (numFields == -1 && superclass->numFields > 0)
  {
    return wrenStringFormat(vm,
        "Foreign class '@' may not inherit from a class with fields.",
        name);
  }

  if (superclass->numFields + numFields > MAX_FIELDS)
  {
    return wrenStringFormat(vm,
        "Class '@' may not have more than 255 fields, including inherited "
        "ones.", name);
  }

  return NULL_VAL;
}

static void bindForeignClass(WrenVM* vm, ObjClass* classObj, ObjModule* module)
{
  WrenForeignClassMethods methods;
  methods.allocate = NULL;
  methods.finalize = NULL;
  
  // Check the optional built-in module first so the host can override it.
  
  if (vm->config.bindForeignClassFn != NULL)
  {
    methods = vm->config.bindForeignClassFn(vm, module->name->value,
                                            classObj->name->value);
  }

  // If the host didn't provide it, see if it's a built in optional module.
  if (methods.allocate == NULL && methods.finalize == NULL)
  {
#if WREN_OPT_RANDOM
    if (strcmp(module->name->value, "random") == 0)
    {
      methods = wrenRandomBindForeignClass(vm, module->name->value,
                                           classObj->name->value);
    }
#endif
  }
  
  Method method;
  method.type = METHOD_FOREIGN;

  // Add the symbol even if there is no allocator so we can ensure that the
  // symbol itself is always in the symbol table.
  int symbol = wrenSymbolTableEnsure(vm, &vm->methodNames, "<allocate>", 10);
  if (methods.allocate != NULL)
  {
    method.as.foreign = methods.allocate;
    wrenBindMethod(vm, classObj, symbol, method);
  }
  
  // Add the symbol even if there is no finalizer so we can ensure that the
  // symbol itself is always in the symbol table.
  symbol = wrenSymbolTableEnsure(vm, &vm->methodNames, "<finalize>", 10);
  if (methods.finalize != NULL)
  {
    method.as.foreign = (WrenForeignMethodFn)methods.finalize;
    wrenBindMethod(vm, classObj, symbol, method);
  }
}

// Completes the process for creating a new class.
//
// The class attributes instance and the class itself should be on the 
// top of the fiber's stack. 
//
// This process handles moving the attribute data for a class from
// compile time to runtime, since it now has all the attributes associated
// with a class, including for methods.
static void endClass(WrenVM* vm) 
{
  // Pull the attributes and class off the stack
  Value attributes = vm->fiber->stackTop[-2];
  Value classValue = vm->fiber->stackTop[-1];

  // Remove the stack items
  vm->fiber->stackTop -= 2;

  ObjClass* classObj = AS_CLASS(classValue);
    classObj->attributes = attributes;
}

// Creates a new class.
//
// If [numFields] is -1, the class is a foreign class. The name and superclass
// should be on top of the fiber's stack. After calling this, the top of the
// stack will contain the new class.
//
// Aborts the current fiber if an error occurs.
static void createClass(WrenVM* vm, int numFields, ObjModule* module)
{
  // Pull the name and superclass off the stack.
  Value name = vm->fiber->stackTop[-2];
  Value superclass = vm->fiber->stackTop[-1];

  // We have two values on the stack and we are going to leave one, so discard
  // the other slot.
  vm->fiber->stackTop--;

  vm->fiber->error = validateSuperclass(vm, name, superclass, numFields);
  if (wrenHasError(vm->fiber)) return;

  ObjClass* classObj = wrenNewClass(vm, AS_CLASS(superclass), numFields,
                                    AS_STRING(name));
  vm->fiber->stackTop[-1] = OBJ_VAL(classObj);

  if (numFields == -1) bindForeignClass(vm, classObj, module);
}

static void createForeign(WrenVM* vm, ObjFiber* fiber, Value* stack)
{
  ObjClass* classObj = AS_CLASS(stack[0]);
  ASSERT(classObj->numFields == -1, "Class must be a foreign class.");

  // TODO: Don't look up every time.
  int symbol = wrenSymbolTableFind(&vm->methodNames, "<allocate>", 10);
  ASSERT(symbol != -1, "Should have defined <allocate> symbol.");

  ASSERT(classObj->methods.count > symbol, "Class should have allocator.");
  Method* method = &classObj->methods.data[symbol];
  ASSERT(method->type == METHOD_FOREIGN, "Allocator should be foreign.");

  // Pass the constructor arguments to the allocator as well.
  ASSERT(vm->apiStack == NULL, "Cannot already be in foreign call.");
  vm->apiStack = stack;

  method->as.foreign(vm);

  vm->apiStack = NULL;
}

void wrenFinalizeForeign(WrenVM* vm, ObjForeign* foreign)
{
  // TODO: Don't look up every time.
  int symbol = wrenSymbolTableFind(&vm->methodNames, "<finalize>", 10);
  ASSERT(symbol != -1, "Should have defined <finalize> symbol.");

  // If there are no finalizers, don't finalize it.
  if (symbol == -1) return;

  // If the class doesn't have a finalizer, bail out.
  ObjClass* classObj = foreign->obj.classObj;
  if (symbol >= classObj->methods.count) return;

  Method* method = &classObj->methods.data[symbol];
  if (method->type == METHOD_NONE) return;

  ASSERT(method->type == METHOD_FOREIGN, "Finalizer should be foreign.");

  WrenFinalizerFn finalizer = (WrenFinalizerFn)method->as.foreign;
  finalizer(foreign->data);
}

// Let the host resolve an imported module name if it wants to.
static Value resolveModule(WrenVM* vm, Value name)
{
  // If the host doesn't care to resolve, leave the name alone.
  if (vm->config.resolveModuleFn == NULL) return name;

  ObjFiber* fiber = vm->fiber;
  ObjFn* fn = fiber->frames[fiber->numFrames - 1].closure->fn;
  ObjString* importer = fn->module->name;
  
  const char* resolved = vm->config.resolveModuleFn(vm, importer->value,
                                                    AS_CSTRING(name));
  if (resolved == NULL)
  {
    vm->fiber->error = wrenStringFormat(vm,
        "Could not resolve module '@' imported from '@'.",
        name, OBJ_VAL(importer));
    return NULL_VAL;
  }
  
  // If they resolved to the exact same string, we don't need to copy it.
  if (resolved == AS_CSTRING(name)) return name;

  // Copy the string into a Wren String object.
  name = wrenNewString(vm, resolved);
  DEALLOCATE(vm, (char*)resolved);
  return name;
}

static Value importModule(WrenVM* vm, Value name)
{
  name = resolveModule(vm, name);
  
  // If the module is already loaded, we don't need to do anything.
  Value existing = wrenMapGet(vm->modules, name);
  if (!IS_UNDEFINED(existing)) return existing;

  wrenPushRoot(vm, AS_OBJ(name));

  WrenLoadModuleResult result = {0};
  const char* source = NULL;
  
  // Let the host try to provide the module.
  if (vm->config.loadModuleFn != NULL)
  {
    result = vm->config.loadModuleFn(vm, AS_CSTRING(name));
  }
  
  // If the host didn't provide it, see if it's a built in optional module.
  if (result.source == NULL)
  {
    result.onComplete = NULL;
    ObjString* nameString = AS_STRING(name);
#if WREN_OPT_META
    if (strcmp(nameString->value, "meta") == 0) result.source = wrenMetaSource();
#endif
#if WREN_OPT_RANDOM
    if (strcmp(nameString->value, "random") == 0) result.source = wrenRandomSource();
#endif
  }
  
  if (result.source == NULL)
  {
    vm->fiber->error = wrenStringFormat(vm, "Could not load module '@'.", name);
    wrenPopRoot(vm); // name.
    return NULL_VAL;
  }
  
  ObjClosure* moduleClosure = compileInModule(vm, name, result.source, false, true);
  
  // Now that we're done, give the result back in case there's cleanup to do.
  if(result.onComplete) result.onComplete(vm, AS_CSTRING(name), result);
  
  if (moduleClosure == NULL)
  {
    vm->fiber->error = wrenStringFormat(vm,
                                        "Could not compile module '@'.", name);
    wrenPopRoot(vm); // name.
    return NULL_VAL;
  }

  wrenPopRoot(vm); // name.

  // Return the closure that executes the module.
  return OBJ_VAL(moduleClosure);
}

static Value getModuleVariable(WrenVM* vm, ObjModule* module,
                               Value variableName)
{
  ObjString* variable = AS_STRING(variableName);
  uint32_t variableEntry = wrenSymbolTableFind(&module->variableNames,
                                               variable->value,
                                               variable->length);
  
  // It's a runtime error if the imported variable does not exist.
  if (variableEntry != UINT32_MAX)
  {
    return module->variables.data[variableEntry];
  }
  
  vm->fiber->error = wrenStringFormat(vm,
      "Could not find a variable named '@' in module '@'.",
      variableName, OBJ_VAL(module->name));
  return NULL_VAL;
}

inline static bool checkArity(WrenVM* vm, Value value, int numArgs)
{
  ASSERT(IS_CLOSURE(value), "Receiver must be a closure.");
  ObjFn* fn = AS_CLOSURE(value)->fn;

  // We only care about missing arguments, not extras. The "- 1" is because
  // numArgs includes the receiver, the function itself, which we don't want to
  // count.
  if (numArgs - 1 >= fn->arity) return true;

  vm->fiber->error = CONST_STRING(vm, "Function expects more arguments.");
  return false;
}


// Reports [line] of the running [fn] to the line hook. The hook gets an empty
// window of API slots above the running frame's stack.
static void callLineHook(WrenVM* vm, ObjFiber* fiber, ObjFn* fn, int line)
{
  vm->hookFiber = fiber;
  vm->hookDepth = fiber->numFrames;
  vm->hookLine = line;

  // The stubs that the C API uses to call into Wren belong to no module and
  // have no source lines to report.
  if (fn->module == NULL) return;

  const char* module = fn->module->name == NULL ? NULL : fn->module->name->value;
  Value* apiStack = vm->apiStack;
  int stackTop = (int)(fiber->stackTop - fiber->stack);
  vm->apiStack = fiber->stackTop;
  vm->inLineHook = true;

  vm->lineHook(vm, module, line);

  vm->inLineHook = false;
  // The hook may have grown the stack, so restore the top by offset.
  fiber->stackTop = fiber->stack + stackTop;
  vm->apiStack = apiStack;
}

// Refills the interrupt countdown and runs the interrupt hook, if any, with the
// same API stack window as the line hook.
static void callInterruptHook(WrenVM* vm, ObjFiber* fiber)
{
  vm->interruptCountdown = vm->interruptInterval;
  if (vm->interruptHook == NULL) return;

  Value* apiStack = vm->apiStack;
  int stackTop = (int)(fiber->stackTop - fiber->stack);
  vm->apiStack = fiber->stackTop;
  vm->inLineHook = true;

  vm->interruptHook(vm);

  vm->inLineHook = false;
  fiber->stackTop = fiber->stack + stackTop;
  vm->apiStack = apiStack;
}

// Returned by an interpreter loop that hands execution to the other one.
#define INTERPRETER_SWITCH -1

#define WREN_INTERPRETER_HOOKED 0
#define WREN_INTERPRETER_NAME runUnhooked
#include "wren_interpreter.inc"
#undef WREN_INTERPRETER_HOOKED
#undef WREN_INTERPRETER_NAME

#define WREN_INTERPRETER_HOOKED 1
#define WREN_INTERPRETER_NAME runHooked
#include "wren_interpreter.inc"
#undef WREN_INTERPRETER_HOOKED
#undef WREN_INTERPRETER_NAME

// Runs [fiber] until it completes or fails. The interpreter is compiled twice
// (wren_interpreter.inc): without a line hook, the plain loop runs, exactly as
// it did before hooks existed; with one, the hooked loop. Installing or
// removing a hook while running moves execution between them at the current
// instruction.
static WrenInterpretResult runInterpreter(WrenVM* vm, ObjFiber* fiber)
{
  // Remember the current fiber so we can find it if a GC happens.
  vm->fiber = fiber;
  fiber->state = FIBER_ROOT;

  // A fiber allocated where an old one was must still report its first line.
  vm->hookFiber = NULL;

  for (;;)
  {
    int result = vm->lineHook != NULL ? runHooked(vm, fiber)
                                      : runUnhooked(vm, fiber);
    if (result != INTERPRETER_SWITCH) return (WrenInterpretResult)result;
    fiber = vm->fiber;
  }
}

WrenHandle* wrenMakeCallHandle(WrenVM* vm, const char* signature)
{
  ASSERT(signature != NULL, "Signature cannot be NULL.");
  
  int signatureLength = (int)strlen(signature);
  ASSERT(signatureLength > 0, "Signature cannot be empty.");
  
  // Count the number parameters the method expects.
  int numParams = 0;
  if (signature[signatureLength - 1] == ')')
  {
    for (int i = signatureLength - 1; i > 0 && signature[i] != '('; i--)
    {
      if (signature[i] == '_') numParams++;
    }
  }
  
  // Count subscript arguments.
  if (signature[0] == '[')
  {
    for (int i = 0; i < signatureLength && signature[i] != ']'; i++)
    {
      if (signature[i] == '_') numParams++;
    }
  }
  
  // Add the signatue to the method table.
  int method =  wrenSymbolTableEnsure(vm, &vm->methodNames,
                                      signature, signatureLength);
  ASSERT(method <= MAX_METHODS, "Method limit reached.");
  
  // Create a little stub function that assumes the arguments are on the stack
  // and calls the method.
  ObjFn* fn = wrenNewFunction(vm, NULL, numParams + 1);
  
  // Wrap the function in a closure and then in a handle. Do this here so it
  // doesn't get collected as we fill it in.
  WrenHandle* value = wrenMakeHandle(vm, OBJ_VAL(fn));
  value->value = OBJ_VAL(wrenNewClosure(vm, fn));
  
  wrenByteBufferWrite(vm, &fn->code, (uint8_t)(CODE_CALL_0 + numParams));
  wrenByteBufferWrite(vm, &fn->code, (method >> 8) & 0xff);
  wrenByteBufferWrite(vm, &fn->code, method & 0xff);
  wrenByteBufferWrite(vm, &fn->code, CODE_RETURN);
  wrenByteBufferWrite(vm, &fn->code, CODE_END);
  wrenIntBufferFill(vm, &fn->debug->sourceLines, 0, 5);
  wrenFunctionBindName(vm, fn, signature, signatureLength);

  return value;
}

WrenInterpretResult wrenCall(WrenVM* vm, WrenHandle* method)
{
  ASSERT(method != NULL, "Method cannot be NULL.");
  ASSERT(IS_CLOSURE(method->value), "Method must be a method handle.");
  ASSERT(vm->fiber != NULL, "Must set up arguments for call first.");
  ASSERT(vm->apiStack != NULL, "Must set up arguments for call first.");
  ASSERT(vm->fiber->numFrames == 0, "Can not call from a foreign method.");
  
  ObjClosure* closure = AS_CLOSURE(method->value);
  
  ASSERT(vm->fiber->stackTop - vm->fiber->stack >= closure->fn->arity,
         "Stack must have enough arguments for method.");
  
  // Clear the API stack. Now that wrenCall() has control, we no longer need
  // it. We use this being non-null to tell if re-entrant calls to foreign
  // methods are happening, so it's important to clear it out now so that you
  // can call foreign methods from within calls to wrenCall().
  vm->apiStack = NULL;

  // Discard any extra temporary slots. We take for granted that the stub
  // function has exactly one slot for each argument.
  vm->fiber->stackTop = &vm->fiber->stack[closure->fn->maxSlots];
  
  wrenCallFunction(vm, vm->fiber, closure, 0);
  WrenInterpretResult result = runInterpreter(vm, vm->fiber);
  
  // If the call didn't abort, then set up the API stack to point to the
  // beginning of the stack so the host can access the call's return value.
  if (vm->fiber != NULL) vm->apiStack = vm->fiber->stack;
  
  return result;
}

WrenHandle* wrenMakeHandle(WrenVM* vm, Value value)
{
  if (IS_OBJ(value)) wrenPushRoot(vm, AS_OBJ(value));
  
  // Make a handle for it.
  WrenHandle* handle = ALLOCATE(vm, WrenHandle);
  handle->value = value;

  if (IS_OBJ(value)) wrenPopRoot(vm);

  // Add it to the front of the linked list of handles.
  if (vm->handles != NULL) vm->handles->prev = handle;
  handle->prev = NULL;
  handle->next = vm->handles;
  vm->handles = handle;
  
  return handle;
}

void wrenReleaseHandle(WrenVM* vm, WrenHandle* handle)
{
  ASSERT(handle != NULL, "Handle cannot be NULL.");

  // Update the VM's head pointer if we're releasing the first handle.
  if (vm->handles == handle) vm->handles = handle->next;

  // Unlink it from the list.
  if (handle->prev != NULL) handle->prev->next = handle->next;
  if (handle->next != NULL) handle->next->prev = handle->prev;

  // Clear it out. This isn't strictly necessary since we're going to free it,
  // but it makes for easier debugging.
  handle->prev = NULL;
  handle->next = NULL;
  handle->value = NULL_VAL;
  DEALLOCATE(vm, handle);
}

WrenInterpretResult wrenInterpret(WrenVM* vm, const char* module,
                                  const char* source)
{
  ObjClosure* closure = wrenCompileSource(vm, module, source, false, true);
  if (closure == NULL) return WREN_RESULT_COMPILE_ERROR;
  
  wrenPushRoot(vm, (Obj*)closure);
  ObjFiber* fiber = wrenNewFiber(vm, closure);
  wrenPopRoot(vm); // closure.
  vm->apiStack = NULL;

  return runInterpreter(vm, fiber);
}

ObjClosure* wrenCompileSource(WrenVM* vm, const char* module, const char* source,
                            bool isExpression, bool printErrors)
{
  Value nameValue = NULL_VAL;
  if (module != NULL)
  {
    nameValue = wrenNewString(vm, module);
    wrenPushRoot(vm, AS_OBJ(nameValue));
  }
  
  ObjClosure* closure = compileInModule(vm, nameValue, source,
                                        isExpression, printErrors);

  if (module != NULL) wrenPopRoot(vm); // nameValue.
  return closure;
}

Value wrenGetModuleVariable(WrenVM* vm, Value moduleName, Value variableName)
{
  ObjModule* module = getModule(vm, moduleName);
  if (module == NULL)
  {
    vm->fiber->error = wrenStringFormat(vm, "Module '@' is not loaded.",
                                        moduleName);
    return NULL_VAL;
  }
  
  return getModuleVariable(vm, module, variableName);
}

Value wrenFindVariable(WrenVM* vm, ObjModule* module, const char* name)
{
  int symbol = wrenSymbolTableFind(&module->variableNames, name, strlen(name));
  return module->variables.data[symbol];
}

int wrenDeclareVariable(WrenVM* vm, ObjModule* module, const char* name,
                        size_t length, int line)
{
  if (module->variables.count == MAX_MODULE_VARS) return -2;

  // Implicitly defined variables get a "value" that is the line where the
  // variable is first used. We'll use that later to report an error on the
  // right line.
  wrenValueBufferWrite(vm, &module->variables, NUM_VAL(line));
  return wrenSymbolTableAdd(vm, &module->variableNames, name, length);
}

int wrenDefineVariable(WrenVM* vm, ObjModule* module, const char* name,
                       size_t length, Value value, int* line)
{
  if (module->variables.count == MAX_MODULE_VARS) return -2;

  if (IS_OBJ(value)) wrenPushRoot(vm, AS_OBJ(value));

  // See if the variable is already explicitly or implicitly declared.
  int symbol = wrenSymbolTableFind(&module->variableNames, name, length);

  if (symbol == -1)
  {
    // Brand new variable.
    symbol = wrenSymbolTableAdd(vm, &module->variableNames, name, length);
    wrenValueBufferWrite(vm, &module->variables, value);
  }
  else if (IS_NUM(module->variables.data[symbol]))
  {
    // An implicitly declared variable's value will always be a number.
    // Now we have a real definition.
    if(line) *line = (int)AS_NUM(module->variables.data[symbol]);
    module->variables.data[symbol] = value;

	// If this was a localname we want to error if it was 
	// referenced before this definition.
	if (wrenIsLocalName(name)) symbol = -3;
  }
  else
  {
    // Already explicitly declared.
    symbol = -1;
  }

  if (IS_OBJ(value)) wrenPopRoot(vm);

  return symbol;
}

// TODO: Inline?
void wrenPushRoot(WrenVM* vm, Obj* obj)
{
  ASSERT(obj != NULL, "Can't root NULL.");
  ASSERT(vm->numTempRoots < WREN_MAX_TEMP_ROOTS, "Too many temporary roots.");

  vm->tempRoots[vm->numTempRoots++] = obj;
}

void wrenPopRoot(WrenVM* vm)
{
  ASSERT(vm->numTempRoots > 0, "No temporary roots to release.");
  vm->numTempRoots--;
}

int wrenGetSlotCount(WrenVM* vm)
{
  if (vm->apiStack == NULL) return 0;
  
  return (int)(vm->fiber->stackTop - vm->apiStack);
}

void wrenEnsureSlots(WrenVM* vm, int numSlots)
{
  // If we don't have a fiber accessible, create one for the API to use.
  if (vm->apiStack == NULL)
  {
    vm->fiber = wrenNewFiber(vm, NULL);
    vm->apiStack = vm->fiber->stack;
  }
  
  int currentSize = (int)(vm->fiber->stackTop - vm->apiStack);
  if (currentSize >= numSlots) return;
  
  // Grow the stack if needed.
  int needed = (int)(vm->apiStack - vm->fiber->stack) + numSlots;
  wrenEnsureStack(vm, vm->fiber, needed);
  
  vm->fiber->stackTop = vm->apiStack + numSlots;
}

// Ensures that [slot] is a valid index into the API's stack of slots.
static void validateApiSlot(WrenVM* vm, int slot)
{
  ASSERT(slot >= 0, "Slot cannot be negative.");
  ASSERT(slot < wrenGetSlotCount(vm), "Not that many slots.");
}

// Gets the type of the object in [slot].
WrenType wrenGetSlotType(WrenVM* vm, int slot)
{
  validateApiSlot(vm, slot);
  if (IS_BOOL(vm->apiStack[slot])) return WREN_TYPE_BOOL;
  if (IS_NUM(vm->apiStack[slot])) return WREN_TYPE_NUM;
  if (IS_FOREIGN(vm->apiStack[slot])) return WREN_TYPE_FOREIGN;
  if (IS_LIST(vm->apiStack[slot])) return WREN_TYPE_LIST;
  if (IS_MAP(vm->apiStack[slot])) return WREN_TYPE_MAP;
  if (IS_NULL(vm->apiStack[slot])) return WREN_TYPE_NULL;
  if (IS_STRING(vm->apiStack[slot])) return WREN_TYPE_STRING;
  
  return WREN_TYPE_UNKNOWN;
}

bool wrenGetSlotBool(WrenVM* vm, int slot)
{
  validateApiSlot(vm, slot);
  ASSERT(IS_BOOL(vm->apiStack[slot]), "Slot must hold a bool.");

  return AS_BOOL(vm->apiStack[slot]);
}

const char* wrenGetSlotBytes(WrenVM* vm, int slot, int* length)
{
  validateApiSlot(vm, slot);
  ASSERT(IS_STRING(vm->apiStack[slot]), "Slot must hold a string.");
  
  ObjString* string = AS_STRING(vm->apiStack[slot]);
  *length = string->length;
  return string->value;
}

double wrenGetSlotDouble(WrenVM* vm, int slot)
{
  validateApiSlot(vm, slot);
  ASSERT(IS_NUM(vm->apiStack[slot]), "Slot must hold a number.");

  return AS_NUM(vm->apiStack[slot]);
}

void* wrenGetSlotForeign(WrenVM* vm, int slot)
{
  validateApiSlot(vm, slot);
  ASSERT(IS_FOREIGN(vm->apiStack[slot]),
         "Slot must hold a foreign instance.");

  return AS_FOREIGN(vm->apiStack[slot])->data;
}

const char* wrenGetSlotString(WrenVM* vm, int slot)
{
  validateApiSlot(vm, slot);
  ASSERT(IS_STRING(vm->apiStack[slot]), "Slot must hold a string.");

  return AS_CSTRING(vm->apiStack[slot]);
}

WrenHandle* wrenGetSlotHandle(WrenVM* vm, int slot)
{
  validateApiSlot(vm, slot);
  return wrenMakeHandle(vm, vm->apiStack[slot]);
}

// Stores [value] in [slot] in the foreign call stack.
static void setSlot(WrenVM* vm, int slot, Value value)
{
  validateApiSlot(vm, slot);
  vm->apiStack[slot] = value;
}

void wrenSetSlotBool(WrenVM* vm, int slot, bool value)
{
  setSlot(vm, slot, BOOL_VAL(value));
}

void wrenSetSlotBytes(WrenVM* vm, int slot, const char* bytes, size_t length)
{
  ASSERT(bytes != NULL, "Byte array cannot be NULL.");
  setSlot(vm, slot, wrenNewStringLength(vm, bytes, length));
}

void wrenSetSlotDouble(WrenVM* vm, int slot, double value)
{
  setSlot(vm, slot, NUM_VAL(value));
}

void* wrenSetSlotNewForeign(WrenVM* vm, int slot, int classSlot, size_t size)
{
  validateApiSlot(vm, slot);
  validateApiSlot(vm, classSlot);
  ASSERT(IS_CLASS(vm->apiStack[classSlot]), "Slot must hold a class.");
  
  ObjClass* classObj = AS_CLASS(vm->apiStack[classSlot]);
  ASSERT(classObj->numFields == -1, "Class must be a foreign class.");
  
  ObjForeign* foreign = wrenNewForeign(vm, classObj, size);
  vm->apiStack[slot] = OBJ_VAL(foreign);
  
  return (void*)foreign->data;
}

void wrenSetSlotNewList(WrenVM* vm, int slot)
{
  setSlot(vm, slot, OBJ_VAL(wrenNewList(vm, 0)));
}

void wrenSetSlotNewMap(WrenVM* vm, int slot)
{
  setSlot(vm, slot, OBJ_VAL(wrenNewMap(vm)));
}

void wrenSetSlotNull(WrenVM* vm, int slot)
{
  setSlot(vm, slot, NULL_VAL);
}

void wrenSetSlotString(WrenVM* vm, int slot, const char* text)
{
  ASSERT(text != NULL, "String cannot be NULL.");
  
  setSlot(vm, slot, wrenNewString(vm, text));
}

void wrenSetSlotHandle(WrenVM* vm, int slot, WrenHandle* handle)
{
  ASSERT(handle != NULL, "Handle cannot be NULL.");

  setSlot(vm, slot, handle->value);
}

int wrenGetListCount(WrenVM* vm, int slot)
{
  validateApiSlot(vm, slot);
  ASSERT(IS_LIST(vm->apiStack[slot]), "Slot must hold a list.");
  
  ValueBuffer elements = AS_LIST(vm->apiStack[slot])->elements;
  return elements.count;
}

void wrenGetListElement(WrenVM* vm, int listSlot, int index, int elementSlot)
{
  validateApiSlot(vm, listSlot);
  validateApiSlot(vm, elementSlot);
  ASSERT(IS_LIST(vm->apiStack[listSlot]), "Slot must hold a list.");

  ValueBuffer elements = AS_LIST(vm->apiStack[listSlot])->elements;

  uint32_t usedIndex = wrenValidateIndex(elements.count, index);
  ASSERT(usedIndex != UINT32_MAX, "Index out of bounds.");

  vm->apiStack[elementSlot] = elements.data[usedIndex];
}

void wrenSetListElement(WrenVM* vm, int listSlot, int index, int elementSlot)
{
  validateApiSlot(vm, listSlot);
  validateApiSlot(vm, elementSlot);
  ASSERT(IS_LIST(vm->apiStack[listSlot]), "Slot must hold a list.");

  ObjList* list = AS_LIST(vm->apiStack[listSlot]);

  uint32_t usedIndex = wrenValidateIndex(list->elements.count, index);
  ASSERT(usedIndex != UINT32_MAX, "Index out of bounds.");
  
  list->elements.data[usedIndex] = vm->apiStack[elementSlot];
}

void wrenInsertInList(WrenVM* vm, int listSlot, int index, int elementSlot)
{
  validateApiSlot(vm, listSlot);
  validateApiSlot(vm, elementSlot);
  ASSERT(IS_LIST(vm->apiStack[listSlot]), "Must insert into a list.");
  
  ObjList* list = AS_LIST(vm->apiStack[listSlot]);
  
  // Negative indices count from the end. 
  // We don't use wrenValidateIndex here because insert allows 1 past the end.
  if (index < 0) index = list->elements.count + 1 + index;
  
  ASSERT(index <= list->elements.count, "Index out of bounds.");
  
  wrenListInsert(vm, list, vm->apiStack[elementSlot], index);
}

int wrenGetMapCount(WrenVM* vm, int slot)
{
  validateApiSlot(vm, slot);
  ASSERT(IS_MAP(vm->apiStack[slot]), "Slot must hold a map.");

  ObjMap* map = AS_MAP(vm->apiStack[slot]);
  return map->count;
}

bool wrenGetMapContainsKey(WrenVM* vm, int mapSlot, int keySlot)
{
  validateApiSlot(vm, mapSlot);
  validateApiSlot(vm, keySlot);
  ASSERT(IS_MAP(vm->apiStack[mapSlot]), "Slot must hold a map.");

  Value key = vm->apiStack[keySlot];
  ASSERT(wrenMapIsValidKey(key), "Key must be a value type");
  if (!validateKey(vm, key)) return false;

  ObjMap* map = AS_MAP(vm->apiStack[mapSlot]);
  Value value = wrenMapGet(map, key);

  return !IS_UNDEFINED(value);
}

void wrenGetMapValue(WrenVM* vm, int mapSlot, int keySlot, int valueSlot)
{
  validateApiSlot(vm, mapSlot);
  validateApiSlot(vm, keySlot);
  validateApiSlot(vm, valueSlot);
  ASSERT(IS_MAP(vm->apiStack[mapSlot]), "Slot must hold a map.");

  ObjMap* map = AS_MAP(vm->apiStack[mapSlot]);
  Value value = wrenMapGet(map, vm->apiStack[keySlot]);
  if (IS_UNDEFINED(value)) {
    value = NULL_VAL;
  }

  vm->apiStack[valueSlot] = value;
}

void wrenSetMapValue(WrenVM* vm, int mapSlot, int keySlot, int valueSlot)
{
  validateApiSlot(vm, mapSlot);
  validateApiSlot(vm, keySlot);
  validateApiSlot(vm, valueSlot);
  ASSERT(IS_MAP(vm->apiStack[mapSlot]), "Must insert into a map.");
  
  Value key = vm->apiStack[keySlot];
  ASSERT(wrenMapIsValidKey(key), "Key must be a value type");

  if (!validateKey(vm, key)) {
    return;
  }

  Value value = vm->apiStack[valueSlot];
  ObjMap* map = AS_MAP(vm->apiStack[mapSlot]);
  
  wrenMapSet(vm, map, key, value);
}

void wrenRemoveMapValue(WrenVM* vm, int mapSlot, int keySlot, 
                        int removedValueSlot)
{
  validateApiSlot(vm, mapSlot);
  validateApiSlot(vm, keySlot);
  ASSERT(IS_MAP(vm->apiStack[mapSlot]), "Slot must hold a map.");

  Value key = vm->apiStack[keySlot];
  if (!validateKey(vm, key)) {
    return;
  }

  ObjMap* map = AS_MAP(vm->apiStack[mapSlot]);
  Value removed = wrenMapRemoveKey(vm, map, key);
  setSlot(vm, removedValueSlot, removed);
}

void wrenGetVariable(WrenVM* vm, const char* module, const char* name,
                     int slot)
{
  ASSERT(module != NULL, "Module cannot be NULL.");
  ASSERT(name != NULL, "Variable name cannot be NULL.");  

  Value moduleName = wrenStringFormat(vm, "$", module);
  wrenPushRoot(vm, AS_OBJ(moduleName));
  
  ObjModule* moduleObj = getModule(vm, moduleName);
  ASSERT(moduleObj != NULL, "Could not find module.");
  
  wrenPopRoot(vm); // moduleName.

  int variableSlot = wrenSymbolTableFind(&moduleObj->variableNames,
                                         name, strlen(name));
  ASSERT(variableSlot != -1, "Could not find variable.");
  
  setSlot(vm, slot, moduleObj->variables.data[variableSlot]);
}

bool wrenHasVariable(WrenVM* vm, const char* module, const char* name)
{
  ASSERT(module != NULL, "Module cannot be NULL.");
  ASSERT(name != NULL, "Variable name cannot be NULL.");

  Value moduleName = wrenStringFormat(vm, "$", module);
  wrenPushRoot(vm, AS_OBJ(moduleName));

  //We don't use wrenHasModule since we want to use the module object.
  ObjModule* moduleObj = getModule(vm, moduleName);
  ASSERT(moduleObj != NULL, "Could not find module.");

  wrenPopRoot(vm); // moduleName.

  int variableSlot = wrenSymbolTableFind(&moduleObj->variableNames,
    name, strlen(name));

  return variableSlot != -1;
}

bool wrenHasModule(WrenVM* vm, const char* module)
{
  ASSERT(module != NULL, "Module cannot be NULL.");
  
  Value moduleName = wrenStringFormat(vm, "$", module);
  wrenPushRoot(vm, AS_OBJ(moduleName));

  ObjModule* moduleObj = getModule(vm, moduleName);
  
  wrenPopRoot(vm); // moduleName.

  return moduleObj != NULL;
}

void wrenAbortFiber(WrenVM* vm, int slot)
{
  validateApiSlot(vm, slot);
  vm->fiber->error = vm->apiStack[slot];
}

void* wrenGetUserData(WrenVM* vm)
{
	return vm->config.userData;
}

void wrenSetUserData(WrenVM* vm, void* userData)
{
	vm->config.userData = userData;
}

// Debugging -------------------------------------------------------------------

void wrenSetLineHook(WrenVM* vm, WrenLineHookFn hook)
{
  vm->lineHook = hook;
  vm->hookFiber = NULL;
}

void wrenSetInterruptHook(WrenVM* vm, WrenInterruptFn hook, int interval)
{
  ASSERT(interval > 0, "Interval must be positive.");
  vm->interruptHook = hook;
  vm->interruptInterval = hook == NULL ? INTERRUPT_OFF : interval;
  vm->interruptCountdown = vm->interruptInterval;
}

// Finds call frame [index] of the running fiber and the fibers that called it,
// innermost first. Skips the stubs the C API uses to call into Wren. Sets
// [offset] to the bytecode offset of the instruction the frame is executing.
static CallFrame* findFrame(WrenVM* vm, int index, int* offset)
{
  if (index < 0) return NULL;

  for (ObjFiber* fiber = vm->fiber; fiber != NULL; fiber = fiber->caller)
  {
    for (int i = fiber->numFrames - 1; i >= 0; i--)
    {
      CallFrame* frame = &fiber->frames[i];
      ObjFn* fn = frame->closure->fn;
      if (fn->module == NULL) continue;
      if (index-- > 0) continue;

      // A frame's ip has advanced past the instruction it last executed,
      // except in the line hook, where the innermost frame is about to start
      // its next one.
      bool starting = vm->inLineHook && fiber == vm->fiber &&
                      i == fiber->numFrames - 1;
      *offset = (int)(frame->ip - fn->code.data) - (starting ? 0 : 1);
      if (*offset < 0) *offset = 0;
      return frame;
    }
  }

  return NULL;
}

int wrenGetStackFrameCount(WrenVM* vm)
{
  int count = 0;
  for (ObjFiber* fiber = vm->fiber; fiber != NULL; fiber = fiber->caller)
  {
    for (int i = 0; i < fiber->numFrames; i++)
    {
      if (fiber->frames[i].closure->fn->module != NULL) count++;
    }
  }
  return count;
}

bool wrenGetStackFrame(WrenVM* vm, int frame, WrenStackFrame* out)
{
  ASSERT(out != NULL, "Frame output cannot be NULL.");

  int offset;
  CallFrame* callFrame = findFrame(vm, frame, &offset);
  if (callFrame == NULL) return false;

  ObjFn* fn = callFrame->closure->fn;
  out->module = fn->module->name == NULL ? NULL : fn->module->name->value;
  out->function = fn->debug->name;
  out->line = fn->debug->sourceLines.data[offset];
  return true;
}

// Returns whether [variable] of a function is visible at bytecode [offset].
static bool isVariableVisible(FnVariable* variable, int offset)
{
  // Captured variables are visible for the whole function.
  if (variable->start == -1) return true;
  return offset >= variable->start && offset < variable->end;
}

// Finds visible variable [index] of [frame] at bytecode [offset].
static FnVariable* findVariable(CallFrame* frame, int offset, int index)
{
  FnVariableBuffer* variables = &frame->closure->fn->debug->variables;
  for (int i = 0; i < variables->count; i++)
  {
    FnVariable* variable = &variables->data[i];
    if (isVariableVisible(variable, offset) && index-- == 0) return variable;
  }
  return NULL;
}

int wrenGetFrameVariableCount(WrenVM* vm, int frame)
{
  int offset;
  CallFrame* callFrame = findFrame(vm, frame, &offset);
  if (callFrame == NULL) return 0;

  FnVariableBuffer* variables = &callFrame->closure->fn->debug->variables;
  int count = 0;
  for (int i = 0; i < variables->count; i++)
  {
    if (isVariableVisible(&variables->data[i], offset)) count++;
  }
  return count;
}

const char* wrenGetFrameVariable(WrenVM* vm, int frame, int index, int slot)
{
  validateApiSlot(vm, slot);

  int offset;
  CallFrame* callFrame = findFrame(vm, frame, &offset);
  if (callFrame == NULL) return NULL;

  FnVariable* variable = findVariable(callFrame, offset, index);
  if (variable == NULL) return NULL;

  if (variable->start == -1)
  {
    setSlot(vm, slot, *callFrame->closure->upvalues[variable->index]->value);
  }
  else
  {
    setSlot(vm, slot, callFrame->stackStart[variable->index]);
  }
  return variable->name;
}

// Looks up the resolved module named [module], or returns NULL.
static ObjModule* findModuleByName(WrenVM* vm, const char* module)
{
  ASSERT(module != NULL, "Module cannot be NULL.");

  Value moduleName = wrenStringFormat(vm, "$", module);
  wrenPushRoot(vm, AS_OBJ(moduleName));
  ObjModule* moduleObj = getModule(vm, moduleName);
  wrenPopRoot(vm); // moduleName.
  return moduleObj;
}

int wrenGetModuleVariableCount(WrenVM* vm, const char* module)
{
  ObjModule* moduleObj = findModuleByName(vm, module);
  return moduleObj == NULL ? -1 : moduleObj->variables.count;
}

const char* wrenGetModuleVariableAt(WrenVM* vm, const char* module, int index,
                                    int slot)
{
  validateApiSlot(vm, slot);

  ObjModule* moduleObj = findModuleByName(vm, module);
  if (moduleObj == NULL) return NULL;
  if (index < 0 || index >= moduleObj->variables.count) return NULL;

  setSlot(vm, slot, moduleObj->variables.data[index]);
  return moduleObj->variableNames.data[index]->value;
}

const char* wrenGetSlotClassName(WrenVM* vm, int slot)
{
  validateApiSlot(vm, slot);
  return wrenGetClassInline(vm, vm->apiStack[slot])->name->value;
}

// Live code replacement -------------------------------------------------------

// Returns the class stored in top-level variable [name] of [module], or NULL.
static ObjClass* findModuleClass(ObjModule* module, const char* name)
{
  int symbol = wrenSymbolTableFind(&module->variableNames, name, strlen(name));
  if (symbol == -1) return NULL;
  Value value = module->variables.data[symbol];
  return IS_CLASS(value) ? AS_CLASS(value) : NULL;
}

static Method methodAt(ObjClass* classObj, int symbol)
{
  if (symbol < classObj->methods.count) return classObj->methods.data[symbol];
  Method none;
  none.type = METHOD_NONE;
  return none;
}

static bool sameMethod(Method a, Method b)
{
  if (a.type != b.type) return false;
  switch (a.type)
  {
    case METHOD_PRIMITIVE:
    case METHOD_FUNCTION_CALL: return a.as.primitive == b.as.primitive;
    case METHOD_FOREIGN:       return a.as.foreign == b.as.foreign;
    case METHOD_BLOCK:         return a.as.closure == b.as.closure;
    case METHOD_NONE:          return true;
  }
  return false;
}

// True if [classObj] defines method [symbol] itself rather than inheriting it.
// Inheriting copies the superclass's entry, so an inherited method is the
// very same entry as the superclass's.
static bool definesMethod(ObjClass* classObj, int symbol)
{
  Method method = methodAt(classObj, symbol);
  if (method.type == METHOD_NONE) return false;
  if (classObj->superclass == NULL) return true;
  return !sameMethod(method, methodAt(classObj->superclass, symbol));
}

static int maxMethodCount(ObjClass* a, ObjClass* b)
{
  return a->methods.count > b->methods.count ? a->methods.count
                                              : b->methods.count;
}

// Copies the [index]th space-separated name of [names] into the VM's detail
// buffer and returns it.
static const char* fieldNameDetail(WrenVM* vm, ObjString* names, int index)
{
  const char* at = names->value;
  const char* end = names->value + names->length;
  for (int i = 0; i < index; i++) at = strchr(at, ' ') + 1;
  const char* stop = strchr(at, ' ');
  if (stop == NULL) stop = end;
  snprintf(vm->replaceDetail, sizeof(vm->replaceDetail), "%.*s",
           (int)(stop - at), at);
  return vm->replaceDetail;
}

// Returns the index of the first field name that differs between [a] and [b]
// (either may be NULL for no fields), or -1 if they are the same.
static int firstFieldDifference(ObjString* a, ObjString* b, ObjString** in)
{
  const char* p = a == NULL ? "" : a->value;
  const char* q = b == NULL ? "" : b->value;
  int index = 0;
  for (;;)
  {
    if (*p == '\0' && *q == '\0') return -1;
    const char* pEnd = strchr(p, ' ');
    const char* qEnd = strchr(q, ' ');
    size_t pLength = pEnd == NULL ? strlen(p) : (size_t)(pEnd - p);
    size_t qLength = qEnd == NULL ? strlen(q) : (size_t)(qEnd - q);
    if (pLength != qLength || memcmp(p, q, pLength) != 0)
    {
      // Name the field the replacement declares, unless it declares none.
      *in = *q != '\0' ? b : a;
      return index;
    }
    p += pLength + (pEnd == NULL ? 0 : 1);
    q += qLength + (qEnd == NULL ? 0 : 1);
    index++;
  }
}

// Returns the upvalue named [name] captured by a method [classObj] defines
// itself, or NULL.
static ObjUpvalue* findMethodUpvalue(ObjClass* classObj, const char* name)
{
  for (int symbol = 0; symbol < classObj->methods.count; symbol++)
  {
    if (!definesMethod(classObj, symbol)) continue;
    Method method = classObj->methods.data[symbol];
    if (method.type != METHOD_BLOCK) continue;

    ObjClosure* closure = method.as.closure;
    FnVariableBuffer* variables = &closure->fn->debug->variables;
    for (int i = 0; i < variables->count; i++)
    {
      FnVariable* variable = &variables->data[i];
      if (variable->start != -1) continue; // A local, not a captured variable.
      if (strcmp(variable->name, name) == 0)
      {
        return closure->upvalues[variable->index];
      }
    }
  }
  return NULL;
}

// Points the captured variables of [source]'s own methods at the variables of
// the same name that [target]'s methods captured. In a class body these are
// the static fields, so the replacement keeps their values.
static void shareUpvalues(ObjClass* target, ObjClass* source)
{
  for (int symbol = 0; symbol < source->methods.count; symbol++)
  {
    if (!definesMethod(source, symbol)) continue;
    Method method = source->methods.data[symbol];
    if (method.type != METHOD_BLOCK) continue;

    ObjClosure* closure = method.as.closure;
    FnVariableBuffer* variables = &closure->fn->debug->variables;
    for (int i = 0; i < variables->count; i++)
    {
      FnVariable* variable = &variables->data[i];
      if (variable->start != -1) continue;
      ObjUpvalue* shared = findMethodUpvalue(target, variable->name);
      if (shared != NULL) closure->upvalues[variable->index] = shared;
    }
  }
}

static bool inheritsFrom(ObjClass* classObj, ObjClass* ancestor)
{
  for (ObjClass* c = classObj->superclass; c != NULL; c = c->superclass)
  {
    if (c == ancestor) return true;
  }
  return false;
}

// Reports the first method [target] defines and [source] does not.
static bool findRemovedMethod(WrenVM* vm, ObjClass* target, ObjClass* source,
                              bool isStatic, const char** detail)
{
  int count = maxMethodCount(target, source);
  for (int symbol = 0; symbol < count; symbol++)
  {
    if (!definesMethod(target, symbol) || definesMethod(source, symbol))
    {
      continue;
    }
    if (detail != NULL)
    {
      snprintf(vm->replaceDetail, sizeof(vm->replaceDetail), "%s%s",
               isStatic ? "static " : "",
               vm->methodNames.data[symbol]->value);
      *detail = vm->replaceDetail;
    }
    return true;
  }
  return false;
}

// Binds every method [source] defines into [target]. For instance methods
// ([subclasses] true), a subclass of [target] that still has the entry
// [target] had inherits the new one too.
static void replaceMethods(WrenVM* vm, ObjClass* target, ObjClass* source,
                           bool subclasses)
{
  for (int symbol = 0; symbol < source->methods.count; symbol++)
  {
    if (!definesMethod(source, symbol)) continue;
    Method old = methodAt(target, symbol);
    Method method = source->methods.data[symbol];
    wrenBindMethod(vm, target, symbol, method);
    if (!subclasses) continue;

    // Binding may grow a method table and collect garbage. The caller
    // collected first, so every class on the list is reachable and stays so;
    // only objects that cannot be classes (the replaced closures) may be
    // freed while we walk.
    for (Obj* obj = vm->first; obj != NULL; obj = obj->next)
    {
      if (obj->type != OBJ_CLASS) continue;
      ObjClass* subclass = (ObjClass*)obj;
      if (!inheritsFrom(subclass, target)) continue;
      if (!sameMethod(methodAt(subclass, symbol), old)) continue;
      wrenBindMethod(vm, subclass, symbol, method);
    }
  }
}

WrenReplaceResult wrenReplaceMethods(WrenVM* vm, const char* module,
                                     const char* target, const char* source,
                                     const char** detail)
{
  ASSERT(target != NULL, "Target class name cannot be NULL.");
  ASSERT(source != NULL, "Source class name cannot be NULL.");
  if (detail != NULL) *detail = NULL;

  ObjModule* moduleObj = findModuleByName(vm, module);
  if (moduleObj == NULL) return WREN_REPLACE_NOT_FOUND;
  ObjClass* targetClass = findModuleClass(moduleObj, target);
  ObjClass* sourceClass = findModuleClass(moduleObj, source);
  if (targetClass == NULL || sourceClass == NULL) return WREN_REPLACE_NOT_FOUND;

  if (targetClass == sourceClass ||
      targetClass->numFields == -1 || sourceClass->numFields == -1)
  {
    return WREN_REPLACE_UNSUPPORTED;
  }

  if (targetClass->superclass != sourceClass->superclass)
  {
    return WREN_REPLACE_SUPERCLASS_CHANGED;
  }

  ObjString* names = NULL;
  int field = firstFieldDifference(targetClass->fieldNames,
                                   sourceClass->fieldNames, &names);
  if (field != -1)
  {
    if (detail != NULL) *detail = fieldNameDetail(vm, names, field);
    return WREN_REPLACE_FIELDS_CHANGED;
  }

  ObjClass* targetMeta = targetClass->obj.classObj;
  ObjClass* sourceMeta = sourceClass->obj.classObj;
  if (findRemovedMethod(vm, targetClass, sourceClass, false, detail) ||
      findRemovedMethod(vm, targetMeta, sourceMeta, true, detail))
  {
    return WREN_REPLACE_METHOD_REMOVED;
  }

  // Every check passed: nothing below can fail.
  shareUpvalues(targetClass, sourceClass);
  shareUpvalues(targetMeta, sourceMeta);
  wrenCollectGarbage(vm);
  replaceMethods(vm, targetClass, sourceClass, true);
  // Static methods are not inherited: a metaclass's superclass is Class.
  replaceMethods(vm, targetMeta, sourceMeta, false);
  targetClass->attributes = sourceClass->attributes;
  return WREN_REPLACE_SUCCESS;
}

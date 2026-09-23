// The main bytecode interpreter loop. This is where the magic happens. It is
// also, as you can imagine, highly performance critical.
//
// This file is included twice by wren_vm.c. With WREN_INTERPRETER_HOOKED 0 it
// is the plain interpreter, which does no debugger work at all. With 1 it
// calls the line hook before each new line. Each loop hands over to the other
// by returning INTERPRETER_SWITCH when a hook is installed or removed. Both
// count loop iterations and calls for the interrupt hook.
static int WREN_INTERPRETER_NAME(WrenVM* vm, register ObjFiber* fiber)
{
  // Hoist these into local variables. They are accessed frequently in the loop
  // but assigned less frequently. Keeping them in locals and updating them when
  // a call frame has been pushed or popped gives a large speed boost.
  register CallFrame* frame;
  register Value* stackStart;
  register uint8_t* ip;
  register ObjFn* fn;

  // These macros are designed to only be invoked within this function.
  #define PUSH(value)  (*fiber->stackTop++ = value)
  #define POP()        (*(--fiber->stackTop))
  #define DROP()       (fiber->stackTop--)
  #define PEEK()       (*(fiber->stackTop - 1))
  #define PEEK2()      (*(fiber->stackTop - 2))
  #define READ_BYTE()  (*ip++)
  #define READ_SHORT() (ip += 2, (uint16_t)((ip[-2] << 8) | ip[-1]))

  // Use this before a CallFrame is pushed to store the local variables back
  // into the current one.
  #define STORE_FRAME() frame->ip = ip

  // Use this after a CallFrame has been pushed or popped to refresh the local
  // variables.
  #define LOAD_FRAME()                                                         \
      do                                                                       \
      {                                                                        \
        frame = &fiber->frames[fiber->numFrames - 1];                          \
        stackStart = frame->stackStart;                                        \
        ip = frame->ip;                                                        \
        fn = frame->closure->fn;                                               \
      } while (false)

  // Terminates the current fiber with error string [error]. If another calling
  // fiber is willing to catch the error, transfers control to it, otherwise
  // exits the interpreter.
  #define RUNTIME_ERROR()                                                      \
      do                                                                       \
      {                                                                        \
        STORE_FRAME();                                                         \
        runtimeError(vm);                                                      \
        if (vm->fiber == NULL) return WREN_RESULT_RUNTIME_ERROR;               \
        fiber = vm->fiber;                                                     \
        LOAD_FRAME();                                                          \
        DISPATCH();                                                            \
      } while (false)

  #if WREN_DEBUG_TRACE_INSTRUCTIONS
    // Prints the stack and instruction before each instruction is executed.
    #define DEBUG_TRACE_INSTRUCTIONS()                                         \
        do                                                                     \
        {                                                                      \
          wrenDumpStack(fiber);                                                \
          wrenDumpInstruction(vm, fn, (int)(ip - fn->code.data));              \
        } while (false)
  #else
    #define DEBUG_TRACE_INSTRUCTIONS() do { } while (false)
  #endif

  // Hands execution to the other interpreter loop, from the current ip.
  #define SWITCH_LOOP()                                                        \
      do                                                                       \
      {                                                                        \
        STORE_FRAME();                                                         \
        return INTERPRETER_SWITCH;                                             \
      } while (false)

  // True when the installed hook no longer matches this loop.
  #define HOOK_CHANGED() ((vm->lineHook != NULL) != WREN_INTERPRETER_HOOKED)

  // A safepoint, before each loop back-jump and each call: every
  // interruptInterval of them, the interrupt hook runs. It sees the frame as
  // a runtime error would, with the jump or call as the last instruction, so
  // an abort reports the line of the loop or call. It may also have moved the
  // stack, so [reload] refreshes pointers into it; or installed or removed the
  // line hook, and then the remaining arguments, which finish the jump or
  // call, run before handing over to the other loop.
  #define INTERRUPT_CHECK(reload, ...)                                         \
      do                                                                       \
      {                                                                        \
        if (--vm->interruptCountdown <= 0)                                     \
        {                                                                      \
          STORE_FRAME();                                                       \
          callInterruptHook(vm, fiber);                                        \
          if (wrenHasError(fiber)) RUNTIME_ERROR();                            \
          LOAD_FRAME();                                                        \
          reload;                                                              \
          if (HOOK_CHANGED())                                                  \
          {                                                                    \
            __VA_ARGS__;                                                       \
            SWITCH_LOOP();                                                     \
          }                                                                    \
        }                                                                      \
      } while (false)

  #if WREN_COMPUTED_GOTO

  static void* dispatchTable[] = {
    #define OPCODE(name, _) &&code_##name,
    #include "wren_opcodes.h"
    #undef OPCODE
  };

  #define INTERPRET_LOOP    DISPATCH();
  #define CASE_CODE(name)   code_##name

  #define DISPATCH_INSTRUCTION()                                               \
      goto *dispatchTable[instruction = (Code)READ_BYTE()]

  #if WREN_INTERPRETER_HOOKED
    #define DISPATCH()                                                         \
        do                                                                     \
        {                                                                      \
          DEBUG_TRACE_INSTRUCTIONS();                                          \
          goto lineHook;                                                       \
        } while (false)
  #else
    #define DISPATCH()                                                         \
        do                                                                     \
        {                                                                      \
          DEBUG_TRACE_INSTRUCTIONS();                                          \
          DISPATCH_INSTRUCTION();                                              \
        } while (false)
  #endif

  #else

  #if WREN_INTERPRETER_HOOKED
    #define INTERPRET_LOOP                                                     \
        loop:                                                                  \
          DEBUG_TRACE_INSTRUCTIONS();                                          \
          goto lineHook;                                                       \
        dispatchInstruction:                                                   \
          switch (instruction = (Code)READ_BYTE())

    #define DISPATCH_INSTRUCTION() goto dispatchInstruction
  #else
    #define INTERPRET_LOOP                                                     \
        loop:                                                                  \
          DEBUG_TRACE_INSTRUCTIONS();                                          \
          switch (instruction = (Code)READ_BYTE())
  #endif

  #define CASE_CODE(name)  case CODE_##name
  #define DISPATCH()       goto loop

  #endif

  LOAD_FRAME();

  Code instruction;
  INTERPRET_LOOP
  {
    CASE_CODE(LOAD_LOCAL_0):
    CASE_CODE(LOAD_LOCAL_1):
    CASE_CODE(LOAD_LOCAL_2):
    CASE_CODE(LOAD_LOCAL_3):
    CASE_CODE(LOAD_LOCAL_4):
    CASE_CODE(LOAD_LOCAL_5):
    CASE_CODE(LOAD_LOCAL_6):
    CASE_CODE(LOAD_LOCAL_7):
    CASE_CODE(LOAD_LOCAL_8):
      PUSH(stackStart[instruction - CODE_LOAD_LOCAL_0]);
      DISPATCH();

    CASE_CODE(LOAD_LOCAL):
      PUSH(stackStart[READ_BYTE()]);
      DISPATCH();

    CASE_CODE(LOAD_FIELD_THIS):
    {
      uint8_t field = READ_BYTE();
      Value receiver = stackStart[0];
      ASSERT(IS_INSTANCE(receiver), "Receiver should be instance.");
      ObjInstance* instance = AS_INSTANCE(receiver);
      ASSERT(field < instance->obj.classObj->numFields, "Out of bounds field.");
      PUSH(instance->fields[field]);
      DISPATCH();
    }

    CASE_CODE(POP):   DROP(); DISPATCH();
    CASE_CODE(NULL):  PUSH(NULL_VAL); DISPATCH();
    CASE_CODE(FALSE): PUSH(FALSE_VAL); DISPATCH();
    CASE_CODE(TRUE):  PUSH(TRUE_VAL); DISPATCH();

    CASE_CODE(STORE_LOCAL):
      stackStart[READ_BYTE()] = PEEK();
      DISPATCH();

    CASE_CODE(CONSTANT):
      PUSH(fn->constants.data[READ_SHORT()]);
      DISPATCH();

    {
      // The opcodes for doing method and superclass calls share a lot of code.
      // However, doing an if() test in the middle of the instruction sequence
      // to handle the bit that is special to super calls makes the non-super
      // call path noticeably slower.
      //
      // Instead, we do this old school using an explicit goto to share code for
      // everything at the tail end of the call-handling code that is the same
      // between normal and superclass calls.
      int numArgs;
      int symbol;

      Value* args;
      ObjClass* classObj;

      Method* method;

    CASE_CODE(CALL_0):
    CASE_CODE(CALL_1):
    CASE_CODE(CALL_2):
    CASE_CODE(CALL_3):
    CASE_CODE(CALL_4):
    CASE_CODE(CALL_5):
    CASE_CODE(CALL_6):
    CASE_CODE(CALL_7):
    CASE_CODE(CALL_8):
    CASE_CODE(CALL_9):
    CASE_CODE(CALL_10):
    CASE_CODE(CALL_11):
    CASE_CODE(CALL_12):
    CASE_CODE(CALL_13):
    CASE_CODE(CALL_14):
    CASE_CODE(CALL_15):
    CASE_CODE(CALL_16):
      // Add one for the implicit receiver argument.
      numArgs = instruction - CODE_CALL_0 + 1;
      symbol = READ_SHORT();

      // The receiver is the first argument.
      args = fiber->stackTop - numArgs;
      classObj = wrenGetClassInline(vm, args[0]);
      goto completeCall;

    CASE_CODE(SUPER_0):
    CASE_CODE(SUPER_1):
    CASE_CODE(SUPER_2):
    CASE_CODE(SUPER_3):
    CASE_CODE(SUPER_4):
    CASE_CODE(SUPER_5):
    CASE_CODE(SUPER_6):
    CASE_CODE(SUPER_7):
    CASE_CODE(SUPER_8):
    CASE_CODE(SUPER_9):
    CASE_CODE(SUPER_10):
    CASE_CODE(SUPER_11):
    CASE_CODE(SUPER_12):
    CASE_CODE(SUPER_13):
    CASE_CODE(SUPER_14):
    CASE_CODE(SUPER_15):
    CASE_CODE(SUPER_16):
      // Add one for the implicit receiver argument.
      numArgs = instruction - CODE_SUPER_0 + 1;
      symbol = READ_SHORT();

      // The receiver is the first argument.
      args = fiber->stackTop - numArgs;

      // The superclass is stored in a constant.
      classObj = AS_CLASS(fn->constants.data[READ_SHORT()]);
      goto completeCall;

    completeCall:
      // If the class's method table doesn't include the symbol, bail.
      if (symbol >= classObj->methods.count ||
          (method = &classObj->methods.data[symbol])->type == METHOD_NONE)
      {
        methodNotFound(vm, classObj, symbol);
        RUNTIME_ERROR();
      }

      switch (method->type)
      {
        case METHOD_PRIMITIVE:
          if (method->as.primitive(vm, args))
          {
            // The result is now in the first arg slot. Discard the other
            // stack slots.
            fiber->stackTop -= numArgs - 1;
          } else {
            // An error, fiber switch, or call frame change occurred.
            STORE_FRAME();

            // If we don't have a fiber to switch to, stop interpreting.
            fiber = vm->fiber;
            if (fiber == NULL) return WREN_RESULT_SUCCESS;
            if (wrenHasError(fiber)) RUNTIME_ERROR();
            LOAD_FRAME();
          }
          break;

        case METHOD_FUNCTION_CALL: 
          if (!checkArity(vm, args[0], numArgs)) {
            RUNTIME_ERROR();
            break;
          }

          INTERRUPT_CHECK(args = fiber->stackTop - numArgs, {
            STORE_FRAME();
            method->as.primitive(vm, args);
            LOAD_FRAME();
          });
          STORE_FRAME();
          method->as.primitive(vm, args);
          LOAD_FRAME();
          break;

        case METHOD_FOREIGN:
          // Let the foreign method walk the stack from the current line.
          STORE_FRAME();
          callForeign(vm, fiber, method->as.foreign, numArgs);
          if (wrenHasError(fiber)) RUNTIME_ERROR();

          // The foreign method may have installed or removed the line hook.
          if (HOOK_CHANGED()) SWITCH_LOOP();
          break;

        case METHOD_BLOCK:
          INTERRUPT_CHECK(args = fiber->stackTop - numArgs, {
            STORE_FRAME();
            wrenCallFunction(vm, fiber, (ObjClosure*)method->as.closure, numArgs);
            LOAD_FRAME();
          });
          STORE_FRAME();
          wrenCallFunction(vm, fiber, (ObjClosure*)method->as.closure, numArgs);
          LOAD_FRAME();
          break;

        case METHOD_NONE:
          UNREACHABLE();
          break;
      }
      DISPATCH();
    }

    CASE_CODE(LOAD_UPVALUE):
    {
      ObjUpvalue** upvalues = frame->closure->upvalues;
      PUSH(*upvalues[READ_BYTE()]->value);
      DISPATCH();
    }

    CASE_CODE(STORE_UPVALUE):
    {
      ObjUpvalue** upvalues = frame->closure->upvalues;
      *upvalues[READ_BYTE()]->value = PEEK();
      DISPATCH();
    }

    CASE_CODE(LOAD_MODULE_VAR):
      PUSH(fn->module->variables.data[READ_SHORT()]);
      DISPATCH();

    CASE_CODE(STORE_MODULE_VAR):
      fn->module->variables.data[READ_SHORT()] = PEEK();
      DISPATCH();

    CASE_CODE(STORE_FIELD_THIS):
    {
      uint8_t field = READ_BYTE();
      Value receiver = stackStart[0];
      ASSERT(IS_INSTANCE(receiver), "Receiver should be instance.");
      ObjInstance* instance = AS_INSTANCE(receiver);
      ASSERT(field < instance->obj.classObj->numFields, "Out of bounds field.");
      instance->fields[field] = PEEK();
      DISPATCH();
    }

    CASE_CODE(LOAD_FIELD):
    {
      uint8_t field = READ_BYTE();
      Value receiver = POP();
      ASSERT(IS_INSTANCE(receiver), "Receiver should be instance.");
      ObjInstance* instance = AS_INSTANCE(receiver);
      ASSERT(field < instance->obj.classObj->numFields, "Out of bounds field.");
      PUSH(instance->fields[field]);
      DISPATCH();
    }

    CASE_CODE(STORE_FIELD):
    {
      uint8_t field = READ_BYTE();
      Value receiver = POP();
      ASSERT(IS_INSTANCE(receiver), "Receiver should be instance.");
      ObjInstance* instance = AS_INSTANCE(receiver);
      ASSERT(field < instance->obj.classObj->numFields, "Out of bounds field.");
      instance->fields[field] = PEEK();
      DISPATCH();
    }

    CASE_CODE(JUMP):
    {
      uint16_t offset = READ_SHORT();
      ip += offset;
      DISPATCH();
    }

    CASE_CODE(LOOP):
    {
      // Jump back to the top of the loop.
      uint16_t offset = READ_SHORT();
      INTERRUPT_CHECK((void)0, ip -= offset);
      ip -= offset;

      #if WREN_INTERPRETER_HOOKED
        // Each iteration reports its first line, even if it is this one.
        vm->hookLine = -1;
      #endif
      DISPATCH();
    }

    CASE_CODE(JUMP_IF):
    {
      uint16_t offset = READ_SHORT();
      Value condition = POP();

      if (wrenIsFalsyValue(condition)) ip += offset;
      DISPATCH();
    }

    CASE_CODE(AND):
    {
      uint16_t offset = READ_SHORT();
      Value condition = PEEK();

      if (wrenIsFalsyValue(condition))
      {
        // Short-circuit the right hand side.
        ip += offset;
      }
      else
      {
        // Discard the condition and evaluate the right hand side.
        DROP();
      }
      DISPATCH();
    }

    CASE_CODE(OR):
    {
      uint16_t offset = READ_SHORT();
      Value condition = PEEK();

      if (wrenIsFalsyValue(condition))
      {
        // Discard the condition and evaluate the right hand side.
        DROP();
      }
      else
      {
        // Short-circuit the right hand side.
        ip += offset;
      }
      DISPATCH();
    }

    CASE_CODE(CLOSE_UPVALUE):
      // Close the upvalue for the local if we have one.
      closeUpvalues(fiber, fiber->stackTop - 1);
      DROP();
      DISPATCH();

    CASE_CODE(RETURN):
    {
      Value result = POP();
      fiber->numFrames--;

      // Close any upvalues still in scope.
      closeUpvalues(fiber, stackStart);

      // If the fiber is complete, end it.
      if (fiber->numFrames == 0)
      {
        // See if there's another fiber to return to. If not, we're done.
        if (fiber->caller == NULL)
        {
          // Store the final result value at the beginning of the stack so the
          // C API can get it.
          fiber->stack[0] = result;
          fiber->stackTop = fiber->stack + 1;
          return WREN_RESULT_SUCCESS;
        }
        
        ObjFiber* resumingFiber = fiber->caller;
        fiber->caller = NULL;
        fiber = resumingFiber;
        vm->fiber = resumingFiber;
        
        // Store the result in the resuming fiber.
        fiber->stackTop[-1] = result;
      }
      else
      {
        // Store the result of the block in the first slot, which is where the
        // caller expects it.
        stackStart[0] = result;

        // Discard the stack slots for the call frame (leaving one slot for the
        // result).
        fiber->stackTop = frame->stackStart + 1;
      }
      
      LOAD_FRAME();

      #if WREN_INTERPRETER_HOOKED
        // The caller resumes partway through a line the hook already reported.
        vm->hookFiber = fiber;
        vm->hookDepth = fiber->numFrames;
        vm->hookLine = fn->debug->sourceLines.data[ip - fn->code.data - 1];
      #endif
      DISPATCH();
    }

    CASE_CODE(CONSTRUCT):
      ASSERT(IS_CLASS(stackStart[0]), "'this' should be a class.");
      stackStart[0] = wrenNewInstance(vm, AS_CLASS(stackStart[0]));
      DISPATCH();

    CASE_CODE(FOREIGN_CONSTRUCT):
      ASSERT(IS_CLASS(stackStart[0]), "'this' should be a class.");
      createForeign(vm, fiber, stackStart);
      if (wrenHasError(fiber)) RUNTIME_ERROR();
      DISPATCH();

    CASE_CODE(CLOSURE):
    {
      // Create the closure and push it on the stack before creating upvalues
      // so that it doesn't get collected.
      ObjFn* function = AS_FN(fn->constants.data[READ_SHORT()]);
      ObjClosure* closure = wrenNewClosure(vm, function);
      PUSH(OBJ_VAL(closure));

      // Capture upvalues, if any.
      for (int i = 0; i < function->numUpvalues; i++)
      {
        uint8_t isLocal = READ_BYTE();
        uint8_t index = READ_BYTE();
        if (isLocal)
        {
          // Make an new upvalue to close over the parent's local variable.
          closure->upvalues[i] = captureUpvalue(vm, fiber,
                                                frame->stackStart + index);
        }
        else
        {
          // Use the same upvalue as the current call frame.
          closure->upvalues[i] = frame->closure->upvalues[index];
        }
      }
      DISPATCH();
    }

    CASE_CODE(END_CLASS):
    {
      endClass(vm);
      if (wrenHasError(fiber)) RUNTIME_ERROR();
      DISPATCH();
    }

    CASE_CODE(CLASS):
    {
      createClass(vm, READ_BYTE(), NULL);
      if (wrenHasError(fiber)) RUNTIME_ERROR();
      DISPATCH();
    }

    CASE_CODE(FOREIGN_CLASS):
    {
      createClass(vm, -1, fn->module);
      if (wrenHasError(fiber)) RUNTIME_ERROR();
      DISPATCH();
    }

    CASE_CODE(METHOD_INSTANCE):
    CASE_CODE(METHOD_STATIC):
    {
      uint16_t symbol = READ_SHORT();
      ObjClass* classObj = AS_CLASS(PEEK());
      Value method = PEEK2();
      bindMethod(vm, instruction, symbol, fn->module, classObj, method);
      if (wrenHasError(fiber)) RUNTIME_ERROR();
      DROP();
      DROP();
      DISPATCH();
    }
    
    CASE_CODE(END_MODULE):
    {
      vm->lastModule = fn->module;
      PUSH(NULL_VAL);
      DISPATCH();
    }
    
    CASE_CODE(IMPORT_MODULE):
    {
      // Make a slot on the stack for the module's fiber to place the return
      // value. It will be popped after this fiber is resumed. Store the
      // imported module's closure in the slot in case a GC happens when
      // invoking the closure.
      PUSH(importModule(vm, fn->constants.data[READ_SHORT()]));
      if (wrenHasError(fiber)) RUNTIME_ERROR();
      
      // If we get a closure, call it to execute the module body.
      if (IS_CLOSURE(PEEK()))
      {
        STORE_FRAME();
        ObjClosure* closure = AS_CLOSURE(PEEK());
        wrenCallFunction(vm, fiber, closure, 1);
        LOAD_FRAME();
      }
      else
      {
        // The module has already been loaded. Remember it so we can import
        // variables from it if needed.
        vm->lastModule = AS_MODULE(PEEK());
      }

      DISPATCH();
    }
    
    CASE_CODE(IMPORT_VARIABLE):
    {
      Value variable = fn->constants.data[READ_SHORT()];
      ASSERT(vm->lastModule != NULL, "Should have already imported module.");
      Value result = getModuleVariable(vm, vm->lastModule, variable);
      if (wrenHasError(fiber)) RUNTIME_ERROR();

      PUSH(result);
      DISPATCH();
    }

    CASE_CODE(END):
      // A CODE_END should always be preceded by a CODE_RETURN. If we get here,
      // the compiler generated wrong code.
      UNREACHABLE();

  #if WREN_INTERPRETER_HOOKED
    // Every instruction passes through here before it is dispatched.
    lineHook:
    {
      // The hook may have removed itself, or a foreign method removed it
      // before an error moved execution to another fiber.
      if (HOOK_CHANGED()) SWITCH_LOOP();

      int line = fn->debug->sourceLines.data[ip - fn->code.data];
      if (line != vm->hookLine || fiber != vm->hookFiber ||
          fiber->numFrames != vm->hookDepth)
      {
        STORE_FRAME();
        callLineHook(vm, fiber, fn, line);
        if (wrenHasError(fiber)) RUNTIME_ERROR();

        // The hook may have moved the stack.
        LOAD_FRAME();
      }
      DISPATCH_INSTRUCTION();
    }
  #endif
  }

  // We should only exit this function from an explicit return from CODE_RETURN
  // or a runtime error.
  UNREACHABLE();
  return WREN_RESULT_RUNTIME_ERROR;

  #undef PUSH
  #undef POP
  #undef DROP
  #undef PEEK
  #undef PEEK2
  #undef READ_BYTE
  #undef READ_SHORT
  #undef STORE_FRAME
  #undef LOAD_FRAME
  #undef RUNTIME_ERROR
  #undef DEBUG_TRACE_INSTRUCTIONS
  #undef SWITCH_LOOP
  #undef HOOK_CHANGED
  #undef INTERRUPT_CHECK
  #undef INTERPRET_LOOP
  #undef CASE_CODE
  #undef DISPATCH
  #undef DISPATCH_INSTRUCTION
}



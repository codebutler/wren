#include "wren.h"

WrenForeignMethodFn foreignGrowsStackBindMethod(const char* signature);
void foreignGrowsStackBindClass(
    const char* className, WrenForeignClassMethods* methods);

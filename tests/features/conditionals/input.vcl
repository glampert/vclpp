; Conditional directives, as in C.
#define FEATURE 1

#vuprog
#if FEATURE && MODE == 2
    iaddiu vi01, vi00, 1
#elif MODE == 3
    not_taken
#else
    not_taken
#endif

#ifdef WITH_LIGHTS
    lq fLight, 0(vi00)
#endif
#ifndef WITH_LIGHTS
    not_taken
#endif

#if 0
    ; Skipped code is not looked at, so it does not have to be valid.
#unknown_directive
#include "does_not_exist.i"
#macro Skipped
    not_taken
#endmacro
#error not reached
#if 1
    not_taken
#endif
#elif defined(WITH_LIGHTS) && !defined NOT_DEFINED
    iaddiu vi02, vi00, 2
#endif

#if (MODE << 2) + 1 == 9 ? true : false
    iaddiu vi03, vi00, 3
#endif
#endvuprog

; "file" is looked for next to the file including it, then in the -I
; directories in order; <file> only in the -I directories.
#include "local.i"
#include "shared.i"
#include <shared.i>
#include "guarded.i"
#include "guarded.i"
#include "sub/nested.i"
#vuprog
    iaddiu vi01, vi00, kLocal
    iaddiu vi02, vi00, kShared
    iaddiu vi03, vi00, kGuarded
    iaddiu vi04, vi00, kDeeper
#endvuprog

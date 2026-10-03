; A program with CRLF line endings, as saved on Windows, using macros from a file
; with LF ones. No directive operand or macro argument picks up the CR, and output
; lines end in LF alone. (vclpp 1 rejected a CRLF #endmacro, hence the LF file.)
#include "macros.i"
#define kBase 8

#vuprog VU1Prog_CrlfLineEndings
    Load{ fA, kBase }
    iaddiu iPtr, vi00, kBase
#endvuprog

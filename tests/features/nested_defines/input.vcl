; Constants can be made of other constants, defined before or after them:
; a macro is expanded, and its expansion rescanned, where it is used.
#define kWindowA      189
#define kWindowB      (kWindowA + kWindowSize)
#define kWindowSize   kWindowVerts * 3 + kTags
#define kWindowVerts  45
#define kTags         7

; A macro that refers to itself is expanded only once, as in C.
#define SELF          SELF + 1

#vuprog
    iaddiu iWin, iBase, kWindowB
    iaddiu iX, vi00, SELF
#endvuprog

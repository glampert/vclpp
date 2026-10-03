; C function-like macros, with # (stringize), ## (paste) and variadic arguments.
#define ADDR(base, off)  ((base) + (off))
#define STR(x)           #x
#define CAT(a, b)        a ## b
#define FIRST(a, ...)    a
#define REST(a, ...)     __VA_ARGS__
#define NOARGS()         nop
#define TWICE(x)         x, x

#vuprog
    lq fA, ADDR(1, 2)(vi00)
    lq fB, ADDR(ADDR(1, 2), (3, 4))(vi00)
    loi STR(a  "q"  b)
    iaddiu CAT(vi, 01), vi00, 1
    iaddiu vi01, vi00, FIRST(7, 8, 9)
    iaddiu REST(0, vi02, vi00, 3)
    NOARGS()
    iaddiu vi01, vi00, ADDR      ; not followed by '(': just a name
    max TWICE(vf01), vf02
#endvuprog

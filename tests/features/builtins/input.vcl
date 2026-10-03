; The built-in macros.
#vuprog
    iaddiu vi01, vi00, __LINE__
#define WHERE __LINE__
    iaddiu vi02, vi00, WHERE
    loi __FILE__
    iaddiu vi03, vi00, __COUNTER__
    iaddiu vi03, vi00, __COUNTER__
#if __VCLPP__ >= 2
    iaddiu vi04, vi00, __VCLPP__
#endif
#endvuprog

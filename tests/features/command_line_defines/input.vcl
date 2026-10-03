; -D NAME defines NAME as 1, -D NAME=VALUE as VALUE.
#vuprog
#if FLAG == 1
    iaddiu vi01, vi00, VALUE
#endif
    iaddiu vi02, vi00, 0 EMPTY
#endvuprog

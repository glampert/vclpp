; vclpp 1 syntax: everything here was accepted by vclpp 1, and preprocesses as it did.
#define kBase    8      ; a comment after a directive
#define kStride  2
#define FOO "bar"

; A macro with no parameters.
#macro LoadConstants
    lq fA, 0(vi00)   ; a comment in a macro body
    ; a comment line in a macro body

    lq fB, 1(vi00)
#endmacro

#macro MatrixMultiplyVertex: vertex_result, matrix, vertex
    mul  acc,           matrix[0], vertex[x]
    madd acc,           matrix[1], vertex[y]
    madd acc,           matrix[2], vertex[z]
    madd vertex_result, matrix[3], vertex[w]
#endmacro

#vuprog VU1Prog_Compat
    LoadConstants{ }
    iaddiu iPtr, vi00, kBase
    MatrixMultiplyVertex{ fVert, fMVP, fVert }
    MatrixMultiplyVertex{ fVert2, fMVP, fVert2 }
    iaddiu iPtr, iPtr, kStride
    loi FOO
#endvuprog

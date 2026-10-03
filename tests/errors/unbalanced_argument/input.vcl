#macro M: x
    lq x, 0(vi00)
#endmacro
#vuprog
    M{ vf01) }
#endvuprog

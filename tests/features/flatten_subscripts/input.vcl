; --flatten-subscripts works outside MASP mode too.
#vuprog VU1Prog_Flatten
    mulax  acc, xform[0], vert[x]
    maddw  out, xform[3], vf00[w]
#endvuprog

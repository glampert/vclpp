; vclpp 1 copied characters it had no use for straight through: the '@' some VCL
; labels end in, a '`', UTF-8 text outside a comment. vclpp 2 does the same.

; Never invoked, as with most of a macro library, but still lexed.
#macro Unused: dst
    ibeq   vi01, vi00, UNU1@
    move   dst, vf00
UNU1@:
#endmacro

#macro Slerp: dst, src
    ibeq   vi01, vi00, QSL1@
    mul    dst, src, i
QSL1@:
    b      src@end            ; 'src' is still a parameter here
#endmacro

#define kWhere @here

#vuprog VU1Prog_StrayCharacters
    Slerp{ fOut, fIn }
    loi    kWhere
    move   `name`, vf00
    move   fé, vf00
#endvuprog

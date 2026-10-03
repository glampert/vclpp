// Comments of every kind are dropped: VCL's ';', and C's // and /* */.
#define kLong 1 + \
              2          /* a #define continued on the next line */
#macro Body: reg
    lq reg, 0(vi00)
    sq reg, 1(vi00)
#endmacro
#vuprog
    iaddiu vi01, /* inline */ vi00, kLong
lStart: Body{ vf01 } nop   ; text around an invocation keeps lines of its own
    Body{ vf02 }
    Body{ vf03 }
    Body{
        vf04 }            ; an argument list can run across lines
	iaddiu vi02, vi00, 2      ; a tab stays a tab
#endvuprog

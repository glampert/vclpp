; In MASP mode, #include takes only a C header's macros, as cpp -imacros does: the
; header is shared with C code, whose declarations are not code for VCL.
	#include	"header.h"
#define kLocal	(kBase + 2)

	.macro	load	reg, offset
	lq	\reg, \offset(vi00)
	.endm

	load	vf01, kBase
	load	vf02, kLocal
	iaddiu	vi01, vi00, kSize/2
	.end

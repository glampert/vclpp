; .include looks next to the including file, then in the -I directories. An .end in an
; included file ends the whole program.
	.include	"common.i"
	.include	"deeper.i"
	common_macro	1
	after_include
	.include	"ends.i"
	never_reached
	.end

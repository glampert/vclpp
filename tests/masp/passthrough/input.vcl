/* A C comment
   over two lines. */
	.name	vsmTest		; VCL's directives pass through,
	.init_vf_all		// as do labels and code.
	.align	4
	.global	vsmTest_CodeStart
	--enter
	--endenter
start:
	nop			! -c makes '!' start comments too
	.equ	kX, 1
	--exit
	--endexit
	.end

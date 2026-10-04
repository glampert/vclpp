; .arepeat and .awhile replay their bodies; .exitm leaves a macro early.
	.macro	rep	n=2
	.arepeat	\n-1
	nop
	.aendr
	.endm

	.macro	limit	n
	.aif \n GT 1
	big	\n
	.exitm
	.aendi
	small	\n
	.endm

	rep
	rep	4
	.arepeat	0
	never
	.aendr

count	.assigna	0
	.awhile	\&count LT 3
	pass	\&count
count	.assigna	\&count+1
	.aendw

	limit	2
	limit	0
	.end

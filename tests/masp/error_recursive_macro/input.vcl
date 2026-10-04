; A macro that invokes itself with no .aif to stop it.
	.macro	forever	n
	forever	\n
	.endm
	forever	1
	.end

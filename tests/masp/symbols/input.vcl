; .equ symbols are evaluated, and replaced as whole, case-sensitive words wherever they
; appear, quotes and directive operands included. \&name reads a variable.
kA	.equ	2+1
kB	.equ	kA*2
_buf	.assignc	"_double"
n	.assigna	kB+1

	use	kA, KA, kAx, kA_1, x.kA, (kA), "kA"
	use	kB, \&_buf, \&n, \&preset
	.arepeat	kA
	rep
	.aendr
	.aif "\&_buf" eq "_double"
	double
	.aendi
 kC	.equ	5
	use	kC
	.end

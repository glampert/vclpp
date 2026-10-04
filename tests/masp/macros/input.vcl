; Macros: parameters with defaults and keyword arguments, \@, a parameter used as the
; macro to invoke, and expanded lines read again for more macros.
	.macro	inner	a
lab\@:	op	\a
	.endm

	.macro	outer	x, y=dflt
	inner	\x
	inner	\y
	.endm

	.macro	twice	operation, operand
	\operation	\operand
	\operation	\operand
	.endm

	; Parameter names are case-sensitive, and \name takes the longest name.
	.macro	words	Abc, xform
	p1	\Abc, \abc, \xform[0], \xform_suffix
	mul\xform.xyz	acc, vf00, vf00
	.endm

	.macro	long_list	first, second,
+			third
	params	\first, \second, \third
	.endm

	outer	p, q
	OUTER	r
	outer	t, y=s
	twice	inner, u
	words	one, two
	long_list	1, 2,
+		3
	inner	"x, y"
	inner.xyz	not_an_invocation
lbl:	inner	v
	.end

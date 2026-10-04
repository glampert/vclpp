; .aif: quoted operands compare as text, the others as integers.
	.macro	describe	value, other
	.aif "\value" EQ ""
	empty
	.aelse
	.aif "\value" NE "\other"
	differs	\value
	.aelse
	same	\value
	.aendi
	.aendi
	.endm

	describe
	describe	a, b
	describe	a, a

	.aif 2+3*4 eq 14
	c_precedence
	.aendi
	.aif 1 LT 2
	lt
	.aendi
	.aif 2 le 2
	le
	.aendi
	.aif 3 GT 2
	gt
	.aendi
	.aif 2 GE 3
	not_ge
	.aelse
	ge_else
	.aendi
	.aif "abc" EQ "ABC"
	text_ignores_case
	.aelse
	text_is_case_sensitive
	.aendi
	.aif 0 EQ 1
	.aif 1 EQ 1
	skipped_inner
	.aendi
	.aelse
	outer_else
	.aendi
	.end

; -f turns subscripts of one digit, or one of w, x, y and z, into suffixes, as the sed
; that ps2stuff ran after masp did. Anything else stays as written.
	mulax	acc, xform[0], vert[x]
	maddw	out, xform[3], vf00[W]
	ilw.x	vi01, offset[1](vi00)
	keep	list[10], spaced[ 0 ], other[q], vf01[xy]
	.end

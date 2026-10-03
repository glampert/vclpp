#define F(x) x
#macro B
    nop
#endmacro
#vuprog
    F(B{ })
#endvuprog

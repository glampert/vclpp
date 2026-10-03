; -x folds the constant expressions that #defines leave behind.
#define kA 1000
#vuprog
    lq fA, kA+1(vi00)
    lq fB, 2*3(vi00)
#endvuprog

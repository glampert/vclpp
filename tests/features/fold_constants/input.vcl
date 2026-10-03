; -x folds the integer constant expressions that stand on their own, and
; leaves alone those an operator next to them would take part in.
#define kBlock 1010
#vuprog
    lq fA, kBlock + 0(vi00)
    lq fB, kBlock + 11(vi00)
    iaddi vi01, vi01, -3
    iaddiu vi01, vi00, a - 1 - 2
    iaddiu vi01, vi00, 1 + 2 * a
    iaddiu vi01, vi00, (1 + 2) * 3
    iaddiu vi01, vi00, 0x10 + 1
    iaddiu vi01, vi00, -1 + 2
    iaddiu vi01, vi00, 1 << 4 | 1
    loi 1.5 + 1
    lq fC, 2 * (3 + 4)(vi02)
    lq.xyz fE, 4+4(vi01)
    --barrier
#endvuprog

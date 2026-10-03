; Shared macros, using constants each program defines for itself.
#define kSpill 1008

#macro CopyTags
    iaddiu iTagPtr, iBase, kGifTags
    isw.x  iTagPtr, kSpill(vi00)
#endmacro

#macro Kick: lblEmpty
    ibeq   iNloop, vi00, lblEmpty
    xgkick iKickAt
    lblEmpty:
#endmacro

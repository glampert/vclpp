; Block macros can invoke other block macros, and use #define constants,
; in their bodies and in their arguments. ## pastes tokens together, which
; gives each invocation labels of its own.
#define kGifTags 2

#macro CopyTag: reg, offset
    lqi reg, (iTagPtr++)
    sqi reg, offset(iOutPtr)
#endmacro

#macro CopyTags: done
    iaddiu iTagPtr, iBase, kGifTags
    CopyTag{ fTag0, 0 }
    CopyTag{ fTag1, kGifTags - 1 }
done:
#endmacro

#macro Loop: name
l##name##Start:
    CopyTags{ l##name##Copied }
    ibne iCount, vi00, l##name##Start
#endmacro

#vuprog
    Loop{ First }
    Loop{ Second }
#endvuprog

; Macros from an included file can use constants the including file defines
; after the #include: macro bodies are expanded where they are invoked.
#include "common.i"
#define kGifTags 4
#vuprog VU1Prog_Includes
    CopyTags{ }
    Kick{ lEmpty }
#endvuprog

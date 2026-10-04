// Shared with the C code.
#include "context.h"
#define kBase (kContextStart + 4) // after the context
#define kSize ((1024 - kBase) / 3)
typedef struct { int x; } shared_t;
int shared_function(void);

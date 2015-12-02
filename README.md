
# VCLPP

VCLPP, a minimal preprocessor for the PS2 [Vector Command Line (VCL)](https://github.com/jsvennevid/openvcl)
tool, written in C++11.

VCL uses GNU GASP by default for source file preprocessing before running its instruction scheduler.
Unfortunately, GASP has become deprecated and is increasingly harder to find a good build
for it, specially on OSX, plus, its preprocessor syntax is peculiar and not familiar to C programmers.

Due to these issues, I wrote this tiny and very ad hoc preprocessor for Vector Unit assembly code
that is compatible with the input expected by the VCL. I use it in my [PS2 port of Quake II](https://github.com/glampert/quake2-for-ps2).

The supported syntax is:

### Define constants

    #define ANSWER 42
    #define FOO "bar"

**NOTE:** Only one level of substitution is performed, so the "value"
of a `#define` must not reference other defines!

### Function-like macros

    #macro MatrixMultiplyVertex: vertex_result, matrix, vertex
        mul  acc,           matrix[0], vertex[x]
        madd acc,           matrix[1], vertex[y]
        madd acc,           matrix[2], vertex[z]
        madd vertex_result, matrix[3], vertex[w]
    #endmacro

And in the invocation, the expected syntax is:

    MatrixMultiplyVertex{ Vert, fTransform, Vert }

### Include files

You can include other files containing defines and macros anywhere inside a source
file. There's also no limit for the number of `#include`s in a source file.

    #include "my_definitions.i"

**NOTE:** Recursive includes are not supported, so files that you `#include` in a source
file *cannot* themselves include other files!

## VCLPP Usage

<pre>
Usage:
 $ vclpp input-file [output-file] [options]
 Applies custom preprocessing to a source file prior to running VCL.
 This preprocessor supports C-style #define constants and custom #macro directives.
 If no output filename is provided the input name is used but the extension is replaced with '.vsm'
 Options are:
  -h, --help     Prints this message and exits.
  -j, --vcljunk  Adds the standard VCL prologue/epilogue junk to the output.
</pre>

Providing the `-j` or `--vcljunk` flag will cause the tool to add the frequently used
VCL prologue/epilogue boilerplate for `enter/exit` sections, so you don't have to repeat that
in every source file. Output example:

<pre>
.init_vf_all
.init_vi_all
.syntax new
.vu

--enter
--endenter

    ; Your VU program here

--exit
--endexit
</pre>

## License

This project's source code is released under the [MIT License](http://opensource.org/licenses/MIT).


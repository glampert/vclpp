
# VCLPP

VCLPP, a C-like preprocessor for the PS2 [Vector Command Line (VCL)](https://github.com/jsvennevid/openvcl)
tool, written in C++17.

VCL uses GNU GASP by default for source file preprocessing before running its instruction scheduler.
Unfortunately, GASP has become deprecated and is increasingly harder to find a good build
for it, specially on OSX, plus, its preprocessor syntax is peculiar and not familiar to C programmers.

Due to these issues, I wrote this preprocessor for Vector Unit assembly code that is compatible
with the input expected by the VCL. It supports the directives of the C preprocessor, and adds block
macros for multi-line assembly. I use it in my [PS2 port of Quake II](https://github.com/glampert/quake2-for-ps2).

## Building

VCLPP reads its input with the lexer from [parse-utils](https://github.com/glampert/parse-utils),
which comes in as a git submodule: clone with `--recursive`, or run `git submodule update --init`
in an existing clone. Then:

    make        # builds the vclpp binary
    make test   # builds it and runs the tests in tests/

It needs a C++17 compiler. CI builds and tests it with GCC 9, the oldest compiler it supports,
and with the current GCC and Apple clang. `CXX` and `BIN_TARGET` can be set on the `make`
command line.

## Syntax

Comments start with a `;`, as in VCL; C's `//` and `/* */` comments work too. Comments and
blank lines are left out of the output, and each line of code keeps its indentation.

### Define constants

    #define ANSWER    42
    #define FOO       "bar"
    #define kWindowB  (kWindowA + kWindowSize)

A constant is expanded where it is used, and what it expands to is expanded again, so constants
can be made of other constants. As in C, a `#define` applies to the code after it.

### Function-like macros

    #define ADDR(base, offset)  ((base) + (offset))
    #define STR(x)              #x
    #define LABEL(name)         l ## name ## Loop

As in C: `#` turns an argument into a string, `##` pastes two tokens together, and `...` takes
any number of arguments, as `__VA_ARGS__`. A function-like macro name without an argument list
after it is left alone.

### Block macros

    #macro MatrixMultiplyVertex: vertex_result, matrix, vertex
        mul  acc,           matrix[0], vertex[x]
        madd acc,           matrix[1], vertex[y]
        madd acc,           matrix[2], vertex[z]
        madd vertex_result, matrix[3], vertex[w]
    #endmacro

And in the invocation, the expected syntax is:

    MatrixMultiplyVertex{ Vert, fTransform, Vert }

The invocation is replaced by the lines of the macro's body. Arguments are separated by commas,
and brackets of any kind group them, so an argument can be an expression like `kBase + 3`; an
argument list can also continue on the next lines. A block macro can invoke other block macros
and use any other macro, in its body and in its arguments, and `##` works in its body:

    #macro Loop: name
    l##name##Start:
        CopyTags{ l##name##Copied }
        ibne iCount, vi00, l##name##Start
    #endmacro

A block macro that invokes itself, directly or through others, is an error, and so is
`Name{ ... }` with no `#macro Name` before it.

### Include files

    #include "my_definitions.i"
    #include <shared_definitions.i>

`"file"` is looked for next to the file that includes it, then in the `-I` directories, in
order, and then in the working directory; `<file>` only in the `-I` directories. Included files
can include other files. `#pragma once` and include guards work as in C.

### Conditionals and other directives

`#if`, `#ifdef`, `#ifndef`, `#elif`, `#else` and `#endif` work as in C, with `defined` and the
C integer operators. An identifier that is not a macro counts as 0 (`-Wundef` warns about it),
and `true` and `false` count as 1 and 0. Also supported: `#undef`, `#error`, `#warning` and
`#pragma once`.

### VU programs

    #vuprog VU1Prog_TexturedTriangles
        ; Your VU program here
    #endvuprog

The program name becomes VCL's `.name` directive. VCLPP warns if either directive is missing.

### Built-in macros

`__FILE__`, `__LINE__`, `__COUNTER__`, and `__VCLPP__`, which is the major version: 2.

## VCLPP Usage

<pre>
Usage:
 $ vclpp [options] &lt;input-file&gt; [output-file]
 Preprocesses a Vector Unit assembly source file for VCL. Besides C's #include,
 #define, #undef, #if/#ifdef/#ifndef/#elif/#else/#endif, #error, #warning and
 #pragma once, it supports #macro blocks and #vuprog/#endvuprog programs.
 With no output file, the output is the input file with its extension replaced by '.vsm'.
 Options are:
  -h, --help             Prints this message and exits.
  -j, --vcl-boilerplate  Adds the standard VCL prologue/epilogue to the output.
  -x, --fixcexpr         Replaces constant integer expressions, like 1+2, by their values.
  -I &lt;dir&gt;               Adds a directory to look for #include files in.
  -D &lt;name&gt;[=&lt;value&gt;]    Defines a macro, as '#define name value' (the value defaults to 1).
  -Wundef                Warns when an #if evaluates an identifier that is not a macro.
  -Werror                Treats warnings as errors.
</pre>

Providing the `-j` or `--vcl-boilerplate` flag will cause the tool to add the frequently used
VCL prologue/epilogue boilerplate for `enter/exit` sections, so you don't have to repeat that
in every source file. Output example:

<pre>
.init_vi VI02, VI03, VI04, VI05, VI06, VI07, VI08, VI09, VI10, VI11, VI12, VI13, VI14
.init_vf_all
.syntax new
.vu

--enter
--endenter

    ; Your VU program here

--exit
--endexit
</pre>

VI01 is left out of `.init_vi` because the clipping instructions use it directly.

With `-x`, an integer constant expression is replaced by its value where it stands on its own,
so `1010 + 0(vi00)` becomes `1010(vi00)`; one that an operator next to it would take part in,
like the `1 - 2` in `a - 1 - 2`, is left alone.

Errors and warnings are reported GCC-style, as `file:line:column: error: message`, after the
chain of `#include`s that led to the file and before the chain of macro invocations that led
to the code, if any. An error stops the run without
writing the output file; so do warnings, with `-Werror`, once all of them have been reported.

## Upgrading from VCLPP 1

VCLPP 2 preprocesses VCLPP 1 sources as VCLPP 1 did, with these differences:

- A `#define` applies to the code after it; VCLPP 1 applied it to the whole file. VCLPP 2
  warns about a constant that is defined after code that uses it.
- Underscores are part of names: VCLPP 1 replaced `FOO` inside `MY_FOO`.
- Text before a block macro invocation on its line is kept; VCLPP 1 dropped it.
- `-x` folds whole expressions correctly; VCLPP 1 only handled `a+b` written without spaces.
- The long form of `-j` is now `--vcl-boilerplate`.
- Two macro expansions in a row are separated by one blank line, not two.
- Output lines end in a plain LF, with no trailing whitespace; VCLPP 1 kept the CRs of a source
  with CRLF line endings. Macros can be defined in such a source too, where VCLPP 1 reported
  their `#endmacro` as a directive inside the macro.

## License

This project's source code is released under the [MIT License](http://opensource.org/licenses/MIT).

VCLPP is built with [parse-utils](https://github.com/glampert/parse-utils), which is released under
the GNU GPL version 3. Since a vclpp binary includes parse-utils, distributing one is subject to the
terms of the GPLv3.

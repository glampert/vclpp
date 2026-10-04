
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
  -h, --help               Prints this message and exits.
  --version                Prints vclpp's version and exits.
  -j, --vcl-boilerplate    Adds the standard VCL prologue/epilogue to the output.
  -x, --fixcexpr           Replaces constant integer expressions, like 1+2, by their values.
  -f, --flatten-subscripts Turns name[0] into name_0 and name[x] into namex.
  -I &lt;dir&gt;                 Adds a directory to look for #include files in.
  -D &lt;name&gt;[=&lt;value&gt;]      Defines a macro, as '#define name value' (the value defaults to 1).
  -Wundef                  Warns when an #if evaluates an identifier that is not a macro.
  -Werror                  Treats warnings as errors.
  -m, --masp               MASP mode: the input is written for masp, the GASP-compatible
                           preprocessor (.macro, .aif, .include, ...). Running vclpp under
                           the name masp or gasp turns it on too.

 In MASP mode, #include takes only a header's macros, the output goes to stdout unless
 an output file is given, and these masp options apply:
  -o &lt;file&gt;                The output file.
  -c &lt;char&gt;                Makes &lt;char&gt; start comments as well as ';'.
  -D &lt;name&gt;[=&lt;value&gt;]      Sets the MASP variable &lt;name&gt;, read as \&amp;name.
  -P &lt;char&gt;                The directive prefix: only '.' is supported.
  -v                       Prints vclpp's version and exits.
  -p, -s, -u, -l, -d       Accepted and ignored.
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

With `-f` or `--flatten-subscripts`, a subscript of one digit, or one of `w`, `x`, `y` and `z`,
becomes a suffix: `matrix[0]` turns into `matrix_0`, and `vf01[x]` into `vf01x`. That is what
ps2stuff's build did with sed after masp, for the VCL of its day, and openvcl still needs it
for code written that way, like ps2gl's.

Errors and warnings are reported GCC-style, as `file:line:column: error: message`, after the
chain of `#include`s that led to the file and before the chain of macro invocations that led
to the code, if any. An error stops the run without
writing the output file; so do warnings, with `-Werror`, once all of them have been reported.

## MASP Mode

With `-m` or `--masp`, VCLPP preprocesses sources written for masp, the GASP-compatible
assembler preprocessor that classic PS2 VU code was written for, such as the microcode of
[ps2gl](https://github.com/ps2dev/ps2gl). Running VCLPP under the name `masp` or `gasp`, e.g.
through a symlink, turns MASP mode on as well; so does a prefixed name like `ee-gasp`. That lets
VCLPP stand in for either in an existing build.

MASP mode does what masp does, then runs the C preprocessor over the result, as a build would
run cpp over masp's output. On ps2gl's VU programs, its output is masp's, apart from whitespace
and comments.

### Running it

    $ vclpp -m [options] <input-file> [output-file]

As with masp, the output goes to stdout unless a file is given, as the second file name or with
`-o`. masp's own options are taken too:

    -o <file>             The output file.
    -c <char>             Makes <char> start comments, as well as ';'.
    -I <dir>              Adds a directory to look for .include and #include files in.
    -D <name>[=<value>]   Sets the MASP variable <name>, read as \&name. Outside MASP
                          mode, -D defines a C macro instead.
    -P <char>             The directive prefix: only '.' is supported.
    -v                    Prints the version.
    -p, -s, -u, -l, -d    Accepted and ignored: they only change masp's listing output.

`-x`, `-f`, `-Wundef` and `-Werror` work as in standard mode.

A build made with ps2stuff's `Makefile.work` runs masp or GASP as `$(APP)`, so it can run
VCLPP instead with

    make APP="vclpp -m"

or through a symlink named `masp` or `gasp`. The code the build assembles then comes out the
same, byte for byte, as with masp: that has been checked on all of ps2gl's VU programs.

That build also runs `sed` before and after masp, and cpp after VCL. VCLPP can do their work as
well, so that openvcl can take its output directly:

    vclpp -m -x -f -I vu1 vu1/general.vcl general_pp.vcl
    openvcl -o general.vsm general_pp.vcl

`#include` takes the C headers' macros, `-x` turns the constants they define into numbers, and
`-f` writes subscripts the way openvcl wants them. With the actual addresses to hand, openvcl
can tell more memory accesses apart, so its code can come out shorter than through cpp.

### Lines, comments and labels

    ; A comment. So are // and /* */ ones.
    loop:   iaddi   vi01, vi01, -1          ; A label, in column 1.
            iaddiu  vi02, vi00,
    +               kOffset                 ; A '+' in column 1 continues the line before.

A name in column 1 is a label. masp lets it go without its colon, and VCLPP then adds one, as
masp does. Code and directives are indented, so that they are not taken for labels. Comments
and blank lines are left out of the output.

### Macros

    .macro  dot3    output, vec1, vec2, op=mul
            \op.xyz    \output, \vec1, \vec2
    .endm

            dot3    vf01, vf02, vf03            ; mul.xyz vf01, vf02, vf03
            dot3    vf01, vf02, vf03, op=add    ; add.xyz vf01, vf02, vf03

A macro is invoked by its name, followed by a space or the end of the line, and then its
arguments, separated by commas; quotes keep a comma inside an argument. Macro names ignore case,
but parameter names do not. A parameter can have a default value, and an argument can name the
parameter it is for, as `op=add` does; after one argument given by name, the rest have to be
too. A missing argument takes its parameter's default, or is empty. `dot3 .macro output, ...`,
with the name in column 1, defines a macro too.

In the body, `\name` is replaced by the argument wherever it appears, even inside a word. The
name is the longest one after the backslash, and one that is not a parameter's is left as
written. `\@` is the number of macro expansions before this one, counting from 0, which gives
each expansion labels of its own:

    .macro  wait_for    flag
    wait\@: ibeq        \flag, vi00, wait\@
    .endm

The lines of an expansion are read again, so they can invoke other macros, including one that a
parameter names, and use any directive. `.exitm` ends an expansion early. A macro can invoke
itself, as long as an `.aif` stops it; expansions nest up to 200 deep. A macro defined again
replaces the one before.

### Conditionals

    .aif    "\ones" EQ ""
            dot3    intensity, light, normal
    .aelse
            dot4    intensity, light, normal, \ones
    .aendi

`.aif` compares two operands with `EQ`, `NE`, `LT`, `LE`, `GT` or `GE`, in any case. Two quoted
strings compare as text, which is case-sensitive; anything else is an integer expression, with
C's operators and precedence. An `.aif` with no comparison holds if its expression is not 0.
Conditionals nest, and one opened in a file or a macro has to be closed there.

### Loops

            .arepeat    \count-1
            iadd        vi01, vi01, vi02
            .aendr

    i       .assigna    0
            .awhile     \&i LT 4
            lq          vf1\&i, \&i(vi00)
    i       .assigna    \&i+1
            .aendw

`.arepeat` repeats its lines a number of times, and `.awhile` while its condition holds, which
is evaluated again before each pass. Either stops after 100000 passes.

### Variables

    _out_buffer  .assignc  "_double"
    count        .assigna  2 * 3

`.assignc` gives a variable some text, without the quotes around it, and `.assigna` the value of
an integer expression. `\&name` is replaced by the variable's value anywhere, in macros or out.
The name goes in column 1, and is case-sensitive.

### Constants

    kInputQPerV  .equ  3

`.equ` gives a name the value of an integer expression, worked out where it is defined. From
then on, the name is replaced by its value wherever it appears as a whole word: in directives
too, and even inside quotes, as masp does. Names are case-sensitive. An indented `.equ` is not
masp's, and passes through to VCL.

### Include files and the end

    .include  "math.i"

`.include` looks for the file next to the file that includes it, then in the `-I` directories,
then in the working directory; masp only looks in the last two. `.end` ends the program, even in
an included file. VCLPP warns if the program has no `.end`, as masp does.

### Other directives

Directives that are not masp's pass through, as code does: VCL's `.name`, `.init_vf_all` and
`--enter`, for example, or `.align`. GASP's data and listing directives, which masp turns into
other assembler directives, are errors: `.data`, `.datab`, `.sdata`, `.sdatab`, `.sdataz`,
`.sdatac`, `.res`, `.sres`, `.sresc`, `.sresz`, `.print`, `.heading`, `.page`, `.form`, `.org`,
`.radix`, `.alternate`, `.program`, `.export` and `.assign`.

### C directives

Lines that start with `#` are C directives. They are carried out on masp's output, in order, as
cpp would. `#include` takes only a header's macros, as `cpp -imacros` does: VU code shares its
headers with C code, whose declarations are not code for VCL. `#define`, `#undef` and `#if` work
as in standard mode, and C macros expand in the code. VCLPP's own `#macro`, `Name{ }` and
`#vuprog` are not available in MASP mode.

### Differences from masp

- A label on a line that invokes a macro stays, on a line of its own. masp drops it.
- A macro's name has to be followed by a space or the end of the line. masp also takes
  `name.xyz` as invoking `name`, with `.xyz` as its first argument.
- `.align` passes through to VCL. masp turns `.align 4` into a bare `4`.
- A comment on a `.macro` line is allowed. masp reads it as more parameters.
- `;` always starts a comment, even when `-c` adds another character.
- The output keeps each line's indentation, and leaves out comments, `//` and `/* */` ones
  included, and blank lines. masp indents every line with a tab, and copies comments through.
- An error, like too many arguments for a macro, stops the run. masp reports some of them and
  carries on.

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

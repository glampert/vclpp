// ================================================================================================
// -*- C++ -*-
// File: output_writer.cpp
// Brief: Turns the preprocessed tokens back into text: one line of code per line, as it was
//        indented, with the optional VCL boilerplate around it.
//
// This source code is released under the MIT license.
// See the accompanying LICENSE file for details.
// ================================================================================================

#include "output_writer.hpp"
#include "expressions.hpp"

#include <string_view>
#include <utility>

namespace vclpp
{

namespace
{

//
// .init_vi_all would be the obvious choice, but it causes problems
// when using clipping instructions. The VU1 clipw and FC flag helpers
// require using register VI01 directly and OpenVCL doesn't seem to
// respect our uses of VI01 when init_vi_all is set, reusing it for
// other variables, which ends up causing the clip instructions to
// override other data. Explicitly specifying which registers OpenVCL
// can use fixes the problem by leaving VI01 out of the list. You'll get
// one less register to work with if you're not doing clipping, but the
// vast majority of programs will, and you still have 13 regs to play
// with, so this should be okay.
//
constexpr std::string_view k_vcl_prologue =
    "\n"
    ".init_vi VI02, VI03, VI04, VI05, VI06, VI07, VI08, VI09, VI10, VI11, VI12, VI13, VI14\n"
    ".init_vf_all\n"
    ".syntax new\n"
    ".vu\n"
    "\n"
    "--enter\n"
    "--endenter\n"
    "\n";

constexpr std::string_view k_vcl_epilogue =
    "\n"
    "--exit\n"
    "--endexit\n"
    "\n";

bool ends_with_blank_line(const std::string & text)
{
    return (text.size() >= 2 && text.compare(text.size() - 2, 2, "\n\n") == 0) || text == "\n";
}

// -f: each subscript of one digit or one of w, x, y and z, written with no spaces inside
// the brackets, becomes a suffix. name[0] turns into name_0 and name[x] into namex, as
// the sed did that ps2stuff's build ran on masp's output: s/\[\([0-9]\)\]/_\1/g and
// s/\[\([w-zW-Z]\)\]/\1/g.
void flatten_subscripts(std::vector<pp_token> & line)
{
    std::vector<pp_token> flat;
    flat.reserve(line.size());

    for (std::size_t i = 0; i < line.size(); ++i)
    {
        if (i + 2 < line.size() && line[i].is(lexer::punctuation_id::open_bracket) &&
            line[i + 1].leading.empty() && line[i + 2].leading.empty() &&
            line[i + 2].is(lexer::punctuation_id::close_bracket) && line[i + 1].text.size() == 1)
        {
            const pp_token & inside = line[i + 1];
            const char c = inside.text.front();

            std::string suffix;
            if (inside.kind == token_kind::number && c >= '0' && c <= '9')
            {
                suffix = std::string{ "_" } + c;
            }
            else if (inside.is_identifier() && std::string_view{ "wxyzWXYZ" }.find(c) != std::string_view::npos)
            {
                suffix = std::string{ c };
            }

            if (!suffix.empty())
            {
                // The suffix keeps the '['s place, and the ']'s blank line after.
                pp_token token    = std::move(line[i]);
                token.kind        = token_kind::identifier;
                token.punct       = lexer::punctuation_id::none;
                token.text        = std::move(suffix);
                token.blank_after = (token.blank_after || line[i + 2].blank_after);
                flat.push_back(std::move(token));
                i += 2;
                continue;
            }
        }
        flat.push_back(std::move(line[i]));
    }

    line = std::move(flat);
}

} // namespace

std::string write_output(std::vector<pp_token> tokens, const std::string & program_name,
                         const output_options & opts, expression_evaluator & evaluator)
{
    std::string text;

    if (!program_name.empty())
    {
        text += "\n.name " + program_name + "\n";
    }
    if (opts.vcl_boilerplate)
    {
        text += k_vcl_prologue;
    }

    std::vector<pp_token> line;
    bool blank_line_pending = false;

    for (std::size_t next = 0; next < tokens.size();)
    {
        line.clear();
        do
        {
            line.push_back(std::move(tokens[next++]));
        }
        while (next < tokens.size() && !tokens[next].starts_line);

        if (opts.fold_constants)
        {
            fold_constant_expressions(line, evaluator);
        }
        if (opts.flatten_subscripts)
        {
            flatten_subscripts(line);
        }

        if ((blank_line_pending || line.front().blank_before) && !text.empty() && !ends_with_blank_line(text))
        {
            text += '\n';
        }
        blank_line_pending = false;

        // The first token's leading whitespace is the line's indentation.
        for (const pp_token & token : line)
        {
            text += token.leading;
            text += token.text;
            blank_line_pending = (blank_line_pending || token.blank_after);
        }
        text += '\n';
    }

    if (opts.vcl_boilerplate)
    {
        text += k_vcl_epilogue;
    }
    return text;
}

} // namespace vclpp

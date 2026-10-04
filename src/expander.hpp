// ================================================================================================
// -*- C++ -*-
// File: expander.hpp
// Brief: Macro expansion: #define constants and function-like macros, and #macro blocks.
//
// This source code is released under the MIT license.
// See the accompanying LICENSE file for details.
// ================================================================================================

#pragma once

#include "macro_table.hpp"
#include "tokenizer.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <string_view>
#include <vector>

namespace vclpp
{

class diagnostics;
class source_manager;

// Expands macros the way the C preprocessor does: an expansion is rescanned for more
// macros, and a token that came out of a macro's expansion is never expanded as that
// macro again (the "hide set" algorithm GCC and Clang document), so a self-referential
// macro stops rather than recursing. A #macro block invoked as Name{ args } follows the
// same rules, but its body is lines: the invocation is replaced by them, and a block
// that leads back to itself is an error, since leaving it unexpanded is no use to VCL.
class expander final
{
public:
    // Supplies the next line of the current file when a macro's argument list runs past
    // the end of its line. Returns false at the end of the file.
    using line_source = std::function<bool(std::vector<pp_token> & line)>;

    expander(macro_table & macros, const source_manager & sources, diagnostics & diags);

    // Name{ is then plain text, not a block macro invocation, as in MASP mode.
    void disable_block_macros() noexcept { m_block_macros = false; }

    // Expands a line of code. Block macros in it expand to their lines, so the result
    // can span several lines.
    std::vector<pp_token> expand_line(std::vector<pp_token> line, const line_source & more_lines);

    // Expands tokens on their own, e.g. those of an #if expression. A block macro in them
    // is an error, said to be invoked in 'context'.
    std::vector<pp_token> expand_tokens(std::vector<pp_token> tokens, std::string_view context);

private:
    using token_list = std::deque<pp_token>;

    struct arguments final
    {
        std::vector<std::vector<pp_token>> values;
        pp_token                           close; // The ')' or '}' that ended them.
    };

    std::vector<pp_token> expand(token_list & input, const line_source * more_lines, std::string_view block_context);

    arguments collect_arguments(token_list & input, const line_source * more_lines, const macro & m, const pp_token & name);
    void check_arguments(const macro & m, const pp_token & name, arguments & args);

    std::vector<pp_token> substitute(const macro & m, const pp_token & name,
                                     const std::vector<std::vector<pp_token>> & args, const hide_set & hidden);

    pp_token expand_builtin(const macro & m, const pp_token & name);
    pp_token stringize(const std::vector<pp_token> & arg, const pp_token & hash) const;
    pp_token paste(const pp_token & left, const pp_token & right, const pp_token & op);

    macro_table &          m_macros;
    const source_manager & m_sources;
    diagnostics &          m_diags;
    std::uint32_t          m_counter = 0; // __COUNTER__
    bool                   m_block_macros = true;
};

} // namespace vclpp

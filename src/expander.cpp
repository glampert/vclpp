// ================================================================================================
// -*- C++ -*-
// File: expander.cpp
// Brief: Macro expansion: #define constants and function-like macros, and #macro blocks.
//
// This source code is released under the MIT license.
// See the accompanying LICENSE file for details.
// ================================================================================================

#include "expander.hpp"
#include "diagnostics.hpp"
#include "source_manager.hpp"

#include <algorithm>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace vclpp
{

namespace
{

using punct = lexer::punctuation_id;

bool hides(const hide_set & hidden, const std::uint32_t id)
{
    return std::binary_search(hidden.begin(), hidden.end(), id);
}

hide_set with_id(hide_set hidden, const std::uint32_t id)
{
    const auto it = std::lower_bound(hidden.begin(), hidden.end(), id);
    if (it == hidden.end() || *it != id)
    {
        hidden.insert(it, id);
    }
    return hidden;
}

hide_set intersection(const hide_set & a, const hide_set & b)
{
    hide_set result;
    std::set_intersection(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(result));
    return result;
}

hide_set merged(const hide_set & a, const hide_set & b)
{
    hide_set result;
    std::set_union(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(result));
    return result;
}

// Whitespace inside a single-line expansion is just "some" or "none", as in C.
void normalize_spacing(pp_token & token)
{
    token.starts_line  = false;
    token.blank_before = false;
    token.blank_after  = false;
    if (!token.leading.empty())
    {
        token.leading = " ";
    }
}

// Puts a macro's replacement back on the input to be rescanned, in the place of the
// invocation that ran from 'first' to 'last'.
void push_replacement(std::deque<pp_token> & input, std::vector<pp_token> replacement,
                      const pp_token & first, const pp_token & last)
{
    if (!replacement.empty())
    {
        pp_token & head = replacement.front();
        head.starts_line  = first.starts_line;
        head.leading      = first.leading;
        head.blank_before = first.blank_before;
        replacement.back().blank_after = last.blank_after;
    }
    else if (first.starts_line && !input.empty() && !input.front().starts_line)
    {
        // Nothing replaces it and its line goes on: what follows starts the line now.
        input.front().starts_line  = true;
        input.front().leading      = first.leading;
        input.front().blank_before = first.blank_before;
    }

    input.insert(input.begin(), std::make_move_iterator(replacement.begin()), std::make_move_iterator(replacement.end()));
}

bool opens_nesting(const pp_token & token, const bool all_brackets)
{
    return token.is(punct::open_parentheses) ||
           (all_brackets && (token.is(punct::open_bracket) || token.is(punct::open_curly_bracket)));
}

std::optional<punct> closer_of(const pp_token & token)
{
    if (token.is(punct::open_parentheses))   { return punct::close_parentheses;   }
    if (token.is(punct::open_bracket))       { return punct::close_bracket;       }
    if (token.is(punct::open_curly_bracket)) { return punct::close_curly_bracket; }
    return std::nullopt;
}

bool closes_nesting(const pp_token & token, const bool all_brackets)
{
    return token.is(punct::close_parentheses) ||
           (all_brackets && (token.is(punct::close_bracket) || token.is(punct::close_curly_bracket)));
}

std::string quoted(const std::string & text)
{
    std::string result = "\"";
    for (const char c : text)
    {
        if (c == '"' || c == '\\')
        {
            result += '\\';
        }
        result += c;
    }
    result += '"';
    return result;
}

std::string plural(const std::size_t count, const char * const word)
{
    return std::to_string(count) + " " + word + (count == 1 ? "" : "s");
}

} // namespace

expander::expander(macro_table & macros, const source_manager & sources, diagnostics & diags)
    : m_macros{ macros }
    , m_sources{ sources }
    , m_diags{ diags }
{
}

std::vector<pp_token> expander::expand_line(std::vector<pp_token> line, const line_source & more_lines)
{
    token_list input{ std::make_move_iterator(line.begin()), std::make_move_iterator(line.end()) };
    return expand(input, &more_lines, {});
}

std::vector<pp_token> expander::expand_tokens(std::vector<pp_token> tokens, const std::string_view context)
{
    token_list input{ std::make_move_iterator(tokens.begin()), std::make_move_iterator(tokens.end()) };
    return expand(input, nullptr, context);
}

std::vector<pp_token> expander::expand(token_list & input, const line_source * more_lines, const std::string_view block_context)
{
    std::vector<pp_token> output;
    std::string line_indent; // Indentation of the line being output.

    const auto emit = [&output, &line_indent](pp_token token)
    {
        if (token.starts_line)
        {
            line_indent = token.leading;
        }
        output.push_back(std::move(token));
    };

    while (!input.empty())
    {
        pp_token name = std::move(input.front());
        input.pop_front();

        // An argument list has to open on the same line as the name.
        const auto followed_by = [&input](const punct id)
        {
            return !input.empty() && !input.front().starts_line && input.front().is(id);
        };

        const macro * m = (name.is_identifier() ? m_macros.find(name.text) : nullptr);
        if (m == nullptr)
        {
            // Name{ } in code can only be a block macro invocation. Left as it is, it
            // would only puzzle VCL.
            if (name.is_identifier() && block_context.empty() && followed_by(punct::open_curly_bracket))
            {
                m_diags.error(name, "'" + name.text + "' is not a block macro: no '#macro " + name.text + "' has been seen");
            }
            emit(std::move(name));
            continue;
        }

        const bool hidden = hides(name.hidden, m->id);

        switch (m->kind)
        {
        case macro_kind::builtin :
            {
                emit(expand_builtin(*m, name));
                break;
            }
        case macro_kind::object_like :
            {
                if (hidden)
                {
                    emit(std::move(name));
                    break;
                }

                push_replacement(input, substitute(*m, name, {}, with_id(name.hidden, m->id)), name, name);
                break;
            }
        case macro_kind::function_like :
            {
                // Without an argument list the name is just a name, as in C.
                if (hidden || !followed_by(punct::open_parentheses))
                {
                    emit(std::move(name));
                    break;
                }

                arguments args = collect_arguments(input, more_lines, *m, name);
                check_arguments(*m, name, args);

                const hide_set expansion_hidden = with_id(intersection(name.hidden, args.close.hidden), m->id);
                push_replacement(input, substitute(*m, name, args.values, expansion_hidden), name, args.close);
                break;
            }
        case macro_kind::block :
            {
                // As in vclpp 1, the name without '{' after it is not an invocation.
                if (!followed_by(punct::open_curly_bracket))
                {
                    emit(std::move(name));
                    break;
                }
                if (hidden)
                {
                    m_diags.error(name, "recursive invocation of block macro '" + m->name + "'");
                }
                if (!block_context.empty())
                {
                    m_diags.error(name, "block macro '" + m->name + "' cannot be invoked in " + std::string{ block_context });
                }

                const std::string indent = (name.starts_line ? name.leading : line_indent);

                arguments args = collect_arguments(input, more_lines, *m, name);
                check_arguments(*m, name, args);

                std::vector<pp_token> body = substitute(*m, name, args.values, with_id(name.hidden, m->id));

                // The body's lines replace the invocation, and whatever followed it on
                // its line moves to a line of its own.
                if (!input.empty() && !input.front().starts_line)
                {
                    input.front().starts_line = true;
                    input.front().leading     = indent;
                }
                if (!body.empty())
                {
                    // An invocation written in the source, not one inside another
                    // macro, is set off by blank lines; one that starts or ends the
                    // body of another passes that one's blank lines on.
                    const bool in_source = (name.origin == nullptr);
                    body.front().starts_line  = true;
                    body.front().blank_before = (body.front().blank_before || name.blank_before || in_source);
                    body.back().blank_after   = (body.back().blank_after || args.close.blank_after || in_source);
                }

                input.insert(input.begin(), std::make_move_iterator(body.begin()), std::make_move_iterator(body.end()));
                break;
            }
        } // switch (m->kind)
    }

    return output;
}

expander::arguments expander::collect_arguments(token_list & input, const line_source * more_lines,
                                                const macro & m, const pp_token & name)
{
    // A block macro's arguments can hold any brackets, which group commas as well. In
    // a function-like macro's, only parentheses do, as in C.
    const bool  block = (m.kind == macro_kind::block);
    const punct close = (block ? punct::close_curly_bracket : punct::close_parentheses);

    // A variadic macro's trailing arguments, commas and all, make up __VA_ARGS__.
    const std::size_t max_values = (m.variadic ? m.params.size() + 1 : std::numeric_limits<std::size_t>::max());

    input.pop_front(); // The opening '(' or '{'.

    arguments args;
    args.values.emplace_back();
    std::vector<punct> nesting; // The closers of the brackets open inside the arguments.

    for (;;)
    {
        if (input.empty())
        {
            std::vector<pp_token> line;
            if (more_lines == nullptr || !(*more_lines)(line))
            {
                m_diags.error(name, "unterminated argument list invoking macro '" + m.name + "'");
            }
            input.insert(input.end(), std::make_move_iterator(line.begin()), std::make_move_iterator(line.end()));
            continue;
        }

        pp_token token = std::move(input.front());
        input.pop_front();

        if (nesting.empty() && token.is(close))
        {
            args.close = std::move(token);
            break;
        }

        if (opens_nesting(token, block))
        {
            nesting.push_back(*closer_of(token));
        }
        else if (closes_nesting(token, block))
        {
            if (nesting.empty() || !token.is(nesting.back()))
            {
                m_diags.error(token, "unbalanced '" + token.text + "' in the arguments of macro '" + m.name + "'");
            }
            nesting.pop_back();
        }
        else if (nesting.empty() && token.is(punct::comma) && args.values.size() < max_values)
        {
            args.values.emplace_back();
            continue;
        }

        args.values.back().push_back(std::move(token));
    }

    return args;
}

void expander::check_arguments(const macro & m, const pp_token & name, arguments & args)
{
    std::vector<std::vector<pp_token>> & values = args.values;
    const std::vector<note> definition{ note{ m.defined_at, "'" + m.name + "' is defined here" } };

    // 'NAME()' and 'NAME{ }' pass no arguments, rather than one that is empty - unless
    // the macro takes one, which a function-like macro can be given empty.
    const bool takes_one = (m.kind == macro_kind::function_like && m.params.size() + (m.variadic ? 1 : 0) == 1);
    if (values.size() == 1 && values.front().empty() && !takes_one)
    {
        values.clear();
    }

    if (m.variadic)
    {
        if (values.size() < m.params.size())
        {
            m_diags.error(name, "macro '" + m.name + "' takes at least " + plural(m.params.size(), "argument") +
                          ", but " + std::to_string(values.size()) + " were given", definition);
        }
        if (values.size() == m.params.size())
        {
            values.emplace_back(); // No trailing arguments: __VA_ARGS__ is empty.
        }
        return;
    }

    if (values.size() != m.params.size())
    {
        m_diags.error(name, "macro '" + m.name + "' takes " + plural(m.params.size(), "argument") +
                      ", but " + std::to_string(values.size()) + (values.size() == 1 ? " was given" : " were given"),
                      definition);
    }

    // A block macro's arguments are operands, and an empty operand is a mistake.
    if (m.kind == macro_kind::block)
    {
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            if (values[i].empty())
            {
                m_diags.error(name, "argument " + std::to_string(i + 1) + " ('" + m.params[i] +
                              "') of block macro '" + m.name + "' is empty", definition);
            }
        }
    }
}

std::vector<pp_token> expander::substitute(const macro & m, const pp_token & name,
                                           const std::vector<std::vector<pp_token>> & args, const hide_set & hidden)
{
    // Each argument is fully expanded on its own before it replaces its parameter, but
    // only once, and only if a parameter needs it: the operands of # and ## are not.
    std::vector<std::optional<std::vector<pp_token>>> expanded_args(args.size());
    const auto expanded_arg = [&](const std::size_t index) -> const std::vector<pp_token> &
    {
        if (!expanded_args[index].has_value())
        {
            token_list input{ args[index].begin(), args[index].end() };
            expanded_args[index] = expand(input, nullptr, "a macro argument");
        }
        return *expanded_args[index];
    };

    const auto origin = std::make_shared<const expansion_origin>(expansion_origin{ m.name, name.location, name.origin });

    std::vector<pp_token> out;
    const pp_token * paste_op = nullptr; // The '##' waiting for its right operand.
    bool last_piece_empty     = false;   // The last thing added was an empty argument.

    for (std::size_t i = 0; i < m.body.size(); ++i)
    {
        const pp_token & token = m.body[i];

        if (token.is(punct::preprocessor_merge))
        {
            paste_op = &token;
            continue;
        }

        std::vector<pp_token> piece;
        bool piece_is_argument = false;

        if (m.kind == macro_kind::function_like && token.is(punct::preprocessor))
        {
            // '#param': the argument's spelling as a string literal. The definition
            // was checked to have a parameter after every '#'.
            piece.push_back(stringize(args[*m.param_index(m.body[i + 1])], token));
            ++i;
        }
        else if (const std::optional<std::size_t> param = m.param_index(token); param.has_value())
        {
            const bool pasted = (paste_op != nullptr || (i + 1 < m.body.size() && m.body[i + 1].is(punct::preprocessor_merge)));
            piece = (pasted ? args[*param] : expanded_arg(*param));
            piece_is_argument = true;

            // The argument takes the parameter's place on its line.
            for (std::size_t k = 0; k < piece.size(); ++k)
            {
                normalize_spacing(piece[k]);
                if (k == 0)
                {
                    piece[k].starts_line = token.starts_line;
                    piece[k].leading     = token.leading;
                }
            }
        }
        else
        {
            piece.push_back(token);
        }

        if (paste_op != nullptr)
        {
            // An empty operand leaves the other one as it is.
            if (!last_piece_empty && !out.empty() && !piece.empty())
            {
                pp_token op = *paste_op;
                op.origin   = origin;
                out.back()  = paste(out.back(), piece.front(), op);
                piece.erase(piece.begin());
            }
            paste_op = nullptr;
        }

        last_piece_empty = (piece_is_argument && piece.empty());
        out.insert(out.end(), std::make_move_iterator(piece.begin()), std::make_move_iterator(piece.end()));
    }

    // Every token knows the expansion it came out of, and must not be expanded as this
    // macro again. Only a block macro's body is lines; any other replacement goes in
    // the invocation's place on its line.
    for (std::size_t i = 0; i < out.size(); ++i)
    {
        pp_token & token = out[i];
        token.hidden = merged(token.hidden, hidden);
        token.origin = origin;
        if (m.kind != macro_kind::block && i != 0)
        {
            normalize_spacing(token);
        }
    }

    return out;
}

pp_token expander::expand_builtin(const macro & m, const pp_token & name)
{
    // __FILE__ and __LINE__ say where the outermost macro was invoked, as in C.
    const source_location where = diagnostics::use_location(name);

    pp_token result = name;
    result.kind     = token_kind::number;
    result.punct    = punct::none;
    result.is_float = false;

    if (m.name == "__FILE__")
    {
        result.kind = token_kind::string;
        result.text = quoted(m_sources.file(where.file_index).display_name);
    }
    else if (m.name == "__LINE__")
    {
        result.text = std::to_string(where.line);
    }
    else if (m.name == "__COUNTER__")
    {
        result.text = std::to_string(m_counter++);
    }
    else // __VCLPP__: the major version.
    {
        result.text = "2";
    }
    return result;
}

pp_token expander::stringize(const std::vector<pp_token> & arg, const pp_token & hash) const
{
    std::string text;
    for (std::size_t i = 0; i < arg.size(); ++i)
    {
        const pp_token & token = arg[i];
        if (i != 0 && !token.leading.empty())
        {
            text += ' ';
        }
        if (token.kind == token_kind::string || token.kind == token_kind::char_literal)
        {
            const std::string quoted_text = quoted(token.text);
            text.append(quoted_text, 1, quoted_text.size() - 2); // Escaped, without the new quotes.
        }
        else
        {
            text += token.text;
        }
    }

    pp_token result = hash;
    result.kind  = token_kind::string;
    result.punct = punct::none;
    result.text  = "\"" + text + "\"";
    return result;
}

pp_token expander::paste(const pp_token & left, const pp_token & right, const pp_token & op)
{
    pp_token lexed;
    if (!lex_single_token(left.text + right.text, &lexed))
    {
        m_diags.error(op, "pasting '" + left.text + "' and '" + right.text + "' does not give a valid token");
    }

    pp_token result = left;
    result.kind     = lexed.kind;
    result.punct    = lexed.punct;
    result.is_float = lexed.is_float;
    result.text     = std::move(lexed.text);
    result.hidden   = merged(left.hidden, right.hidden);
    return result;
}

} // namespace vclpp

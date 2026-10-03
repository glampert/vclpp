// ================================================================================================
// -*- C++ -*-
// File: engine.cpp
// Brief: The preprocessor proper: reads the source a line at a time, carries out directives
//        and expands macros in the code.
//
// This source code is released under the MIT license.
// See the accompanying LICENSE file for details.
// ================================================================================================

#include "engine.hpp"
#include "diagnostics.hpp"
#include "source_manager.hpp"

#include <algorithm>
#include <filesystem>
#include <iterator>

namespace vclpp
{

namespace
{

using punct = lexer::punctuation_id;

// Deep enough for any sensible nesting, shallow enough to stop a file that includes
// itself (without #pragma once or a guard) long before the stack runs out.
constexpr std::size_t k_max_include_depth = 200;

// The text of a directive's tokens from 'first' on, spaced as written, with a space before it.
std::string rest_of_line(const std::vector<pp_token> & line, const std::size_t first)
{
    std::string text;
    for (std::size_t i = first; i < line.size(); ++i)
    {
        text += (i == first ? std::string{ " " } : line[i].leading);
        text += line[i].text;
    }
    return text;
}

pp_token number_token(const std::int64_t value, const pp_token & in_place_of)
{
    pp_token token = in_place_of;
    token.kind     = token_kind::number;
    token.punct    = punct::none;
    token.is_float = false;
    token.text     = std::to_string(value);
    return token;
}

} // namespace

engine::engine(const options & opts, source_manager & sources, diagnostics & diags)
    : m_options{ opts }
    , m_sources{ sources }
    , m_diags{ diags }
    , m_expander{ m_macros, sources, diags }
    , m_evaluator{ diags }
{
}

preprocessed engine::run()
{
    define_command_line_macros();

    const std::optional<std::uint32_t> main_file = m_sources.load_file(m_options.input_path, std::nullopt);
    if (!main_file.has_value())
    {
        m_diags.error("cannot read input file '" + m_options.input_path + "'");
    }
    enter_file(*main_file);

    std::vector<pp_token> line;
    while (!m_files.empty())
    {
        if (!take_line(m_files.back(), line))
        {
            leave_file();
        }
        else if (line.front().is(punct::preprocessor))
        {
            handle_directive(line);
        }
        else if (is_active())
        {
            process_code_line(line);
        }
    }

    const source_location main_location{ *main_file, 0, 0 };
    if (!m_vuprog_at.has_value())
    {
        m_diags.warning(main_location, "program start directive '#vuprog' was not found");
    }
    if (!m_endvuprog_at.has_value())
    {
        m_diags.warning(main_location, "program end directive '#endvuprog' was not found");
    }

    return preprocessed{ std::move(m_output), std::move(m_program_name) };
}

// ========================================================
// Lines and files:
// ========================================================

void engine::enter_file(const std::uint32_t file_index)
{
    m_files.push_back(file_frame{ file_index, tokenize_file(file_index, m_sources, m_diags), 0, m_conditionals.size() });
}

void engine::leave_file()
{
    // Conditionals do not carry across the end of a file.
    if (m_conditionals.size() > m_files.back().conditional_depth)
    {
        const conditional & open = m_conditionals.back();
        m_diags.error(open.opened_at, "unterminated #" + open.directive + ": missing #endif");
    }
    m_files.pop_back();
}

bool engine::take_line(file_frame & frame, std::vector<pp_token> & line)
{
    line.clear();
    if (frame.next >= frame.tokens.size())
    {
        return false;
    }

    do
    {
        line.push_back(std::move(frame.tokens[frame.next++]));
    }
    while (frame.next < frame.tokens.size() && !frame.tokens[frame.next].starts_line);

    return true;
}

bool engine::is_active() const
{
    return m_conditionals.empty() || m_conditionals.back().active;
}

void engine::process_code_line(std::vector<pp_token> & line)
{
    // An argument list can run on into the lines after it, which then become part of
    // this one - up to the end of the file, or a directive, which ends the line.
    const expander::line_source more_lines = [this](std::vector<pp_token> & next_line)
    {
        file_frame & frame = m_files.back();
        if (frame.next < frame.tokens.size() && frame.tokens[frame.next].is(punct::preprocessor))
        {
            return false;
        }
        return take_line(frame, next_line);
    };

    std::vector<pp_token> expanded = m_expander.expand_line(std::move(line), more_lines);

    // Remember the names left in the code: one #defined after this would not apply here.
    for (const pp_token & token : expanded)
    {
        if (token.is_identifier() && m_macros.find(token.text) == nullptr)
        {
            m_used_undefined.try_emplace(token.text, diagnostics::use_location(token));
        }
    }

    m_output.insert(m_output.end(), std::make_move_iterator(expanded.begin()), std::make_move_iterator(expanded.end()));
}

// ========================================================
// Directives:
// ========================================================

void engine::handle_directive(const std::vector<pp_token> & line)
{
    // A '#' alone on its line does nothing, as in C.
    if (line.size() == 1)
    {
        return;
    }

    const pp_token & directive = line[1];
    const std::string & name = directive.text;

    // Conditionals are followed even in code being skipped, to find where it ends.
    if      (name == "if")     { handle_if(line);           return; }
    else if (name == "ifdef")  { handle_ifdef(line, false); return; }
    else if (name == "ifndef") { handle_ifdef(line, true);  return; }
    else if (name == "elif")   { handle_elif(line);         return; }
    else if (name == "else")   { handle_else(line);         return; }
    else if (name == "endif")  { handle_endif(line);        return; }

    if (!is_active())
    {
        return;
    }

    if      (name == "include")   { handle_include(line);     }
    else if (name == "define")    { handle_define(line);      }
    else if (name == "undef")     { handle_undef(line);       }
    else if (name == "macro")     { handle_block_macro(line); }
    else if (name == "vuprog")    { handle_vuprog(line);      }
    else if (name == "endvuprog") { handle_endvuprog(line);   }
    else if (name == "pragma")    { handle_pragma(line);      }
    else if (name == "error")     { m_diags.error(directive, "#error" + rest_of_line(line, 2));     }
    else if (name == "warning")   { m_diags.warning(directive, "#warning" + rest_of_line(line, 2)); }
    else if (name == "endmacro")  { m_diags.error(directive, "#endmacro without #macro");           }
    else                          { m_diags.error(directive, "unknown preprocessor directive '#" + name + "'"); }
}

void engine::handle_include(const std::vector<pp_token> & line)
{
    if (line.size() < 3)
    {
        m_diags.error(line[1], "expected \"file\" or <file> after #include");
    }

    const pp_token & first = line[2];
    std::string name;
    bool angled = false;
    std::size_t next = 3;

    if (first.kind == token_kind::string)
    {
        name = first.text.substr(1, first.text.size() - 2);
    }
    else if (first.is(punct::logic_less))
    {
        angled = true;
        for (; next < line.size() && !line[next].is(punct::logic_greater); ++next)
        {
            name += (next != 3 ? line[next].leading : std::string{});
            name += line[next].text;
        }
        if (next == line.size())
        {
            m_diags.error(first, "missing '>' after the #include file name");
        }
        ++next;
    }
    else
    {
        m_diags.error(first, "expected \"file\" or <file> after #include");
    }

    if (name.empty())
    {
        m_diags.error(first, "empty file name in #include");
    }
    warn_extra_tokens(line, next);

    if (m_files.size() >= k_max_include_depth)
    {
        m_diags.error(first, "#include nested " + std::to_string(k_max_include_depth) +
                      " levels deep: does a file include itself?");
    }

    std::vector<std::filesystem::path> tried;
    const std::optional<std::filesystem::path> path = m_sources.find_include(name, angled, m_files.back().file_index, &tried);
    if (!path.has_value())
    {
        std::vector<note> notes;
        for (const std::filesystem::path & candidate : tried)
        {
            notes.push_back(note{ std::nullopt, "not found at '" + candidate.generic_string() + "'" });
        }
        if (tried.empty())
        {
            notes.push_back(note{ std::nullopt, "<file> is only looked for in the -I directories, and there are none" });
        }
        m_diags.error(first, "cannot find include file '" + name + "'", notes);
    }

    if (m_sources.is_include_once(*path))
    {
        return;
    }

    const source_location included_from{ line[0].location.file_index, line[0].location.line, 0 };
    const std::optional<std::uint32_t> file_index = m_sources.load_file(*path, included_from);
    if (!file_index.has_value())
    {
        m_diags.error(first, "cannot read include file '" + path->generic_string() + "'");
    }
    enter_file(*file_index);
}

void engine::handle_define(const std::vector<pp_token> & line)
{
    if (line.size() < 3 || !line[2].is_identifier())
    {
        m_diags.error(line[std::min<std::size_t>(2, line.size() - 1)], "expected a macro name after #define");
    }

    const pp_token & name = line[2];
    check_macro_name(name, false);

    macro m;
    m.name       = name.text;
    m.kind       = macro_kind::object_like;
    m.defined_at = name.location;

    std::size_t next = 3;

    // A '(' right after the name, with no space between, opens a parameter list.
    if (next < line.size() && line[next].is(punct::open_parentheses) && line[next].leading.empty())
    {
        m.kind = macro_kind::function_like;
        const pp_token & open = line[next++];

        for (bool expect_param = true;; ++next)
        {
            if (next >= line.size())
            {
                m_diags.error(open, "missing ')' in the parameter list of macro '" + m.name + "'");
            }

            const pp_token & token = line[next];
            if (token.is(punct::close_parentheses))
            {
                if (expect_param && !m.params.empty())
                {
                    m_diags.error(token, "expected a parameter name before ')' in macro '" + m.name + "'");
                }
                ++next;
                break;
            }

            if (!expect_param)
            {
                if (m.variadic)
                {
                    m_diags.error(token, "'...' must be the last parameter of macro '" + m.name + "'");
                }
                if (!token.is(punct::comma))
                {
                    m_diags.error(token, "expected ',' or ')' in the parameter list of macro '" + m.name + "'");
                }
                expect_param = true;
                continue;
            }

            if (token.is(punct::ellipsis))
            {
                m.variadic = true;
            }
            else if (!token.is_identifier() || token.text == "__VA_ARGS__")
            {
                m_diags.error(token, "expected a parameter name in macro '" + m.name + "'");
            }
            else if (std::find(m.params.begin(), m.params.end(), token.text) != m.params.end())
            {
                m_diags.error(token, "duplicate parameter '" + token.text + "' in macro '" + m.name + "'");
            }
            else
            {
                m.params.push_back(token.text);
            }
            expect_param = false;
        }
    }

    // The body is one line, however many lines it was written across.
    m.body.assign(line.begin() + static_cast<std::ptrdiff_t>(next), line.end());
    for (pp_token & token : m.body)
    {
        token.starts_line = false;
    }

    if (m.kind == macro_kind::function_like)
    {
        for (std::size_t i = 0; i < m.body.size(); ++i)
        {
            if (m.body[i].is(punct::preprocessor) && (i + 1 == m.body.size() || !m.param_index(m.body[i + 1]).has_value()))
            {
                m_diags.error(m.body[i], "'#' is not followed by a parameter of macro '" + m.name + "'");
            }
        }
    }
    check_paste_operators(m);

    define_macro(std::move(m), name);
}

void engine::handle_undef(const std::vector<pp_token> & line)
{
    if (line.size() < 3 || !line[2].is_identifier())
    {
        m_diags.error(line[std::min<std::size_t>(2, line.size() - 1)], "expected a macro name after #undef");
    }

    check_macro_name(line[2], true);
    warn_extra_tokens(line, 3);
    m_macros.undefine(line[2].text);
}

void engine::handle_block_macro(const std::vector<pp_token> & line)
{
    if (line.size() < 3 || !line[2].is_identifier())
    {
        m_diags.error(line[std::min<std::size_t>(2, line.size() - 1)], "expected a macro name after #macro");
    }

    const pp_token & name = line[2];
    check_macro_name(name, false);

    macro m;
    m.name       = name.text;
    m.kind       = macro_kind::block;
    m.defined_at = name.location;

    // The parameter list: '#macro Name: param1, param2'.
    if (line.size() > 3)
    {
        if (!line[3].is(punct::colon))
        {
            m_diags.error(line[3], "unexpected text after the name of macro '" + m.name +
                          "'; a parameter list starts with ':', as in '#macro " + m.name + ": param1, param2'");
        }

        bool expect_param = true;
        for (std::size_t i = 4; i < line.size(); ++i)
        {
            const pp_token & token = line[i];
            if (token.is(punct::comma))
            {
                if (expect_param)
                {
                    m_diags.error(token, (m.params.empty() ? "lost comma before the first parameter of macro '" + m.name + "'"
                                                           : "lost comma after parameter '" + m.params.back() + "' of macro '" + m.name + "'"));
                }
                expect_param = true;
                continue;
            }

            if (!expect_param)
            {
                m_diags.error(token, "missing comma after parameter '" + m.params.back() + "' of macro '" + m.name + "'");
            }
            if (!token.is_identifier())
            {
                m_diags.error(token, "expected a parameter name in macro '" + m.name + "'");
            }
            if (std::find(m.params.begin(), m.params.end(), token.text) != m.params.end())
            {
                m_diags.error(token, "duplicate parameter '" + token.text + "' in macro '" + m.name + "'");
            }
            m.params.push_back(token.text);
            expect_param = false;
        }

        if (expect_param && !m.params.empty())
        {
            m_diags.error(line.back(), "extraneous comma after the last parameter '" + m.params.back() +
                          "' of macro '" + m.name + "'");
        }
    }

    // The body: the lines up to #endmacro, as they are written.
    std::vector<pp_token> body_line;
    for (;;)
    {
        if (!take_line(m_files.back(), body_line))
        {
            m_diags.error(name, "unterminated #macro '" + m.name + "': missing #endmacro");
        }

        if (body_line.front().is(punct::preprocessor))
        {
            const std::string directive = (body_line.size() > 1 ? body_line[1].text : std::string{});
            if (directive == "endmacro")
            {
                if (body_line.size() > 2)
                {
                    m_diags.error(body_line[2], "unexpected text after #endmacro");
                }
                break;
            }
            m_diags.error(body_line.front(), "preprocessor directive '#" + directive + "' inside #macro '" + m.name + "'");
        }

        m.body.insert(m.body.end(), std::make_move_iterator(body_line.begin()), std::make_move_iterator(body_line.end()));
    }

    check_paste_operators(m);
    define_macro(std::move(m), name);
}

void engine::handle_vuprog(const std::vector<pp_token> & line)
{
    const pp_token & directive = line[1];
    if (m_vuprog_at.has_value())
    {
        m_diags.error(directive, "#vuprog appears twice", { note{ *m_vuprog_at, "the first one is here" } });
    }
    m_vuprog_at = directive.location;

    if (line.size() > 2)
    {
        if (!line[2].is_identifier())
        {
            m_diags.error(line[2], "expected a program name after #vuprog");
        }
        if (line.size() > 3)
        {
            m_diags.error(line[3], "unexpected text after the program name");
        }
        m_program_name = line[2].text;
    }
}

void engine::handle_endvuprog(const std::vector<pp_token> & line)
{
    const pp_token & directive = line[1];
    if (!m_vuprog_at.has_value())
    {
        m_diags.error(directive, "#endvuprog without #vuprog");
    }
    if (m_endvuprog_at.has_value())
    {
        m_diags.error(directive, "#endvuprog appears twice", { note{ *m_endvuprog_at, "the first one is here" } });
    }
    if (line.size() > 2)
    {
        m_diags.error(line[2], "unexpected text after #endvuprog");
    }
    m_endvuprog_at = directive.location;
}

void engine::handle_pragma(const std::vector<pp_token> & line)
{
    if (line.size() == 3 && line[2].text == "once")
    {
        m_sources.mark_include_once(m_files.back().file_index);
        return;
    }
    m_diags.warning(line[1], "ignoring unknown '#pragma" + rest_of_line(line, 2) + "'");
}

void engine::handle_if(const std::vector<pp_token> & line)
{
    // Inside code being skipped, no branch is taken and nothing is evaluated.
    const bool parent_active = is_active();
    const bool taken = (parent_active && evaluate_condition(line));
    m_conditionals.push_back(conditional{ "if", line[1].location, parent_active, taken, taken, false });
}

void engine::handle_ifdef(const std::vector<pp_token> & line, const bool negated)
{
    const pp_token & directive = line[1];
    const bool parent_active = is_active();
    bool taken = false;

    if (parent_active)
    {
        if (line.size() < 3 || !line[2].is_identifier())
        {
            m_diags.error(line[std::min<std::size_t>(2, line.size() - 1)], "expected a macro name after #" + directive.text);
        }
        warn_extra_tokens(line, 3);
        taken = ((m_macros.find(line[2].text) != nullptr) != negated);
    }

    m_conditionals.push_back(conditional{ directive.text, directive.location, parent_active, taken, taken, false });
}

void engine::handle_elif(const std::vector<pp_token> & line)
{
    conditional & open = current_conditional(line[1]);
    if (open.seen_else)
    {
        m_diags.error(line[1], "#elif after #else");
    }

    if (!open.parent_active || open.branch_taken)
    {
        open.active = false;
        return;
    }
    open.active       = evaluate_condition(line);
    open.branch_taken = open.active;
}

void engine::handle_else(const std::vector<pp_token> & line)
{
    conditional & open = current_conditional(line[1]);
    if (open.seen_else)
    {
        m_diags.error(line[1], "#else after #else");
    }
    if (open.parent_active)
    {
        warn_extra_tokens(line, 2);
    }

    open.seen_else    = true;
    open.active       = (open.parent_active && !open.branch_taken);
    open.branch_taken = true;
}

void engine::handle_endif(const std::vector<pp_token> & line)
{
    const conditional & open = current_conditional(line[1]);
    if (open.parent_active)
    {
        warn_extra_tokens(line, 2);
    }
    m_conditionals.pop_back();
}

// ========================================================
// Directive helpers:
// ========================================================

engine::conditional & engine::current_conditional(const pp_token & directive)
{
    // Only one opened in this file: conditionals do not carry across files.
    if (m_conditionals.size() <= m_files.back().conditional_depth)
    {
        m_diags.error(directive, "#" + directive.text + " without #if");
    }
    return m_conditionals.back();
}

bool engine::evaluate_condition(const std::vector<pp_token> & line)
{
    const pp_token & directive = line[1];

    // 'defined NAME' and 'defined(NAME)' first, before NAME could be expanded.
    std::vector<pp_token> tokens;
    for (std::size_t i = 2; i < line.size(); ++i)
    {
        const pp_token & token = line[i];
        if (!token.is_identifier() || token.text != "defined")
        {
            tokens.push_back(token);
            continue;
        }

        std::size_t name = i + 1;
        const bool parenthesized = (name < line.size() && line[name].is(punct::open_parentheses));
        if (parenthesized)
        {
            ++name;
        }
        if (name >= line.size() || !line[name].is_identifier())
        {
            m_diags.error(token, "'defined' must be followed by a macro name");
        }

        std::size_t last = name;
        if (parenthesized)
        {
            if (++last >= line.size() || !line[last].is(punct::close_parentheses))
            {
                m_diags.error(token, "missing ')' after 'defined(" + line[name].text + "'");
            }
        }

        tokens.push_back(number_token((m_macros.find(line[name].text) != nullptr ? 1 : 0), token));
        i = last;
    }

    if (tokens.empty())
    {
        m_diags.error(directive, "#" + directive.text + " with no expression");
    }

    std::vector<pp_token> expanded = m_expander.expand_tokens(std::move(tokens), "an #" + directive.text + " expression");

    // Names left after expansion are not macros. As in C, they count as 0; as in C++,
    // true and false count as 1 and 0.
    for (pp_token & token : expanded)
    {
        if (!token.is_identifier())
        {
            continue;
        }

        if (token.text == "true" || token.text == "false")
        {
            token = number_token((token.text == "true" ? 1 : 0), token);
            continue;
        }

        if (m_options.warn_undef)
        {
            m_diags.warning(token, "'" + token.text + "' is not defined, evaluates to 0", "-Wundef");
        }
        token = number_token(0, token);
    }

    return m_evaluator.evaluate(expanded, directive, "#" + directive.text) != 0;
}

void engine::check_macro_name(const pp_token & name, const bool undefining)
{
    if (name.text == "defined")
    {
        m_diags.error(name, "'defined' cannot be used as a macro name");
    }
    if (macro_table::is_builtin_name(name.text))
    {
        m_diags.error(name, std::string{ undefining ? "cannot undefine" : "cannot redefine" } +
                      " built-in macro '" + name.text + "'");
    }
}

void engine::check_paste_operators(const macro & m)
{
    // '##' needs a token on either side of it, on its own line.
    for (std::size_t i = 0; i < m.body.size(); ++i)
    {
        const pp_token & token = m.body[i];
        if (!token.is(punct::preprocessor_merge))
        {
            continue;
        }

        const bool at_start = (i == 0 || token.starts_line);
        const bool at_end   = (i + 1 == m.body.size() || m.body[i + 1].starts_line);
        if (at_start || at_end)
        {
            m_diags.error(token, "'##' cannot be at either end of " +
                          std::string{ m.kind == macro_kind::block ? "a line of macro '" : "macro '" } + m.name + "'");
        }
    }
}

void engine::define_macro(macro m, const pp_token & name)
{
    // vclpp 1 expanded a #define anywhere in the file, even above it.
    if (const auto use = m_used_undefined.find(m.name); use != m_used_undefined.end())
    {
        m_diags.warning(name, "'" + m.name + "' is defined after code that uses it; those uses are not expanded "
                        "(vclpp 1 expanded them)", {}, { note{ use->second, "first used here" } });
        m_used_undefined.erase(use);
    }

    const std::optional<macro> previous = m_macros.define(std::move(m));
    if (previous.has_value() && !same_definition(*previous, *m_macros.find(name.text)))
    {
        m_diags.warning(name, "'" + name.text + "' redefined", {}, { note{ previous->defined_at, "the previous definition is here" } });
    }
}

void engine::define_command_line_macros()
{
    for (const auto & [name, value] : m_options.defines)
    {
        const std::uint32_t file_index = m_sources.add_text("<command line>", value);

        macro m;
        m.name       = name;
        m.kind       = macro_kind::object_like;
        m.defined_at = source_location{ file_index, 0, 0 };
        m.body       = tokenize_file(file_index, m_sources, m_diags);
        for (pp_token & token : m.body)
        {
            token.starts_line = false;
        }
        m_macros.define(std::move(m));
    }
}

void engine::warn_extra_tokens(const std::vector<pp_token> & line, const std::size_t first_extra)
{
    if (first_extra < line.size())
    {
        m_diags.warning(line[first_extra], "extra tokens at the end of the #" + line[1].text + " directive");
    }
}

} // namespace vclpp

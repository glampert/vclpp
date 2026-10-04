// ================================================================================================
// -*- C++ -*-
// File: masp.cpp
// Brief: The first stage of MASP mode: the macro language of masp and GASP, the assembler
//        preprocessors that classic VU code, like ps2gl's, was written for.
//
// This source code is released under the MIT license.
// See the accompanying LICENSE file for details.
// ================================================================================================

#include "masp.hpp"
#include "diagnostics.hpp"
#include "expressions.hpp"
#include "source_manager.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <iterator>

namespace vclpp
{

namespace
{

using punct = lexer::punctuation_id;

// Deep enough for any sensible nesting, shallow enough to stop a macro that invokes
// itself, or a file that includes itself, long before the stack runs out.
constexpr std::size_t k_max_include_depth   = 200;
constexpr std::size_t k_max_expansion_depth = 200;

// More passes than a loop in VU code needs; an .awhile whose condition never turns
// false stops here.
constexpr std::uint64_t k_max_loop_passes = 100000;

// The GASP directives that masp carries out itself - it turns .sdata into .byte, for
// one - and vclpp does not. Passed on as they are, they would mean something else to VCL.
constexpr std::array<std::string_view, 20> k_refused_directives = {
    "alternate", "assign", "data", "datab", "export", "form", "heading", "org", "page", "print",
    "program", "radix", "res", "sdata", "sdatab", "sdatac", "sdataz", "sres", "sresc", "sresz"
};

bool is_name_start(const char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool is_name_char(const char c)
{
    return is_name_start(c) || (c >= '0' && c <= '9');
}

// The length of the name that starts at 'pos', or zero if none does.
std::size_t name_length(const std::string_view text, const std::size_t pos)
{
    if (pos >= text.size() || !is_name_start(text[pos]))
    {
        return 0;
    }
    std::size_t end = pos + 1;
    while (end < text.size() && is_name_char(text[end]))
    {
        ++end;
    }
    return end - pos;
}

std::string lower_case(const std::string_view text)
{
    std::string lower{ text };
    for (char & c : lower)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return lower;
}

std::string_view trim(const std::string_view text)
{
    const std::size_t first = text.find_first_not_of(" \t");
    if (first == std::string_view::npos)
    {
        return {};
    }
    return text.substr(first, text.find_last_not_of(" \t") - first + 1);
}

// Splits text at its commas, except those inside quotes, and trims each part. Blank text
// has no parts; "a," has two, the second empty.
std::vector<std::string> split_at_commas(const std::string_view text)
{
    std::vector<std::string> parts;
    if (trim(text).empty())
    {
        return parts;
    }

    bool in_string = false;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= text.size(); ++i)
    {
        if (i == text.size() || (text[i] == ',' && !in_string))
        {
            parts.emplace_back(trim(text.substr(start, i - start)));
            start = i + 1;
        }
        else if (text[i] == '"')
        {
            in_string = !in_string;
        }
    }
    return parts;
}

// The comparisons of .aif and .awhile.
enum class comparison : std::uint8_t
{
    eq, ne, lt, le, gt, ge
};

std::optional<comparison> comparison_named(const std::string_view word)
{
    const std::string lower = lower_case(word);
    if (lower == "eq") { return comparison::eq; }
    if (lower == "ne") { return comparison::ne; }
    if (lower == "lt") { return comparison::lt; }
    if (lower == "le") { return comparison::le; }
    if (lower == "gt") { return comparison::gt; }
    if (lower == "ge") { return comparison::ge; }
    return std::nullopt;
}

template<typename T>
bool compare(const T & left, const comparison op, const T & right)
{
    switch (op)
    {
    case comparison::eq : return left == right;
    case comparison::ne : return left != right;
    case comparison::lt : return left <  right;
    case comparison::le : return left <= right;
    case comparison::gt : return left >  right;
    case comparison::ge : return left >= right;
    } // switch (op)
    return false;
}

} // namespace

masp_reader::masp_reader(const std::uint32_t main_file, const masp_options & opts, source_manager & sources,
                         diagnostics & diags, expression_evaluator & evaluator)
    : m_options{ opts }
    , m_sources{ sources }
    , m_diags{ diags }
    , m_evaluator{ evaluator }
    , m_main_file{ main_file }
{
    for (const auto & [name, value] : opts.variables)
    {
        m_variables[name] = value;
    }

    frame file;
    file.kind  = frame_kind::file;
    file.lines = split_lines(main_file);
    m_frames.push_back(std::move(file));
}

bool masp_reader::next_line(std::vector<pp_token> & line)
{
    if (!m_put_back.empty())
    {
        line = std::move(m_put_back.back());
        m_put_back.pop_back();
        return true;
    }

    text_line raw;
    while (read_line(raw))
    {
        if (process_line(raw, line))
        {
            return true;
        }
    }

    m_frames.clear();
    return false;
}

void masp_reader::unget_line(std::vector<pp_token> line)
{
    m_put_back.push_back(std::move(line));
}

// ========================================================
// Reading:
// ========================================================

std::vector<masp_reader::text_line> masp_reader::split_lines(const std::uint32_t file_index) const
{
    const std::string & text = m_sources.file(file_index).text;
    std::vector<text_line> lines;
    bool in_block_comment = false;
    std::uint32_t number = 0;

    // A UTF-8 byte order mark is not part of the text.
    std::size_t start = (text.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0);
    while (start < text.size())
    {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos)
        {
            end = text.size();
        }

        std::string_view physical = std::string_view{ text }.substr(start, end - start);
        if (!physical.empty() && physical.back() == '\r')
        {
            physical.remove_suffix(1);
        }
        ++number;

        // A '+' in column 1 continues the line before.
        const bool continues = (!in_block_comment && !physical.empty() && physical.front() == '+' && !lines.empty());
        std::string code = strip_comments(physical, in_block_comment);
        if (continues)
        {
            lines.back().text += code.substr(1);
        }
        else
        {
            lines.push_back(text_line{ std::move(code), source_location{ file_index, number, 1 } });
        }
        start = end + 1;
    }
    return lines;
}

std::string masp_reader::strip_comments(const std::string_view line, bool & in_block_comment) const
{
    std::string code;
    bool in_string = false;

    for (std::size_t i = 0; i < line.size(); ++i)
    {
        const char c    = line[i];
        const char next = (i + 1 < line.size() ? line[i + 1] : '\0');

        if (in_block_comment)
        {
            if (c == '*' && next == '/')
            {
                in_block_comment = false;
                ++i;
            }
            continue;
        }
        if (in_string)
        {
            code += c;
            in_string = (c != '"');
            continue;
        }

        if (c == '"')
        {
            in_string = true;
        }
        else if (m_options.comment_chars.find(c) != std::string::npos || (c == '/' && next == '/'))
        {
            break;
        }
        else if (c == '/' && next == '*')
        {
            // As in C, a comment separates what is on either side of it.
            in_block_comment = true;
            code += ' ';
            ++i;
            continue;
        }
        code += c;
    }

    // The whitespace before a comment goes with it.
    code.erase(code.find_last_not_of(" \t") + 1);
    return code;
}

bool masp_reader::read_line(text_line & line)
{
    while (!m_ended && !m_frames.empty())
    {
        frame & top = m_frames.back();
        if (top.next < top.lines.size())
        {
            const text_line & raw = top.lines[top.next++];
            line.location = raw.location;
            line.text     = (top.kind == frame_kind::macro ? substitute_arguments(raw.text, top) : raw.text);
            return true;
        }
        end_frame();
    }
    return false;
}

void masp_reader::end_frame()
{
    const frame & top = m_frames.back();
    if (m_conditionals.size() > top.conditional_depth)
    {
        error(text_line{ {}, m_conditionals[top.conditional_depth].opened_at }, "unterminated .aif: missing .aendi");
    }

    // A loop goes round again while it has passes left.
    if (top.kind == frame_kind::loop)
    {
        if (top.repeats_left > 0)
        {
            --m_frames.back().repeats_left;
            m_frames.back().next = 0;
            return;
        }
        if (top.while_condition.has_value())
        {
            const text_line condition = *top.while_condition;
            if (++m_frames.back().passes > k_max_loop_passes)
            {
                error(condition, ".awhile did not end after " + std::to_string(k_max_loop_passes) + " passes");
            }
            if (evaluate_condition(substitute_symbols(condition.text, condition), condition, ".awhile"))
            {
                m_frames.back().next = 0;
                return;
            }
        }
    }

    pop_frame();
}

void masp_reader::pop_frame()
{
    const frame & top = m_frames.back();
    const bool top_level_expansion = (top.kind == frame_kind::macro && top.origin->parent == nullptr);
    const bool main_file = (top.kind == frame_kind::file && m_frames.size() == 1);
    m_frames.pop_back();

    // An expansion written in the source is set off by blank lines.
    if (top_level_expansion)
    {
        m_blank_pending = true;
    }
    if (main_file && !m_ended)
    {
        m_diags.warning(source_location{ m_main_file, 0, 0 }, "the program does not end with .end");
    }
}

std::vector<masp_reader::text_line> masp_reader::capture_body(const std::string_view open, const std::string_view close,
                                                              const text_line & opener)
{
    // The body is read from where its opening directive is, and has to end there.
    frame & source = m_frames.back();
    std::vector<text_line> body;
    std::size_t depth = 1;

    for (;;)
    {
        if (source.next >= source.lines.size())
        {
            error(opener, "unterminated ." + std::string{ open } + ": missing ." + std::string{ close });
        }

        text_line line = source.lines[source.next++];
        if (source.kind == frame_kind::macro)
        {
            line.text = substitute_arguments(line.text, source);
        }

        const std::string directive = parse_statement(line.text).directive;
        if (directive == open)
        {
            ++depth;
        }
        else if (directive == close && --depth == 0)
        {
            return body;
        }
        body.push_back(std::move(line));
    }
}

// ========================================================
// Lines:
// ========================================================

masp_reader::statement masp_reader::parse_statement(const std::string & text)
{
    statement st;
    std::size_t pos = 0;

    // A name in column 1 is the name an .equ, .assigna, .assignc or .macro after it gives a
    // value to, or else a label, which masp lets go without its colon.
    if (const std::size_t length = name_length(text, 0); length != 0)
    {
        const bool colon = (length < text.size() && text[length] == ':');
        if (!colon)
        {
            const std::size_t dot = text.find_first_not_of(" \t", length);
            const std::size_t directive_length = (dot != std::string::npos && dot != length && text[dot] == '.'
                                                  ? name_length(text, dot + 1) : 0);
            const std::string directive = lower_case(std::string_view{ text }.substr(dot + 1, directive_length));

            if (directive_length != 0 && (directive == "equ" || directive == "assigna" || directive == "assignc"))
            {
                st.symbol    = text.substr(0, length);
                st.directive = directive;
                st.operand   = text.substr(dot + 1 + directive_length);
                return st;
            }
            if (directive_length != 0 && directive == "macro")
            {
                // "name .macro parameters" names the macro as ".macro name parameters" does.
                st.directive = directive;
                st.operand   = " " + text.substr(0, length) + text.substr(dot + 1 + directive_length);
                return st;
            }
        }

        st.label     = text.substr(0, length) + ":";
        st.label_end = length + (colon ? 1 : 0);
        pos = st.label_end;
    }

    const std::size_t start = text.find_first_not_of(" \t", pos);
    if (start == std::string::npos)
    {
        return st;
    }

    if (text[start] == '.')
    {
        if (const std::size_t length = name_length(text, start + 1); length != 0)
        {
            st.directive = lower_case(std::string_view{ text }.substr(start + 1, length));
            st.operand   = text.substr(start + 1 + length);
        }
    }
    else if (const std::size_t length = name_length(text, start); length != 0)
    {
        st.word    = text.substr(start, length);
        st.operand = text.substr(start + length);
    }
    return st;
}

std::string masp_reader::with_label(const statement & st, const std::string & text)
{
    // The label gets its colon, if it was written without, and some space after it.
    if (st.label.empty())
    {
        return text;
    }
    const std::string rest = text.substr(st.label_end);
    const bool spaced = (rest.empty() || rest.front() == ' ' || rest.front() == '\t');
    return st.label + (spaced ? rest : "\t" + rest);
}

bool masp_reader::process_line(const text_line & raw, std::vector<pp_token> & out)
{
    const std::size_t first = raw.text.find_first_not_of(" \t");
    if (first == std::string::npos)
    {
        return false;
    }

    const statement st = parse_statement(raw.text);

    // In code being skipped, only the nesting of .aif matters.
    if (!is_active())
    {
        if (st.directive == "aif")
        {
            m_conditionals.push_back(conditional{ raw.location, false, true, false, false });
        }
        else if (st.directive == "aelse")
        {
            else_conditional(raw);
        }
        else if (st.directive == "aendi")
        {
            close_conditional(raw);
        }
        return false;
    }

    // C directives are the engine's.
    if (raw.text[first] == '#')
    {
        return make_line(raw.text, raw, out);
    }

    if (!st.symbol.empty())
    {
        assign_symbol(st, raw);
        return false;
    }
    if (!st.directive.empty() && carry_out_directive(st, raw))
    {
        // A label on the line stays, on a line of its own; masp drops it.
        return !st.label.empty() && make_line(st.label, raw, out);
    }

    // Code, or a macro invocation. Variables and .equ symbols are replaced first, so that
    // they can name the macro too. A macro's name is followed by a space or nothing:
    // "maxx.x" is an instruction even with a macro called maxx.
    const std::string text = substitute_symbols(raw.text, raw);
    const statement code = parse_statement(text);

    if (!code.word.empty() && (code.operand.empty() || code.operand.front() == ' ' || code.operand.front() == '\t'))
    {
        if (const auto m = m_macros.find(lower_case(code.word)); m != m_macros.end())
        {
            // The expansion is set off by blank lines, a label on its line included. The
            // label stays, on a line of its own; masp drops it.
            const bool top_level = (current_origin() == nullptr);
            m_blank_pending = (m_blank_pending || top_level);

            const bool labelled = (!code.label.empty() && make_line(code.label, raw, out));
            invoke_macro(m->second, code.operand, raw);
            if (labelled)
            {
                m_blank_pending = false;
            }
            return labelled;
        }
    }

    return make_line(with_label(code, text), raw, out);
}

bool masp_reader::carry_out_directive(const statement & st, const text_line & raw)
{
    const std::string & name = st.directive;

    if      (name == "macro")   { define_macro(st, raw);     }
    else if (name == "exitm")   { exit_macro(raw);           }
    else if (name == "aif")     { open_conditional(st, raw); }
    else if (name == "aelse")   { else_conditional(raw);     }
    else if (name == "aendi")   { close_conditional(raw);    }
    else if (name == "arepeat") { repeat(st, raw);           }
    else if (name == "awhile")  { loop_while(st, raw);       }
    else if (name == "include") { include_file(st, raw);     }
    else if (name == "end")     { m_ended = true;            }
    else if (name == "endm")    { error(raw, ".endm without .macro");     }
    else if (name == "aendr")   { error(raw, ".aendr without .arepeat");  }
    else if (name == "aendw")   { error(raw, ".aendw without .awhile");   }
    else if (name == "assigna" || name == "assignc")
    {
        error(raw, "." + name + " takes the name it assigns in column 1, as in 'name ." + name + " value'");
    }
    else if (std::find(k_refused_directives.begin(), k_refused_directives.end(), name) != k_refused_directives.end())
    {
        error(raw, "MASP directive '." + name + "' is not supported by vclpp");
    }
    else
    {
        return false; // Not masp's, e.g. VCL's .name: it passes on as code.
    }
    return true;
}

void masp_reader::assign_symbol(const statement & st, const text_line & raw)
{
    const std::string operand = substitute_symbols(st.operand, raw);

    if (st.directive == "assignc")
    {
        // The text, without the quotes around it.
        std::string_view value = trim(operand);
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
        {
            value = value.substr(1, value.size() - 2);
        }
        m_variables[st.symbol] = std::string{ value };
        return;
    }

    const std::string value = std::to_string(evaluate(operand, raw, "." + st.directive));
    if (st.directive == "equ")
    {
        m_equates[st.symbol] = value;
    }
    else
    {
        m_variables[st.symbol] = value;
    }
}

bool masp_reader::make_line(const std::string & text, const text_line & raw, std::vector<pp_token> & out)
{
    out = tokenize_text(text, raw.location, current_origin(), m_diags);
    if (out.empty())
    {
        return false;
    }

    // The blank line goes before code, not before a directive, which leaves no line.
    if (m_blank_pending && !out.front().is(punct::preprocessor))
    {
        out.front().blank_before = true;
        m_blank_pending = false;
    }
    return true;
}

// ========================================================
// Directives:
// ========================================================

void masp_reader::define_macro(const statement & st, const text_line & raw)
{
    const std::string_view operand = trim(st.operand);
    const std::size_t length = name_length(operand, 0);
    if (length == 0)
    {
        error(raw, "expected a macro name after .macro");
    }

    macro_def m;
    m.name = std::string{ operand.substr(0, length) };

    for (const std::string & param : split_at_commas(operand.substr(length)))
    {
        const std::size_t equals = param.find('=');
        const std::string name{ trim(std::string_view{ param }.substr(0, equals)) };

        if (name.empty() || name_length(name, 0) != name.size())
        {
            error(raw, param.empty() ? "missing parameter name in macro '" + m.name + "'"
                                     : "'" + param + "' is not a parameter name in macro '" + m.name +
                                       "': parameters are separated by commas");
        }
        const auto same_name = [&name](const auto & p) { return p.first == name; };
        if (std::find_if(m.params.begin(), m.params.end(), same_name) != m.params.end())
        {
            error(raw, "duplicate parameter '" + name + "' in macro '" + m.name + "'");
        }

        std::string default_value = (equals == std::string::npos ? std::string{}
                                                                 : std::string{ trim(std::string_view{ param }.substr(equals + 1)) });
        m.params.emplace_back(name, std::move(default_value));
    }

    m.body = capture_body("macro", "endm", raw);

    // As in masp, a macro defined again replaces the one before.
    m_macros[lower_case(m.name)] = std::move(m);
}

void masp_reader::invoke_macro(const macro_def & m, const std::string & arguments, const text_line & raw)
{
    const std::size_t depth = static_cast<std::size_t>(std::count_if(m_frames.begin(), m_frames.end(),
                                                       [](const frame & f) { return f.kind == frame_kind::macro; }));
    if (depth >= k_max_expansion_depth)
    {
        error(raw, "macro expansions nested " + std::to_string(k_max_expansion_depth) +
              " levels deep: does macro '" + m.name + "' invoke itself?");
    }

    frame expansion;
    expansion.kind      = frame_kind::macro;
    expansion.lines     = m.body;
    expansion.arguments = m.params; // Their defaults, until the arguments are in.

    std::size_t positional = 0;
    bool by_name = false;
    for (const std::string & argument : split_at_commas(arguments))
    {
        // "name=value" gives a parameter by name. As in masp, the arguments after one
        // have to as well.
        const std::size_t length = name_length(argument, 0);
        const std::size_t equals = (length != 0 ? argument.find_first_not_of(" \t", length) : std::string::npos);
        if (equals != std::string::npos && argument[equals] == '=')
        {
            const std::string name = argument.substr(0, length);
            const auto param = std::find_if(expansion.arguments.begin(), expansion.arguments.end(),
                                            [&name](const auto & p) { return p.first == name; });
            if (param == expansion.arguments.end())
            {
                error(raw, "macro '" + m.name + "' has no parameter named '" + name + "'");
            }
            param->second = std::string{ trim(std::string_view{ argument }.substr(equals + 1)) };
            by_name = true;
            continue;
        }

        if (by_name)
        {
            error(raw, "argument '" + argument + "' after one given by name, invoking macro '" + m.name +
                  "': after one, all have to be");
        }
        if (positional >= m.params.size())
        {
            error(raw, "too many arguments for macro '" + m.name + "': it takes " + std::to_string(m.params.size()));
        }
        expansion.arguments[positional++].second = argument;
    }

    expansion.counter           = std::to_string(m_expansions++);
    expansion.origin            = std::make_shared<const expansion_origin>(expansion_origin{ m.name, raw.location, current_origin() });
    expansion.conditional_depth = m_conditionals.size();
    m_frames.push_back(std::move(expansion));
}

void masp_reader::exit_macro(const text_line & raw)
{
    const auto expansion = std::find_if(m_frames.rbegin(), m_frames.rend(),
                                        [](const frame & f) { return f.kind == frame_kind::macro; });
    if (expansion == m_frames.rend())
    {
        error(raw, ".exitm outside a macro");
    }

    // The expansion ends, with any loops and .aifs inside it.
    const auto frames_to_pop = std::distance(m_frames.rbegin(), expansion) + 1;
    m_conditionals.erase(m_conditionals.begin() + static_cast<std::ptrdiff_t>(expansion->conditional_depth), m_conditionals.end());
    for (std::ptrdiff_t i = 0; i < frames_to_pop; ++i)
    {
        pop_frame();
    }
}

void masp_reader::open_conditional(const statement & st, const text_line & raw)
{
    const bool active = evaluate_condition(substitute_symbols(st.operand, raw), raw, ".aif");
    m_conditionals.push_back(conditional{ raw.location, true, active, active, false });
}

void masp_reader::else_conditional(const text_line & raw)
{
    if (m_conditionals.size() <= frame_conditional_depth())
    {
        error(raw, ".aelse without .aif");
    }

    conditional & c = m_conditionals.back();
    if (c.seen_else)
    {
        error(raw, ".aelse after .aelse");
    }
    c.seen_else    = true;
    c.active       = (c.parent_active && !c.branch_taken);
    c.branch_taken = (c.branch_taken || c.active);
}

void masp_reader::close_conditional(const text_line & raw)
{
    if (m_conditionals.size() <= frame_conditional_depth())
    {
        error(raw, ".aendi without .aif");
    }
    m_conditionals.pop_back();
}

void masp_reader::repeat(const statement & st, const text_line & raw)
{
    const std::int64_t count = evaluate(substitute_symbols(st.operand, raw), raw, ".arepeat");
    if (count > static_cast<std::int64_t>(k_max_loop_passes))
    {
        error(raw, ".arepeat count " + std::to_string(count) + " is more than the " +
              std::to_string(k_max_loop_passes) + " passes a loop can make");
    }

    std::vector<text_line> body = capture_body("arepeat", "aendr", raw);
    if (count <= 0 || body.empty())
    {
        return;
    }

    frame loop;
    loop.kind              = frame_kind::loop;
    loop.lines             = std::move(body);
    loop.repeats_left      = static_cast<std::uint64_t>(count) - 1;
    loop.origin            = current_origin();
    loop.conditional_depth = m_conditionals.size();
    m_frames.push_back(std::move(loop));
}

void masp_reader::loop_while(const statement & st, const text_line & raw)
{
    // The condition is kept as written, to be evaluated again before each pass.
    const text_line condition{ st.operand, raw.location };
    std::vector<text_line> body = capture_body("awhile", "aendw", raw);

    if (!evaluate_condition(substitute_symbols(condition.text, condition), condition, ".awhile"))
    {
        return;
    }

    frame loop;
    loop.kind              = frame_kind::loop;
    loop.lines             = std::move(body);
    loop.while_condition   = condition;
    loop.passes            = 1;
    loop.origin            = current_origin();
    loop.conditional_depth = m_conditionals.size();
    m_frames.push_back(std::move(loop));
}

void masp_reader::include_file(const statement & st, const text_line & raw)
{
    const std::string operand{ trim(substitute_symbols(st.operand, raw)) };
    if (operand.size() < 2 || operand.front() != '"' || operand.back() != '"')
    {
        error(raw, "expected \"file\" after .include");
    }

    const std::string name = operand.substr(1, operand.size() - 2);
    if (name.empty())
    {
        error(raw, "empty file name in .include");
    }

    const auto files = static_cast<std::size_t>(std::count_if(m_frames.begin(), m_frames.end(),
                                                [](const frame & f) { return f.kind == frame_kind::file; }));
    if (files >= k_max_include_depth)
    {
        error(raw, ".include nested " + std::to_string(k_max_include_depth) + " levels deep: does a file include itself?");
    }

    std::vector<std::filesystem::path> tried;
    const std::optional<std::filesystem::path> path = m_sources.find_include(name, false, raw.location.file_index, &tried);
    if (!path.has_value())
    {
        std::vector<note> notes;
        for (const std::filesystem::path & candidate : tried)
        {
            notes.push_back(note{ std::nullopt, "not found at '" + candidate.generic_string() + "'" });
        }
        m_diags.error(at(raw), "cannot find include file '" + name + "'", notes);
    }

    const std::optional<std::uint32_t> file_index = m_sources.load_file(*path, raw.location);
    if (!file_index.has_value())
    {
        error(raw, "cannot read include file '" + path->generic_string() + "'");
    }

    frame file;
    file.kind              = frame_kind::file;
    file.lines             = split_lines(*file_index);
    file.conditional_depth = m_conditionals.size();
    m_frames.push_back(std::move(file));
}

// ========================================================
// Substitution and evaluation:
// ========================================================

std::string masp_reader::substitute_arguments(const std::string & text, const frame & expansion)
{
    std::string out;
    out.reserve(text.size());

    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '\\' && i + 1 < text.size())
        {
            if (text[i + 1] == '@')
            {
                out += expansion.counter;
                ++i;
                continue;
            }

            // The longest name after the backslash, if it is a parameter's; masp leaves
            // any other name as it is.
            if (const std::size_t length = name_length(text, i + 1); length != 0)
            {
                const std::string_view name = std::string_view{ text }.substr(i + 1, length);
                const auto param = std::find_if(expansion.arguments.begin(), expansion.arguments.end(),
                                                [name](const auto & p) { return p.first == name; });
                if (param != expansion.arguments.end())
                {
                    out += param->second;
                    i += length;
                    continue;
                }
            }
        }
        out += text[i];
    }
    return out;
}

std::string masp_reader::substitute_symbols(const std::string & text, const text_line & raw) const
{
    // \&NAME first, since a variable's value can hold .equ symbols.
    std::string with_variables;
    with_variables.reserve(text.size());

    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '\\' && i + 1 < text.size() && text[i + 1] == '&')
        {
            if (const std::size_t length = name_length(text, i + 2); length != 0)
            {
                const std::string name = text.substr(i + 2, length);
                const auto variable = m_variables.find(name);
                if (variable == m_variables.end())
                {
                    error(raw, "MASP variable '" + name + "' is not defined: .assigna and .assignc define them");
                }
                with_variables += variable->second;
                i += 1 + length;
                continue;
            }
        }
        with_variables += text[i];
    }

    if (m_equates.empty())
    {
        return with_variables;
    }

    // Then each .equ symbol, as a whole word. As in masp, that includes words in quotes.
    std::string out;
    out.reserve(with_variables.size());

    for (std::size_t i = 0; i < with_variables.size();)
    {
        const char c = with_variables[i];

        // A number, with any letters in it (0x1F), is not a name.
        if (c >= '0' && c <= '9')
        {
            std::size_t end = i + 1;
            while (end < with_variables.size() && is_name_char(with_variables[end]))
            {
                ++end;
            }
            out.append(with_variables, i, end - i);
            i = end;
            continue;
        }

        if (const std::size_t length = name_length(with_variables, i); length != 0)
        {
            const std::string name = with_variables.substr(i, length);
            const auto equate = m_equates.find(name);

            // A name after a backslash is a parameter reference masp left alone.
            const bool after_backslash = (i > 0 && with_variables[i - 1] == '\\');
            out += (equate != m_equates.end() && !after_backslash ? equate->second : name);
            i += length;
            continue;
        }

        out += c;
        ++i;
    }
    return out;
}

std::int64_t masp_reader::evaluate(const std::string & text, const text_line & raw, const std::string_view context)
{
    const std::vector<pp_token> tokens = tokenize_text(text, raw.location, current_origin(), m_diags);
    return m_evaluator.evaluate(tokens.data(), tokens.size(), at(raw), context);
}

bool masp_reader::evaluate_condition(const std::string & text, const text_line & raw, const std::string_view context)
{
    const std::vector<pp_token> tokens = tokenize_text(text, raw.location, current_origin(), m_diags);

    // A comparison - EQ, NE, LT, LE, GT or GE - between two operands, outside parentheses.
    std::optional<comparison> op;
    std::size_t op_index = 0;
    int depth = 0;

    for (std::size_t i = 0; i < tokens.size() && !op.has_value(); ++i)
    {
        if (tokens[i].is(punct::open_parentheses))
        {
            ++depth;
        }
        else if (tokens[i].is(punct::close_parentheses))
        {
            --depth;
        }
        else if (depth == 0 && tokens[i].is_identifier())
        {
            op       = comparison_named(tokens[i].text);
            op_index = i;
        }
    }

    // Without one, the condition holds if its value is not zero.
    if (!op.has_value())
    {
        return m_evaluator.evaluate(tokens.data(), tokens.size(), at(raw), context) != 0;
    }

    const pp_token * const left        = tokens.data();
    const std::size_t      left_count  = op_index;
    const pp_token * const right       = tokens.data() + op_index + 1;
    const std::size_t      right_count = tokens.size() - op_index - 1;

    // Two quoted strings compare as text.
    const auto is_string = [](const pp_token * const t, const std::size_t count)
    {
        return count == 1 && t->kind == token_kind::string;
    };
    if (is_string(left, left_count) && is_string(right, right_count))
    {
        const auto unquoted = [](const pp_token & t) { return t.text.substr(1, t.text.size() - 2); };
        return compare(unquoted(*left), *op, unquoted(*right));
    }

    const auto has_string = [](const pp_token * const t, const std::size_t count)
    {
        return std::any_of(t, t + count, [](const pp_token & token) { return token.kind == token_kind::string; });
    };
    if (has_string(left, left_count) || has_string(right, right_count))
    {
        error(raw, std::string{ context } + ": a quoted string can only be compared with another one");
    }

    return compare(m_evaluator.evaluate(left, left_count, at(raw), context), *op,
                   m_evaluator.evaluate(right, right_count, at(raw), context));
}

// ========================================================
// Helpers:
// ========================================================

bool masp_reader::is_active() const
{
    return m_conditionals.empty() || m_conditionals.back().active;
}

std::size_t masp_reader::frame_conditional_depth() const
{
    return (m_frames.empty() ? 0 : m_frames.back().conditional_depth);
}

std::shared_ptr<const expansion_origin> masp_reader::current_origin() const
{
    return (m_frames.empty() ? nullptr : m_frames.back().origin);
}

pp_token masp_reader::at(const text_line & raw) const
{
    pp_token token;
    token.location = raw.location;
    token.origin   = current_origin();
    return token;
}

void masp_reader::error(const text_line & raw, const std::string & message) const
{
    m_diags.error(at(raw), message);
}

} // namespace vclpp

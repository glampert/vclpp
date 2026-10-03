// ================================================================================================
// -*- C++ -*-
// File: tokenizer.cpp
// Brief: Splitting source files into tokens with parse-utils' lexer.
//
// This source code is released under the MIT license.
// See the accompanying LICENSE file for details.
// ================================================================================================

#include "tokenizer.hpp"
#include "diagnostics.hpp"
#include "source_manager.hpp"

#include <string_view>
#include <utility>

namespace vclpp
{

namespace
{

// ';' comments as well as C and C++ ones, and every string kept exactly as written:
// tokens are printed back by their spelling, never by the lexer's reading of them.
constexpr std::uint32_t k_lexer_flags = lexer::flags::semicolon_comments |
                                        lexer::flags::no_string_concat   |
                                        lexer::flags::no_string_escape_chars;

token_kind kind_of(const lexer::token & tok)
{
    switch (tok.get_type())
    {
    case lexer::token::type::number      : return token_kind::number;
    case lexer::token::type::string      : return token_kind::string;
    case lexer::token::type::literal     : return token_kind::char_literal;
    case lexer::token::type::identifier  : return token_kind::identifier;
    case lexer::token::type::punctuation : return token_kind::punctuation;
    case lexer::token::type::none        : break;
    } // switch (tok.get_type())
    return token_kind::punctuation;
}

lexer::punctuation_id punctuation_of(const lexer::token & tok)
{
    return (tok.is_punctuation() ? static_cast<lexer::punctuation_id>(tok.get_flags()) : lexer::punctuation_id::none);
}

// The text between two tokens: whitespace, maybe with comments in it.
struct gap_layout final
{
    bool        line_break = false; // The next token starts a new line.
    std::string leading;            // The whitespace the next token keeps.
};

gap_layout read_gap(const std::string_view gap)
{
    // A newline only ends the line outside a comment: as in C, a C-style comment that
    // spans lines does not split the line it is in. Line comments stop at their newline.
    gap_layout layout;
    std::size_t last_line_start = 0;

    for (std::size_t i = 0; i < gap.size();)
    {
        if (gap.substr(i, 2) == "/*")
        {
            const std::size_t end = gap.find("*/", i + 2);
            i = (end == std::string_view::npos ? gap.size() : end + 2);
        }
        else if (gap.substr(i, 2) == "//" || gap[i] == ';')
        {
            const std::size_t end = gap.find('\n', i);
            i = (end == std::string_view::npos ? gap.size() : end);
        }
        else
        {
            if (gap[i] == '\n')
            {
                layout.line_break = true;
                last_line_start   = i + 1;
            }
            ++i;
        }
    }

    const std::string_view last_line = gap.substr(last_line_start);
    if (layout.line_break)
    {
        // The token's indentation. A comment in front of it turns into spaces, so the
        // token stays in its column.
        layout.leading.assign(last_line);
        for (char & c : layout.leading)
        {
            if (c != ' ' && c != '\t')
            {
                c = ' ';
            }
        }
    }
    else if (last_line.find_first_not_of(" \t") == std::string_view::npos)
    {
        layout.leading.assign(last_line);
    }
    else // A comment between two tokens on a line separates them like a space.
    {
        layout.leading = " ";
    }
    return layout;
}

std::uint32_t column_of(const std::string & text, const std::size_t offset)
{
    const std::size_t newline = (offset == 0 ? std::string::npos : text.rfind('\n', offset - 1));
    const std::size_t line_start = (newline == std::string::npos ? 0 : newline + 1);
    return static_cast<std::uint32_t>(offset - line_start + 1);
}

// A '\' that ends a line joins the next line to it.
void join_spliced_lines(std::vector<pp_token> & tokens)
{
    std::vector<pp_token> joined;
    joined.reserve(tokens.size());

    for (std::size_t i = 0; i < tokens.size(); ++i)
    {
        const pp_token & tok = tokens[i];
        const bool ends_line = (i + 1 == tokens.size() || tokens[i + 1].starts_line);

        if (tok.is(lexer::punctuation_id::backslash) && ends_line)
        {
            if (i + 1 < tokens.size())
            {
                pp_token & next = tokens[i + 1];
                next.starts_line = tok.starts_line;
                next.leading     = (tok.starts_line ? tok.leading : std::string{ " " });
            }
            continue;
        }
        joined.push_back(std::move(tokens[i]));
    }

    tokens = std::move(joined);
}

} // namespace

std::vector<pp_token> tokenize_file(const std::uint32_t file_index, const source_manager & sources, diagnostics & diags)
{
    const source_file & file = sources.file(file_index);
    const std::string & text = file.text;
    std::vector<pp_token> tokens;

    const parse_utils_diagnostics_scope scope{ diags, file_index };
    try
    {
        lexer lex;
        lex.init_from_memory(text.c_str(), static_cast<std::uint32_t>(text.size()), file.display_name, k_lexer_flags);

        lexer::token tok;
        while (lex.next_token(&tok))
        {
            const std::size_t gap_begin = lex.get_last_whitespace_start();
            const std::size_t begin     = lex.get_last_whitespace_end();
            const std::size_t end       = lex.get_script_offset();

            gap_layout gap = read_gap(std::string_view{ text }.substr(gap_begin, begin - gap_begin));

            pp_token & t  = tokens.emplace_back();
            t.kind        = kind_of(tok);
            t.punct       = punctuation_of(tok);
            t.is_float    = tok.is_float();
            t.starts_line = (tokens.size() == 1 || gap.line_break);
            t.text.assign(text, begin, end - begin);
            t.leading     = std::move(gap.leading);
            t.location    = source_location{ file_index, tok.get_line_number(), column_of(text, begin) };
        }
    }
    catch (const parse_utils_error & error)
    {
        diags.error(source_location{ file_index, error.line, 0 }, error.message);
    }

    join_spliced_lines(tokens);
    return tokens;
}

bool lex_single_token(const std::string & text, pp_token * out_token)
{
    // Failing is the answer here, not an error to report.
    constexpr std::uint32_t quiet = lexer::flags::no_errors | lexer::flags::no_warnings | lexer::flags::no_fatal_errors;

    lexer lex;
    if (text.empty() ||
        !lex.init_from_memory(text.c_str(), static_cast<std::uint32_t>(text.size()), "(paste)", k_lexer_flags | quiet))
    {
        return false;
    }

    lexer::token tok;
    if (!lex.next_token(&tok) || lex.get_error_count() != 0 ||
        lex.get_last_whitespace_end() != 0 || lex.get_script_offset() != text.size())
    {
        return false;
    }

    out_token->kind     = kind_of(tok);
    out_token->punct    = punctuation_of(tok);
    out_token->is_float = tok.is_float();
    out_token->text     = text;
    return true;
}

} // namespace vclpp

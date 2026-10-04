// ================================================================================================
// -*- C++ -*-
// File: tokenizer.hpp
// Brief: The preprocessor's token, and splitting source files into tokens with parse-utils' lexer.
//
// This source code is released under the MIT license.
// See the accompanying LICENSE file for details.
// ================================================================================================

#pragma once

#include "lexer.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace vclpp
{

class diagnostics;
class source_manager;

// A position in a source file. Lines and columns are 1-based; zero means unknown.
struct source_location final
{
    std::uint32_t file_index = 0;
    std::uint32_t line       = 0;
    std::uint32_t column     = 0;
};

// The macro invocation a token came out of, so that diagnostics can show the chain of
// expansions behind it. All the tokens of one expansion share one.
struct expansion_origin final
{
    std::string                             macro_name;
    source_location                         invoked_at;
    std::shared_ptr<const expansion_origin> parent; // The expansion the invocation came from, if any.
};

// The macros a token must not be expanded as again (C's "hide set"), as interned
// macro name ids. Sorted, no duplicates.
using hide_set = std::vector<std::uint32_t>;

enum class token_kind : std::uint8_t
{
    identifier,
    number,
    string,
    char_literal,
    punctuation
};

struct pp_token final
{
    token_kind                              kind         = token_kind::punctuation;
    lexer::punctuation_id                   punct        = lexer::punctuation_id::none; // For token_kind::punctuation.
    bool                                    is_float     = false; // A floating-point number.
    bool                                    starts_line  = false; // The first token of a line.
    bool                                    blank_before = false; // Set off from the line before by a blank line.
    bool                                    blank_after  = false; // Set off from the line after by a blank line.
    std::string                             text;                 // The exact spelling.
    std::string                             leading;              // Whitespace before it: its indentation if it starts a line.
    source_location                         location;
    hide_set                                hidden;
    std::shared_ptr<const expansion_origin> origin;               // Null for a token read straight from a file.

    bool is(const lexer::punctuation_id id) const noexcept
    {
        return kind == token_kind::punctuation && punct == id;
    }

    bool is_identifier() const noexcept
    {
        return kind == token_kind::identifier;
    }
};

// Splits a loaded source file into tokens. Each token keeps its exact spelling and the
// whitespace before it; comments are dropped. A '\' that ends a line joins it to the
// next one. Lexical errors are reported through 'diags'.
std::vector<pp_token> tokenize_file(std::uint32_t file_index, const source_manager & sources, diagnostics & diags);

// Splits text from some point in a file into tokens, as tokenize_file() does: 'where' is
// the location of its first line, and each token's location is in that file, with its
// column counted in 'text'. The tokens come out of the expansion 'origin', if any.
std::vector<pp_token> tokenize_text(std::string_view text, const source_location & where,
                                    const std::shared_ptr<const expansion_origin> & origin, diagnostics & diags);

// Lexes text that must be exactly one token, e.g. the result of a ## paste. Fills in the
// token's kind and text only. Returns false if the text is not a single valid token.
bool lex_single_token(const std::string & text, pp_token * out_token);

} // namespace vclpp

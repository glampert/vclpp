// ================================================================================================
// -*- C++ -*-
// File: expressions.cpp
// Brief: Integer constant expressions: #if conditions and -x constant folding.
//
// This source code is released under the MIT license.
// See the accompanying LICENSE file for details.
// ================================================================================================

#include "expressions.hpp"
#include "diagnostics.hpp"

#include <optional>
#include <string>

namespace vclpp
{

namespace
{

using punct = lexer::punctuation_id;

bool is_integer(const pp_token & token)
{
    return token.kind == token_kind::number && !token.is_float;
}

bool is_value(const pp_token & token)
{
    return token.kind == token_kind::identifier || token.kind == token_kind::number ||
           token.is(punct::close_parentheses) || token.is(punct::close_bracket);
}

// Punctuation an expression can stand next to without becoming part of something larger.
bool is_separator(const pp_token & token)
{
    return token.is(punct::comma) || token.is(punct::colon) ||
           token.is(punct::open_parentheses)   || token.is(punct::close_parentheses) ||
           token.is(punct::open_bracket)       || token.is(punct::close_bracket) ||
           token.is(punct::open_curly_bracket) || token.is(punct::close_curly_bracket);
}

bool is_unary_operator(const pp_token & token)
{
    return token.is(punct::add) || token.is(punct::sub) || token.is(punct::bitwise_not);
}

// C's precedence for the binary operators -x folds; zero for anything else.
int binary_precedence(const pp_token & token)
{
    if (token.kind != token_kind::punctuation)
    {
        return 0;
    }

    switch (token.punct)
    {
    case punct::mul         :
    case punct::div         :
    case punct::mod         : return 6;
    case punct::add         :
    case punct::sub         : return 5;
    case punct::lshift      :
    case punct::rshift      : return 4;
    case punct::bitwise_and : return 3;
    case punct::bitwise_xor : return 2;
    case punct::bitwise_or  : return 1;
    default                 : return 0;
    } // switch (token.punct)
}

// Finds the longest integer constant expression starting at a position of a line.
class constant_expression_parser final
{
public:
    explicit constant_expression_parser(const std::vector<pp_token> & line)
        : m_line{ line }
    {
    }

    // The end of the expression, if one starts there.
    std::optional<std::size_t> parse(const std::size_t start)
    {
        m_applies_binary_operator = false;
        return binary(start, 1);
    }

    bool applies_binary_operator() const noexcept { return m_applies_binary_operator; }

private:
    std::optional<std::size_t> unary(const std::size_t pos)
    {
        if (pos >= m_line.size())
        {
            return std::nullopt;
        }

        const pp_token & token = m_line[pos];
        if (is_integer(token))
        {
            return pos + 1;
        }
        if (is_unary_operator(token))
        {
            return unary(pos + 1);
        }
        if (token.is(punct::open_parentheses))
        {
            const std::optional<std::size_t> end = binary(pos + 1, 1);
            if (end.has_value() && *end < m_line.size() && m_line[*end].is(punct::close_parentheses))
            {
                return *end + 1;
            }
        }
        return std::nullopt;
    }

    std::optional<std::size_t> binary(const std::size_t pos, const int min_precedence)
    {
        std::optional<std::size_t> end = unary(pos);
        while (end.has_value() && *end < m_line.size())
        {
            const int precedence = binary_precedence(m_line[*end]);
            if (precedence == 0 || precedence < min_precedence)
            {
                break;
            }

            // An operator without an operand after it is not part of the expression.
            const bool applied = m_applies_binary_operator;
            const std::optional<std::size_t> rhs_end = binary(*end + 1, precedence + 1);
            if (!rhs_end.has_value())
            {
                m_applies_binary_operator = applied;
                break;
            }

            end = rhs_end;
            m_applies_binary_operator = true;
        }
        return end;
    }

    const std::vector<pp_token> & m_line;
    bool                          m_applies_binary_operator = false;
};

} // namespace

expression_evaluator::expression_evaluator(diagnostics & diags)
    : m_diags{ diags }
{
}

std::int64_t expression_evaluator::evaluate(const std::span<const pp_token> tokens, const pp_token & where,
                                            const std::string_view context)
{
    const std::string prefix = std::string{ context } + ": ";

    std::string text;
    for (const pp_token & token : tokens)
    {
        if (token.kind == token_kind::number && token.is_float)
        {
            m_diags.error(token, prefix + "floating-point number '" + token.text + "' in an integer expression");
        }
        if (token.kind != token_kind::number && token.kind != token_kind::punctuation)
        {
            m_diags.error(token, prefix + "'" + token.text + "' cannot appear in an integer expression");
        }

        if (!text.empty())
        {
            text += ' ';
        }
        text += token.text;
    }

    if (text.empty())
    {
        m_diags.error(where, prefix + "missing expression");
    }

    std::int64_t result = 0;
    double float_result = 0.0;
    bool evaluated = false;

    const parse_utils_diagnostics_scope scope{ m_diags, diagnostics::use_location(where) };
    try
    {
        evaluated = m_preprocessor.eval(text, &result, &float_result, /* math_consts = */ false,
                                        /* math_funcs = */ false, /* undefined_consts_are_zero = */ false);
    }
    catch (const parse_utils_error & error)
    {
        // parse-utils ends its messages with a '!'.
        std::string message = error.message;
        if (message.ends_with('!'))
        {
            message.pop_back();
        }
        m_diags.error(where, prefix + message);
    }

    if (!evaluated)
    {
        m_diags.error(where, prefix + "cannot evaluate '" + text + "'");
    }
    return result;
}

void fold_constant_expressions(std::vector<pp_token> & line, expression_evaluator & evaluator)
{
    constant_expression_parser parser{ line };

    for (std::size_t start = 0; start < line.size(); ++start)
    {
        const pp_token & first = line[start];
        const bool operator_first = (is_unary_operator(first) || first.is(punct::open_parentheses));

        if (!is_integer(first) && !operator_first)
        {
            continue;
        }
        if (start > 0)
        {
            // An operator before it would take part, and so would a value before a
            // leading operator or parenthesis ("a -1 + 2" is "(a - 1) + 2").
            const pp_token & before = line[start - 1];
            if ((before.kind == token_kind::punctuation && !is_separator(before)) || (operator_first && is_value(before)))
            {
                continue;
            }
        }

        const std::optional<std::size_t> end = parser.parse(start);
        if (!end.has_value() || !parser.applies_binary_operator())
        {
            continue;
        }
        if (*end < line.size())
        {
            const pp_token & after = line[*end];
            if (after.kind == token_kind::punctuation && !is_separator(after))
            {
                continue;
            }
        }

        const std::span<const pp_token> expression{ line.data() + start, *end - start };
        const std::int64_t value = evaluator.evaluate(expression, first, "constant expression");

        pp_token folded    = first;
        folded.kind        = token_kind::number;
        folded.punct       = punct::none;
        folded.is_float    = false;
        folded.text        = std::to_string(value);
        folded.blank_after = line[*end - 1].blank_after;

        line.erase(line.begin() + static_cast<std::ptrdiff_t>(start) + 1, line.begin() + static_cast<std::ptrdiff_t>(*end));
        line[start] = std::move(folded);
    }
}

} // namespace vclpp

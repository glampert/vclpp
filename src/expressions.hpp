// ================================================================================================
// -*- C++ -*-
// File: expressions.hpp
// Brief: Integer constant expressions: #if conditions and -x constant folding.
//
// This source code is released under the MIT license.
// See the accompanying LICENSE file for details.
// ================================================================================================

#pragma once

#include "tokenizer.hpp"
#include "preprocessor.hpp"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace vclpp
{

class diagnostics;

// Evaluates integer constant expressions with the C operators, through the expression
// evaluator of parse-utils' preprocessor.
class expression_evaluator final
{
public:
    explicit expression_evaluator(diagnostics & diags);

    // Evaluates an expression made of integer numbers, operators and parentheses, e.g.
    // what is left of an #if line once its macros are expanded. Errors are reported at
    // 'where', prefixed with 'context'.
    std::int64_t evaluate(std::span<const pp_token> tokens, const pp_token & where, std::string_view context);

private:
    diagnostics &  m_diags;
    ::preprocessor m_preprocessor;
};

// Replaces the integer constant expressions on a line of output by their values, in
// decimal: "1010 + 0(vi00)" becomes "1010(vi00)". An expression is only folded where it
// stands on its own: not when an operator touches it from either side ("a - 1 - 2" and
// "1 + 2 * a" are left alone), and it must apply at least one binary operator.
void fold_constant_expressions(std::vector<pp_token> & line, expression_evaluator & evaluator);

} // namespace vclpp

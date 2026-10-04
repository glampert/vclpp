// ================================================================================================
// -*- C++ -*-
// File: output_writer.hpp
// Brief: Turns the preprocessed tokens back into text: one line of code per line, as it was
//        indented, with the optional VCL boilerplate around it.
//
// This source code is released under the MIT license.
// See the accompanying LICENSE file for details.
// ================================================================================================

#pragma once

#include "tokenizer.hpp"

#include <string>
#include <vector>

namespace vclpp
{

class expression_evaluator;

struct output_options final
{
    bool vcl_boilerplate    = false; // -j: the .init_* directives and the --enter/--exit blocks.
    bool fold_constants     = false; // -x: replace constant expressions by their values.
    bool flatten_subscripts = false; // -f: name[0] becomes name_0 and name[x] namex.
};

// Writes the code out as text. Each line keeps the indentation and spacing it was written
// with (a #macro's body lines, those of its definition); comments and blank lines are
// gone, except for a blank line around each #macro expansion. A #vuprog name becomes
// VCL's .name directive.
std::string write_output(std::vector<pp_token> tokens, const std::string & program_name,
                         const output_options & opts, expression_evaluator & evaluator);

} // namespace vclpp

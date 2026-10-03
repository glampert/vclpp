// ================================================================================================
// -*- C++ -*-
// File: parse_utils.cpp
// Brief: The implementation of parse-utils' header-only lexer and preprocessor, compiled here
//        once. Every other file includes the headers for their declarations only.
//
// parse-utils is released under the GNU GPL version 3; see external/parse-utils/LICENSE.
// ================================================================================================

#define LEXER_IMPLEMENTATION
#include "lexer.hpp"

// Needs the lexer's declarations first.
#define PREPROCESSOR_IMPLEMENTATION
#include "preprocessor.hpp"

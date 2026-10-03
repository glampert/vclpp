// ================================================================================================
// -*- C++ -*-
// File: diagnostics.hpp
// Brief: Error and warning reporting, GCC style: "file:line:col: error: message".
//
// This source code is released under the MIT license.
// See the accompanying LICENSE file for details.
// ================================================================================================

#pragma once

#include "tokenizer.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace vclpp
{

// Thrown once an error has been reported, to end the run.
class fatal_error final : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

// An extra line of context printed after an error or warning.
struct note final
{
    std::optional<source_location> location;
    std::string                    message;
};

// Reports errors and warnings to stderr. A diagnostic in an included file is preceded by
// the chain of #includes that led to it, and one on a token that came out of a macro
// expansion is followed by the chain of invocations behind it. Errors are fatal: error()
// reports and throws fatal_error. With warnings as errors, warnings print as errors but
// the run goes on, so that all of them are reported; failed() then says so.
class diagnostics final
{
public:
    diagnostics(const source_manager & sources, bool warnings_are_errors);

    [[noreturn]] void error(const std::string & message, const std::vector<note> & notes = {});
    [[noreturn]] void error(const source_location & location, const std::string & message, const std::vector<note> & notes = {});
    [[noreturn]] void error(const pp_token & token, const std::string & message, std::vector<note> notes = {});

    // 'flag' is the option controlling the warning, if any, e.g. "-Wundef".
    void warning(const std::string & message);
    void warning(const source_location & location, const std::string & message, std::string_view flag = {},
                 const std::vector<note> & notes = {});
    void warning(const pp_token & token, const std::string & message, std::string_view flag = {},
                 std::vector<note> notes = {});

    // Warnings were reported and they count as errors.
    bool failed() const noexcept { return m_warnings_are_errors && m_warning_count != 0; }

    // Where a token was used: its own location, or for a token out of a macro expansion,
    // where the outermost macro was invoked.
    static source_location use_location(const pp_token & token);

private:
    void report(const source_location * location, std::string_view severity, const std::string & message,
                std::string_view flag, const std::vector<note> & notes) const;

    std::string format_location(const source_location & location) const;
    static std::vector<note> expansion_notes(const pp_token & token);

    const source_manager & m_sources;
    const bool             m_warnings_are_errors;
    std::uint32_t          m_warning_count = 0;
};

// An error raised inside parse-utils, carried to the code that knows where in the
// source it happened.
struct parse_utils_error final
{
    std::uint32_t line;    // The lexer's line number.
    std::string   message;
};

// While alive, routes the diagnostics of parse-utils' lexer and expression evaluator to
// vclpp: errors throw parse_utils_error, and warnings are reported either on the lexer's
// line of a file or at a fixed location (e.g. that of the #if being evaluated). Scopes nest.
class parse_utils_diagnostics_scope final
{
public:
    parse_utils_diagnostics_scope(diagnostics & diags, std::uint32_t file_index);
    parse_utils_diagnostics_scope(diagnostics & diags, const source_location & fixed_location);
    ~parse_utils_diagnostics_scope();

    parse_utils_diagnostics_scope(const parse_utils_diagnostics_scope &) = delete;
    parse_utils_diagnostics_scope & operator = (const parse_utils_diagnostics_scope &) = delete;

private:
    std::unique_ptr<lexer::error_callbacks> m_callbacks;
    lexer::error_callbacks *                m_previous;
};

} // namespace vclpp

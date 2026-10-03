// ================================================================================================
// -*- C++ -*-
// File: diagnostics.cpp
// Brief: Error and warning reporting, GCC style: "file:line:col: error: message".
//
// This source code is released under the MIT license.
// See the accompanying LICENSE file for details.
// ================================================================================================

#include "diagnostics.hpp"
#include "source_manager.hpp"

#include <iostream>
#include <utility>

namespace vclpp
{

diagnostics::diagnostics(const source_manager & sources, const bool warnings_are_errors)
    : m_sources{ sources }
    , m_warnings_are_errors{ warnings_are_errors }
{
}

void diagnostics::error(const std::string & message, const std::vector<note> & notes)
{
    report(nullptr, "error", message, {}, notes);
    throw fatal_error{ message };
}

void diagnostics::error(const source_location & location, const std::string & message, const std::vector<note> & notes)
{
    report(&location, "error", message, {}, notes);
    throw fatal_error{ message };
}

void diagnostics::error(const pp_token & token, const std::string & message, std::vector<note> notes)
{
    std::vector<note> expansions = expansion_notes(token);
    notes.insert(notes.begin(), expansions.begin(), expansions.end());
    error(token.location, message, notes);
}

void diagnostics::warning(const std::string & message)
{
    ++m_warning_count;
    report(nullptr, (m_warnings_are_errors ? "error" : "warning"), message, {}, {});
}

void diagnostics::warning(const source_location & location, const std::string & message, const std::string_view flag,
                          const std::vector<note> & notes)
{
    ++m_warning_count;
    report(&location, (m_warnings_are_errors ? "error" : "warning"), message, flag, notes);
}

void diagnostics::warning(const pp_token & token, const std::string & message, const std::string_view flag,
                          std::vector<note> notes)
{
    std::vector<note> expansions = expansion_notes(token);
    notes.insert(notes.begin(), expansions.begin(), expansions.end());
    warning(token.location, message, flag, notes);
}

source_location diagnostics::use_location(const pp_token & token)
{
    if (token.origin == nullptr)
    {
        return token.location;
    }

    const expansion_origin * outermost = token.origin.get();
    while (outermost->parent != nullptr)
    {
        outermost = outermost->parent.get();
    }
    return outermost->invoked_at;
}

void diagnostics::report(const source_location * location, const std::string_view severity, const std::string & message,
                         const std::string_view flag, const std::vector<note> & notes) const
{
    std::string text;

    if (location != nullptr)
    {
        // The chain of #includes that led to the file. A long one - a file including
        // itself - is shortened to its two ends.
        std::vector<source_location> chain;
        for (auto from = m_sources.file(location->file_index).included_from; from.has_value();
             from = m_sources.file(from->file_index).included_from)
        {
            chain.push_back(*from);
        }

        constexpr std::size_t shown_at_each_end = 4;
        for (std::size_t i = 0; i < chain.size(); ++i)
        {
            if (chain.size() > 2 * shown_at_each_end && i == shown_at_each_end)
            {
                text += ",\n                 [" + std::to_string(chain.size() - 2 * shown_at_each_end) + " more]";
                i = chain.size() - shown_at_each_end - 1;
                continue;
            }
            text += (i == 0 ? "In file included from " : ",\n                 from ");
            text += format_location(chain[i]);
        }
        if (!chain.empty())
        {
            text += ":\n";
        }

        text += format_location(*location);
        text += ": ";
    }
    else
    {
        text += "vclpp: ";
    }

    text += severity;
    text += ": ";
    text += message;

    if (!flag.empty())
    {
        text += " [";
        text += flag;
        text += "]";
    }
    text += "\n";

    for (const note & n : notes)
    {
        if (n.location.has_value())
        {
            text += format_location(*n.location);
            text += ": ";
        }
        text += "note: ";
        text += n.message;
        text += "\n";
    }

    std::cerr << text << std::flush;
}

std::string diagnostics::format_location(const source_location & location) const
{
    std::string text = m_sources.file(location.file_index).display_name;
    if (location.line != 0)
    {
        text += ":" + std::to_string(location.line);
        if (location.column != 0)
        {
            text += ":" + std::to_string(location.column);
        }
    }
    return text;
}

std::vector<note> diagnostics::expansion_notes(const pp_token & token)
{
    std::vector<note> notes;
    for (const expansion_origin * origin = token.origin.get(); origin != nullptr; origin = origin->parent.get())
    {
        notes.push_back(note{ origin->invoked_at, "in expansion of macro '" + origin->macro_name + "'" });
    }
    return notes;
}

// ========================================================
// parse_utils_diagnostics_scope:
// ========================================================

namespace
{

// parse-utils ends most of its messages with a '!'.
std::string without_exclamation(std::string message)
{
    if (message.ends_with('!'))
    {
        message.pop_back();
    }
    return message;
}

class parse_utils_callbacks final : public lexer::error_callbacks
{
public:
    parse_utils_callbacks(diagnostics & diags, const std::uint32_t file_index, std::optional<source_location> fixed_location)
        : m_diags{ diags }
        , m_file_index{ file_index }
        , m_fixed_location{ std::move(fixed_location) }
    {
    }

    void error_at(const std::string &, const std::uint32_t line_num, const std::string & message, bool) override
    {
        throw parse_utils_error{ line_num, without_exclamation(message) };
    }

    void warning_at(const std::string &, const std::uint32_t line_num, const std::string & message) override
    {
        m_diags.warning(m_fixed_location.value_or(source_location{ m_file_index, line_num, 0 }), without_exclamation(message));
    }

    // Only the default error_at()/warning_at() call these.
    void error(const std::string & message, bool) override
    {
        throw parse_utils_error{ 0, without_exclamation(message) };
    }

    void warning(const std::string & message) override
    {
        m_diags.warning(without_exclamation(message));
    }

private:
    diagnostics &                  m_diags;
    std::uint32_t                  m_file_index;
    std::optional<source_location> m_fixed_location;
};

} // namespace

parse_utils_diagnostics_scope::parse_utils_diagnostics_scope(diagnostics & diags, const std::uint32_t file_index)
    : m_callbacks{ std::make_unique<parse_utils_callbacks>(diags, file_index, std::nullopt) }
    , m_previous{ lexer::get_error_callbacks() }
{
    lexer::set_error_callbacks(m_callbacks.get());
}

parse_utils_diagnostics_scope::parse_utils_diagnostics_scope(diagnostics & diags, const source_location & fixed_location)
    : m_callbacks{ std::make_unique<parse_utils_callbacks>(diags, fixed_location.file_index, fixed_location) }
    , m_previous{ lexer::get_error_callbacks() }
{
    lexer::set_error_callbacks(m_callbacks.get());
}

parse_utils_diagnostics_scope::~parse_utils_diagnostics_scope()
{
    lexer::set_error_callbacks(m_previous);
}

} // namespace vclpp

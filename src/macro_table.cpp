// ================================================================================================
// -*- C++ -*-
// File: macro_table.cpp
// Brief: The macros defined so far: #define constants and function-like macros, #macro blocks
//        and the built-ins.
//
// This source code is released under the MIT license.
// See the accompanying LICENSE file for details.
// ================================================================================================

#include "macro_table.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace vclpp
{

namespace
{

constexpr std::array<std::string_view, 4> k_builtin_names{ "__FILE__", "__LINE__", "__COUNTER__", "__VCLPP__" };

} // namespace

std::optional<std::size_t> macro::param_index(const pp_token & token) const
{
    if (!token.is_identifier())
    {
        return std::nullopt;
    }

    const auto it = std::find(params.begin(), params.end(), token.text);
    if (it != params.end())
    {
        return static_cast<std::size_t>(it - params.begin());
    }

    if (variadic && token.text == "__VA_ARGS__")
    {
        return params.size();
    }
    return std::nullopt;
}

bool same_definition(const macro & a, const macro & b)
{
    if (a.kind != b.kind || a.params != b.params || a.variadic != b.variadic || a.body.size() != b.body.size())
    {
        return false;
    }

    // Same tokens, and whitespace in the same places (C does not care how much).
    for (std::size_t i = 0; i < a.body.size(); ++i)
    {
        const pp_token & ta = a.body[i];
        const pp_token & tb = b.body[i];

        if (ta.text != tb.text || ta.starts_line != tb.starts_line || ta.leading.empty() != tb.leading.empty())
        {
            return false;
        }
    }
    return true;
}

macro_table::macro_table()
{
    for (const std::string_view name : k_builtin_names)
    {
        macro builtin;
        builtin.name = name;
        builtin.kind = macro_kind::builtin;
        define(std::move(builtin));
    }
}

const macro * macro_table::find(const std::string_view name) const
{
    const auto it = m_macros.find(name);
    return (it != m_macros.end() ? &it->second : nullptr);
}

std::optional<macro> macro_table::define(macro m)
{
    m.id = intern(m.name);

    std::optional<macro> previous;
    if (const auto it = m_macros.find(m.name); it != m_macros.end())
    {
        previous = std::move(it->second);
        it->second = std::move(m);
    }
    else
    {
        std::string name = m.name;
        m_macros.emplace(std::move(name), std::move(m));
    }
    return previous;
}

void macro_table::undefine(const std::string_view name)
{
    if (const auto it = m_macros.find(name); it != m_macros.end())
    {
        m_macros.erase(it);
    }
}

std::uint32_t macro_table::intern(const std::string_view name)
{
    if (const auto it = m_ids.find(name); it != m_ids.end())
    {
        return it->second;
    }

    const auto id = static_cast<std::uint32_t>(m_ids.size());
    m_ids.emplace(std::string{ name }, id);
    return id;
}

bool macro_table::is_builtin_name(const std::string_view name)
{
    return std::find(k_builtin_names.begin(), k_builtin_names.end(), name) != k_builtin_names.end();
}

} // namespace vclpp

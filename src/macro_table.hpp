// ================================================================================================
// -*- C++ -*-
// File: macro_table.hpp
// Brief: The macros defined so far: #define constants and function-like macros, #macro blocks
//        and the built-ins.
//
// This source code is released under the MIT license.
// See the accompanying LICENSE file for details.
// ================================================================================================

#pragma once

#include "tokenizer.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vclpp
{

enum class macro_kind : std::uint8_t
{
    object_like,   // #define NAME body
    function_like, // #define NAME(params) body
    block,         // #macro NAME: params ... #endmacro, invoked as NAME{ args }
    builtin        // __FILE__, __LINE__, __COUNTER__ and __VCLPP__
};

struct macro final
{
    std::string              name;
    std::uint32_t            id       = 0; // The interned name, for hide sets.
    macro_kind               kind     = macro_kind::object_like;
    std::vector<std::string> params;
    bool                     variadic = false; // Function-like with a trailing '...' (__VA_ARGS__).
    std::vector<pp_token>    body;
    source_location          defined_at;

    // The index of the parameter the token names, if any. __VA_ARGS__ is the one after the named ones.
    std::optional<std::size_t> param_index(const pp_token & token) const;
};

// The two definitions are the same, as C allows a macro to be redefined with.
bool same_definition(const macro & a, const macro & b);

class macro_table final
{
public:
    macro_table(); // Defines the built-ins.

    const macro * find(std::string_view name) const;

    // Defines or redefines a macro, returning the definition it replaced, if any.
    std::optional<macro> define(macro m);

    void undefine(std::string_view name);

    // The id hide sets know the name by.
    std::uint32_t intern(std::string_view name);

    static bool is_builtin_name(std::string_view name);

private:
    // std::less<> lets these be searched with a string_view, without making a string.
    template<typename T>
    using string_map = std::map<std::string, T, std::less<>>;

    string_map<macro>         m_macros;
    string_map<std::uint32_t> m_ids;
};

} // namespace vclpp

// ================================================================================================
// -*- C++ -*-
// File: source_manager.cpp
// Brief: Source files: loading them, finding #includes and remembering #pragma once.
//
// This source code is released under the MIT license.
// See the accompanying LICENSE file for details.
// ================================================================================================

#include "source_manager.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

namespace vclpp
{

namespace fs = std::filesystem;

source_manager::source_manager(std::vector<fs::path> include_dirs)
    : m_include_dirs{ std::move(include_dirs) }
{
}

std::optional<std::uint32_t> source_manager::load_file(const fs::path & path, std::optional<source_location> included_from)
{
    std::ifstream stream{ path, std::ios::binary };
    if (!stream)
    {
        return std::nullopt;
    }

    std::string text{ std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{} };
    if (stream.bad())
    {
        return std::nullopt;
    }

    m_files.push_back(source_file{ path, path.lexically_normal().generic_string(), std::move(text), included_from });
    return static_cast<std::uint32_t>(m_files.size() - 1);
}

std::uint32_t source_manager::add_text(std::string display_name, std::string text)
{
    m_files.push_back(source_file{ fs::path{}, std::move(display_name), std::move(text), std::nullopt });
    return static_cast<std::uint32_t>(m_files.size() - 1);
}

std::optional<fs::path> source_manager::find_include(const std::string & name, const bool angled,
                                                     const std::uint32_t includer, std::vector<fs::path> * tried) const
{
    const fs::path name_path{ name };
    std::vector<fs::path> candidates;

    if (name_path.is_absolute())
    {
        candidates.push_back(name_path);
    }
    else
    {
        if (!angled)
        {
            candidates.push_back(file(includer).path.parent_path() / name_path);
        }
        for (const fs::path & dir : m_include_dirs)
        {
            candidates.push_back(dir / name_path);
        }
        if (!angled)
        {
            candidates.push_back(name_path);
        }
    }

    for (const fs::path & candidate : candidates)
    {
        const fs::path normal = candidate.lexically_normal();
        if (std::find(tried->begin(), tried->end(), normal) != tried->end())
        {
            continue; // The includer's directory and the working directory are often the same.
        }
        tried->push_back(normal);

        std::error_code error;
        if (fs::is_regular_file(normal, error))
        {
            return normal;
        }
    }
    return std::nullopt;
}

void source_manager::mark_include_once(const std::uint32_t file_index)
{
    const fs::path & path = file(file_index).path;
    if (!path.empty())
    {
        m_include_once.insert(file_identity(path));
    }
}

bool source_manager::is_include_once(const fs::path & path) const
{
    return m_include_once.count(file_identity(path)) != 0;
}

std::string source_manager::file_identity(const fs::path & path)
{
    // The same file can be reached through different relative paths.
    std::error_code error;
    const fs::path canonical = fs::weakly_canonical(path, error);
    return (error ? fs::absolute(path, error) : canonical).generic_string();
}

} // namespace vclpp

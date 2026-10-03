// ================================================================================================
// -*- C++ -*-
// File: source_manager.hpp
// Brief: Source files: loading them, finding #includes and remembering #pragma once.
//
// This source code is released under the MIT license.
// See the accompanying LICENSE file for details.
// ================================================================================================

#pragma once

#include "tokenizer.hpp"

#include <cstdint>
#include <deque>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace vclpp
{

// One inclusion of a file. A file included twice is loaded twice, so that each
// inclusion knows where it was included from.
struct source_file final
{
    std::filesystem::path          path;          // As opened; empty for text that is not a file.
    std::string                    display_name;  // What diagnostics and __FILE__ call it.
    std::string                    text;
    std::optional<source_location> included_from;
};

class source_manager final
{
public:
    explicit source_manager(std::vector<std::filesystem::path> include_dirs);

    // Loads a file. Returns its index, or nothing if it cannot be read.
    std::optional<std::uint32_t> load_file(const std::filesystem::path & path,
                                           std::optional<source_location> included_from);

    // Adds text that does not come from a file, e.g. the value of a -D option.
    std::uint32_t add_text(std::string display_name, std::string text);

    // Finds the file an #include names. "name" is looked for next to the file that
    // includes it, then in each -I directory, then in the working directory (where
    // vclpp 1 looked); <name> only in the -I directories. Returns nothing if no
    // candidate exists, listing them in 'tried'.
    std::optional<std::filesystem::path> find_include(const std::string & name, bool angled,
                                                      std::uint32_t includer,
                                                      std::vector<std::filesystem::path> * tried) const;

    // #pragma once: the file at that path is not included again.
    void mark_include_once(std::uint32_t file_index);
    bool is_include_once(const std::filesystem::path & path) const;

    const source_file & file(const std::uint32_t index) const { return m_files.at(index); }

private:
    static std::string file_identity(const std::filesystem::path & path);

    std::vector<std::filesystem::path> m_include_dirs;
    std::deque<source_file>            m_files;        // A deque, so references stay valid as files are added.
    std::unordered_set<std::string>    m_include_once; // file_identity() of each #pragma once file.
};

} // namespace vclpp

// ================================================================================================
// -*- C++ -*-
// File: engine.hpp
// Brief: The preprocessor proper: reads the source a line at a time, carries out directives
//        and expands macros in the code.
//
// This source code is released under the MIT license.
// See the accompanying LICENSE file for details.
// ================================================================================================

#pragma once

#include "expander.hpp"
#include "expressions.hpp"
#include "macro_table.hpp"
#include "masp.hpp"
#include "tokenizer.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace vclpp
{

class diagnostics;
class source_manager;

struct options final
{
    std::string                                      input_path;
    std::string                                      output_path;             // Empty for stdout.
    bool                                             vcl_boilerplate = false; // -j
    bool                                             fold_constants  = false; // -x
    bool                                             flatten_subscripts = false; // -f
    bool                                             warn_undef      = false; // -Wundef
    bool                                             werror          = false; // -Werror
    std::vector<std::string>                         include_dirs;            // -I
    std::vector<std::pair<std::string, std::string>> defines;                 // -D name=value
    bool                                             masp = false;            // -m: MASP mode.
    masp_options                                     masp_settings;           // -c and MASP mode's -D.
};

struct preprocessed final
{
    std::vector<pp_token> tokens;       // The code, a token at a time; tokens that start a line say so.
    std::string           program_name; // From #vuprog, if it names one.
};

class engine final
{
public:
    engine(const options & opts, source_manager & sources, diagnostics & diags);

    preprocessed run();

private:
    // A file being read: the main one, or one it #includes.
    struct file_frame final
    {
        std::uint32_t         file_index;
        std::vector<pp_token> tokens;
        std::size_t           next;              // The next token to read.
        std::size_t           conditional_depth; // m_conditionals.size() when the file was entered.
        bool                  macros_only;       // Only its directives count (MASP mode's #include).
    };

    // An #if/#ifdef/#ifndef and the #elif/#else that follow it.
    struct conditional final
    {
        std::string     directive;     // The one that opened it.
        source_location opened_at;
        bool            parent_active; // The code around it is not being skipped.
        bool            branch_taken;  // One of its branches has been taken.
        bool            active;        // The current branch is being taken.
        bool            seen_else;
    };

    // Lines and files.
    void enter_file(std::uint32_t file_index);
    void leave_file();
    static bool take_line(file_frame & frame, std::vector<pp_token> & line);
    bool next_line(std::vector<pp_token> & line);
    bool is_active() const;
    std::size_t file_conditional_depth() const;
    void process_code_line(std::vector<pp_token> & line);

    // Directives.
    void handle_directive(const std::vector<pp_token> & line);
    void handle_include(const std::vector<pp_token> & line);
    void handle_define(const std::vector<pp_token> & line);
    void handle_undef(const std::vector<pp_token> & line);
    void handle_block_macro(const std::vector<pp_token> & line);
    void handle_vuprog(const std::vector<pp_token> & line);
    void handle_endvuprog(const std::vector<pp_token> & line);
    void handle_pragma(const std::vector<pp_token> & line);
    void handle_if(const std::vector<pp_token> & line);
    void handle_ifdef(const std::vector<pp_token> & line, bool negated);
    void handle_elif(const std::vector<pp_token> & line);
    void handle_else(const std::vector<pp_token> & line);
    void handle_endif(const std::vector<pp_token> & line);

    // Directive helpers.
    conditional & current_conditional(const pp_token & directive);
    bool evaluate_condition(const std::vector<pp_token> & line);
    void check_macro_name(const pp_token & name, bool undefining);
    void check_paste_operators(const macro & m);
    void define_macro(macro m, const pp_token & name);
    void define_command_line_macros();
    void warn_extra_tokens(const std::vector<pp_token> & line, std::size_t first_extra);

    const options &                                  m_options;
    source_manager &                                 m_sources;
    diagnostics &                                    m_diags;
    macro_table                                      m_macros;
    expander                                         m_expander;
    expression_evaluator                             m_evaluator;
    std::vector<file_frame>                          m_files;
    std::unique_ptr<masp_reader>                     m_masp;         // MASP mode: the main file's lines.
    std::vector<conditional>                         m_conditionals;
    std::vector<pp_token>                            m_output;
    std::unordered_map<std::string, source_location> m_used_undefined; // Identifiers left in the code, where first seen.
    std::optional<source_location>                   m_vuprog_at;
    std::optional<source_location>                   m_endvuprog_at;
    std::string                                      m_program_name;
};

} // namespace vclpp
